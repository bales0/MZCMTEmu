#include "audio/player.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "audio/wav_reader.h"
#include "audio/wav_pulse_stream.h"
#include "audio/raw_pulse.h"
#include "audio/mzf_stream.h"
#include "formats/sidecar.h"
#include "formats/mz_tape_profiles.h"
#include "formats/tape_metadata.h"
#include "record/record_autoname.h"
#include "audio/edge_stream.h"
#include "audio/tap_stream.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#define PULSE_CHUNK 256U
#define PULSE_PREFILL_PULSES (PULSE_CHUNK * 12U)
/* RMT primes 128 symbols (up to 256 source intervals). Sparse WAV must
 * leave data for its next refill instead of terminating at initial encode. */
#define WAV_PREFILL_MIN_PULSES 256U
#define WAV_PREFILL_US 300000ULL
#define PLAYER_TASK_STACK_WORDS 8192U
#define PLAYER_TASK_PRIORITY 5U
#define PLAYER_TASK_CORE 0
#define PLAYER_TASK_ACTIVE_DELAY_MS 1U
#define PLAYER_TASK_IDLE_DELAY_MS 5U
#define PLAYER_UL_IDLE_INTERVAL_MS 100U

static program_metadata_store_t metadata_store;
static program_metadata_t metadata_last;
static struct {uint64_t at;
program_metadata_t value;
} metadata_queue[4];
static unsigned metadata_head,metadata_tail;
static uint64_t metadata_source_us;
static uint32_t metadata_revision;
static bool metadata_audio;
void player_metadata(program_metadata_t *out){program_metadata_snapshot(&metadata_store,out);
}
static void metadata_reset(file_format_t format){
 program_metadata_reset(&metadata_last,format);
 program_metadata_publish(&metadata_store,&metadata_last);
 metadata_head=metadata_tail=0;
 metadata_source_us=0;
 metadata_revision=0;
 metadata_audio=false;
}
static void metadata_collect(void){
 uint32_t revision=metadata_audio ? record_autoname_metadata_revision():tap_stream_metadata_revision();
 if(revision==metadata_revision)return;
 metadata_revision=revision;
 program_metadata_t value;
 if(metadata_audio)tape_metadata_snapshot(&value);
 else tap_stream_metadata(&value);
 program_metadata_t key=value;
 key.received_bytes=0;
 program_metadata_t old=metadata_last;
 old.received_bytes=0;
 if(!memcmp(&key,&old,sizeof(key)))return;
 metadata_last=value;
 unsigned next=(metadata_head+1)%4;
 if(next==metadata_tail){ /* Never stall transport for diagnostics. */
  value.valid=0;
  value.confidence=PROGRAM_UNKNOWN;
  metadata_queue[(metadata_head+3)%4].value=value;
  return;
 }
 metadata_queue[metadata_head].at=(metadata_source_us+8U)/16U;
 metadata_queue[metadata_head].value=value;
 metadata_head=next;
}
static void metadata_confirm(void){
 uint64_t played=raw_pulse_get_completed_us();
 while(metadata_tail!=metadata_head && metadata_queue[metadata_tail].at<=played){
  program_metadata_publish(&metadata_store,&metadata_queue[metadata_tail].value);
  metadata_tail=(metadata_tail+1)%4;
 }
}
static raw_pulse_t pulse_buf[PULSE_CHUNK];
static uint32_t pulse_ticks[PULSE_CHUNK];

static wav_info_t s_info;
static mzf_stream_info_t s_mzf_info;
static edge_stream_info_t s_edge_info;
static bool s_binary_tape = false;
static bool s_edge_tape = false;
static bool s_tap_tape = false;
static uint64_t s_wav_final_us;
static bool player_is_wav(void) { return !s_binary_tape && !s_edge_tape && !s_tap_tape;
}
static tap_speed_t s_tap_speed = TAP_SPEED_1400;
static tap_stream_info_t s_tap_info;
static uint64_t s_tap_ticks, s_tap_queued_us, s_tap_final_us;
static uint64_t s_binary_elapsed_us, s_binary_final_us;
static uint64_t s_edge_final_us;
static uint32_t s_binary_source_bytes,s_binary_final_bytes;
/* 64 KiB UI metadata, allocated once in PSRAM at init, never used by ISR/DMA. */
static uint8_t *s_record_modes;
static char s_record_modes_path[1024];
static bool s_binary_waiting, s_binary_motor_low_seen;
static int64_t s_binary_wait_until, s_binary_pause_remaining;
static uint64_t player_binary_elapsed_us(void);
static uint32_t player_binary_completed_bytes(void);
static bool s_ultrafast_enabled = false;
static int s_playing = 0;
static int s_eof = 0;
static int s_read_error = 0;
static player_state_t s_state = PLAYER_STATE_IDLE;
static const char *last_error;
static unsigned last_stop_cause;
const char *player_state_name(player_state_t state){
    static const char *const names[]={"IDLE","PLAYING","PAUSED","STOPPED","EOF","UNDERRUN","ERROR","READY"};
    return (unsigned)state<sizeof(names)/sizeof(names[0]) ? names[state]:"UNKNOWN";
}
const char *player_stop_cause_name(void){
    static const char *const names[]={"NONE","API_STOP","BUTTON_STOP","BUTTON_STOP_LONG","S1_EXTERNAL","SD_REMOVED","SD_REINIT","SD_RESTORE_FAILED","RECORD_START"};
    unsigned cause=__atomic_load_n(&last_stop_cause,__ATOMIC_ACQUIRE);
    return cause<sizeof(names)/sizeof(names[0]) ? names[cause]:"UNKNOWN";
}
const char *player_error_message(void){const char *s=__atomic_load_n(&last_error,__ATOMIC_ACQUIRE);return s ? s:"PLAYBACK ERROR";}
static void player_error(const char *message){
    __atomic_store_n(&last_error,message,__ATOMIC_RELEASE);
    printf("player: ERROR %s\n",message);
}
static player_pause_reason_t s_pause_reason = PLAYER_PAUSE_NONE;
static SemaphoreHandle_t s_player_mutex = NULL;
static TaskHandle_t s_player_task = NULL;

/*
 * Pulse staging state.  A stream pulse is never discarded after it was read
 * from MZF/L16/LEP.  raw_pulse_write() may accept less than a whole block;
 * the remaining pulses stay here until the GPTimer FIFO accepts them.
 */
static size_t s_pulse_pending_pos = 0;
static size_t s_pulse_pending_len = 0;

/* Playback information for UI. */
static uint32_t s_bytes_queued = 0;
static char s_current_path[1024];

static void player_service_locked(void);

static void player_lock(void)
{
    if (s_player_mutex != NULL) {
        xSemaphoreTake(s_player_mutex, portMAX_DELAY);
    }
}

static void player_unlock(void)
{
    if (s_player_mutex != NULL) {
        xSemaphoreGive(s_player_mutex);
    }
}

/* Serialize application policy with UL ownership changes on the player task.
 * A stale policy snapshot must never overwrite an active handshake. */
bool player_set_sense_level(bool high)
{
    player_lock();
    bool allowed=!mzf_stream_is_ultrafast_active();
    if(allowed) gpio_set_level(GPIO_NUM_7,high);
    player_unlock();
    return allowed;
}

static void player_task_loop(void *arg)
{
    (void)arg;
    TickType_t last_idle=xTaskGetTickCount();

    for (;;) {
        bool active,ul_data;
        TickType_t delay_ticks;

        player_lock();
        player_service_locked();
        active = (s_playing != 0);
        ul_data=active && mzf_stream_is_ultrafast_active() && mzf_stream_get_ultrafast_sent_bytes()!=0;
        player_unlock();

        /* UL pump returns only at byte/HIGH boundaries. Keep feeding it like
         * the reference foreground loop, but let the idle task run periodically (100 ms of data work). */
        TickType_t now=xTaskGetTickCount();
        TickType_t interval=pdMS_TO_TICKS(PLAYER_UL_IDLE_INTERVAL_MS);
        if(!interval)interval=1;
        if(ul_data && (TickType_t)(now-last_idle)<interval)continue;

        delay_ticks = pdMS_TO_TICKS(active ?
            PLAYER_TASK_ACTIVE_DELAY_MS : PLAYER_TASK_IDLE_DELAY_MS);
        if (delay_ticks == 0) {
            delay_ticks = 1;
        }
        vTaskDelay(delay_ticks);
        last_idle=xTaskGetTickCount();
    }
}

static bool player_uses_pulse_output(void)
{
    return true;
}

static bool path_is_binary_tape(const char *path)
{
    const char *ext;

    if (path == NULL) {
        return false;
    }

    ext = strrchr(path, '.');
    if (ext == NULL) {
        return false;
    }

    return (strcasecmp(ext, ".mzf") == 0) ||
           (strcasecmp(ext, ".mzq") == 0) ||
           (strcasecmp(ext, ".qdf") == 0) ||
           (strcasecmp(ext, ".qd") == 0) ||
           (strcasecmp(ext, ".mzt") == 0) ||
           (strcasecmp(ext, ".m12") == 0);
}

static bool path_is_edge_tape(const char *path)
{
    return edge_stream_is_edge_path(path);
}

static void player_reset_pulse_pending(void)
{
    s_pulse_pending_pos = 0U;
    s_pulse_pending_len = 0U;
}

static bool player_pulse_has_pending(void)
{
    return s_pulse_pending_pos < s_pulse_pending_len;
}

static void player_mark_pulse_source_end(void)
{
    if (s_tap_tape) {
        if (tap_stream_has_error()) s_read_error = 1;
        else if (tap_stream_is_eof()) s_eof = 1;
        else s_read_error = 1;
        return;
    }

    if (s_binary_tape) {
        if (mzf_stream_at_boundary() || mzf_stream_is_ultrafast_ready() || mzf_stream_is_eof()) {
            s_eof = 1;
        } else if (mzf_stream_has_error()) {
            s_read_error = 1;
        } else {
            s_read_error = 1;
        }
        return;
    }

    if(player_is_wav()) {
        if(wav_pulse_stream_error()) s_read_error=1;
        else if(wav_pulse_stream_eof()) s_eof=1;
        else s_read_error=1;
        return;
    }
    if (edge_stream_has_error()) {
        s_read_error = 1;
    } else if (edge_stream_is_eof() ||
               edge_stream_get_consumed_bytes() >= edge_stream_get_total_bytes()) {
        s_eof = 1;
    } else {
        s_read_error = 1;
    }
}

static void player_finish_stop(player_state_t final_state)
{
    printf("player: finish %s -> %s completed_us=%llu path=%s\n",player_state_name(s_state),player_state_name(final_state),(unsigned long long)raw_pulse_get_completed_us(),s_current_path);
    if(final_state==PLAYER_STATE_ERROR && !__atomic_load_n(&last_error,__ATOMIC_ACQUIRE))player_error("SOURCE / TX ERROR");
    if (s_edge_tape) s_edge_final_us = raw_pulse_get_completed_us();
    if (s_tap_tape) s_tap_final_us = raw_pulse_get_completed_us();
    if (player_is_wav()) s_wav_final_us=raw_pulse_get_completed_us();
    if (s_binary_tape) {s_binary_final_us=player_binary_elapsed_us();s_binary_final_bytes=player_binary_completed_bytes();}
    raw_pulse_stop();

    wav_reader_close();
    /* QD completion or the transport STOP returns to its selector while
     * retaining the source/index. Leaving the image/error/removal closes it. */
    if((final_state==PLAYER_STATE_EOF || final_state==PLAYER_STATE_READY) &&
       s_binary_tape && mzf_stream_details()->format==FILE_FORMAT_MZQ){
        uint16_t record=mzf_stream_details()->record;
        loader_mode_t mode=mzf_stream_details()->requested;
        if(mzf_stream_select_mode(s_current_path,record,mode)){
            gpio_set_level(GPIO_NUM_7,1); /* Release the cassette button in READY. */
            program_metadata_publish(&metadata_store,&mzf_stream_details()->metadata);
            final_state=PLAYER_STATE_READY;
        } else {mzf_stream_close();player_error("QD SELECT");final_state=PLAYER_STATE_ERROR;}
    } else mzf_stream_close();
    edge_stream_close();
    tap_stream_close();

    s_playing = 0;
    s_eof = 0;
    s_read_error = 0;
    player_reset_pulse_pending();
    s_state = final_state;
    s_pause_reason = PLAYER_PAUSE_NONE;

    /* Keep UI at 100% after a normal EOF. */
    if (final_state == PLAYER_STATE_EOF) {
        if (s_binary_tape) {
            s_bytes_queued = s_mzf_info.total_samples;
        } else if (s_edge_tape) {
            s_bytes_queued = s_edge_info.total_samples;
        } else {
            s_bytes_queued = s_info.data_size;
        }
    } else {
        s_bytes_queued = 0;
    }

    /* s_current_path intentionally remains for the UI. */
}

static bool player_fill_pulse_pending(void)
{
    size_t count = 0U;

    if (player_pulse_has_pending() || s_eof || s_read_error) {
        return false;
    }

    bool native=raw_pulse_native_ticks16();
    while (count < (player_is_wav() ? 32U:PULSE_CHUNK)) {
        uint8_t level = 0U;
        uint32_t duration_us = 0U;
        bool count_pulse = true;
        bool ok;

        if (s_binary_tape) {
            ok = native ? mzf_stream_next_ticks16(&level,&duration_us):mzf_stream_next_pulse(&level, &duration_us);
        } else if (s_tap_tape) {
            uint32_t ticks;
            ok = tap_stream_next_pulse(&level, &ticks, &count_pulse);
            if (ok) {
                /* Quantize absolute transition times, not each half pulse:
                 * total error remains <= 0.5 us at this legacy 1 MHz HAL.
                 * The source retains 16 MHz ticks for a future native HAL. */
                s_tap_ticks += ticks;
                uint64_t end_us = (s_tap_ticks + 8U) / 16U;
                duration_us = native ? ticks:(uint32_t)(end_us - s_tap_queued_us);
                s_tap_queued_us = end_us;
            }
        } else if(player_is_wav()) {
            ok=native ? wav_pulse_stream_next_ticks16(&level,&duration_us):wav_pulse_stream_next(&level,&duration_us);
            count_pulse=level!=0;
        } else {
            ok = edge_stream_next_pulse(&level, &duration_us, &count_pulse);
        }

        if (!ok) {
            if(metadata_audio){tape_metadata_end();
            metadata_collect();
            }
            player_mark_pulse_source_end();
            break;
        }

        if(metadata_audio){
            tape_metadata_feed(level,native && !s_edge_tape ? (duration_us+8U)/16U:duration_us,
                s_edge_tape && edge_stream_monitor_muted());
            metadata_collect();
        } else if(s_tap_tape)metadata_collect();
        metadata_source_us+=native && !s_edge_tape ? duration_us:(uint64_t)duration_us*16U;
        pulse_ticks[count]=native && !s_edge_tape ? duration_us:duration_us*16U;
        pulse_buf[count].level = level ? 1U : 0U;
        pulse_buf[count].count_pulse = count_pulse ? RAW_PULSE_COUNT : 0U;
        if((s_edge_tape && edge_stream_monitor_muted()) || (!count_pulse && s_tap_tape && duration_us >= (native ? 1600000U:100000U)))
            pulse_buf[count].count_pulse |= RAW_PULSE_MONITOR_MUTE;
        if((s_edge_tape && !edge_stream_monitor_muted()) ||
           (s_binary_tape && mzf_stream_last_pulse_source_byte()))pulse_buf[count].count_pulse|=RAW_PULSE_SOURCE_BYTE;
        pulse_buf[count].duration_us = duration_us == 0U ? 1U : duration_us;
        count++;
    }

    s_pulse_pending_pos = 0U;
    s_pulse_pending_len = count;
    return count != 0U;
}

static bool player_feed_one_pulse_chunk(void)
{
    size_t written = 0U;
    bool progressed = false;

    if (!player_pulse_has_pending()) {
        (void)player_fill_pulse_pending();
    }

    if (player_pulse_has_pending()) {
        written = raw_pulse_native_ticks16() ?
            raw_pulse_write_ticks16(&pulse_buf[s_pulse_pending_pos],&pulse_ticks[s_pulse_pending_pos],s_pulse_pending_len-s_pulse_pending_pos):
            raw_pulse_write(&pulse_buf[s_pulse_pending_pos],s_pulse_pending_len-s_pulse_pending_pos);
        if (written != 0U) {
            s_pulse_pending_pos += written;
            s_bytes_queued += (uint32_t)written;
            progressed = true;
        }

        if (!player_pulse_has_pending()) {
            player_reset_pulse_pending();
        }
    }

    /* Finish is legal only after the final stream pulse reached the GPTimer FIFO. */
    if (s_eof && !player_pulse_has_pending()) {
        raw_pulse_finish();
    }

    return progressed;
}

void player_init(void)
{
    if (!s_record_modes) s_record_modes=heap_caps_malloc(65536U,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (s_record_modes) memset(s_record_modes,255,65536U);
    metadata_reset(FILE_FORMAT_UNKNOWN);
    s_record_modes_path[0]=0;
    if (s_player_mutex == NULL) {
        s_player_mutex = xSemaphoreCreateMutex();
    }

    mzf_stream_set_ultrafast_enabled(false);

    s_playing = 0;
    s_eof = 0;
    s_read_error = 0;
    s_state = PLAYER_STATE_IDLE;
    s_pause_reason = PLAYER_PAUSE_NONE;
    player_reset_pulse_pending();
    s_bytes_queued = 0U;
    s_binary_tape = false;
    s_edge_tape = false;
    s_tap_tape = false;
    memset(&s_tap_info, 0, sizeof(s_tap_info));
    s_tap_ticks = s_tap_queued_us = s_tap_final_us = 0;
    s_binary_elapsed_us = s_binary_final_us = 0;
    s_edge_final_us=0;
    s_binary_source_bytes=s_binary_final_bytes=0;
    s_binary_waiting = s_binary_motor_low_seen = false;
    memset(&s_info, 0, sizeof(s_info));
    memset(&s_mzf_info, 0, sizeof(s_mzf_info));
    memset(&s_edge_info, 0, sizeof(s_edge_info));
    s_current_path[0] = '\0';
}

void player_task_start(void)
{
    if (s_player_mutex == NULL) {
        player_init();
    }

    if (s_player_task != NULL) {
        return;
    }

    xTaskCreatePinnedToCore(
        player_task_loop,
        "player_task",
        PLAYER_TASK_STACK_WORDS,
        NULL,
        PLAYER_TASK_PRIORITY,
        &s_player_task,
        PLAYER_TASK_CORE);
}

void player_start(const char *path)
{
    char path_copy[sizeof(s_current_path)];
    snprintf(path_copy,sizeof(path_copy),"%s",path ? path:"");
    path=path_copy;
    player_lock();
    __atomic_store_n(&last_error,NULL,__ATOMIC_RELEASE);
    __atomic_store_n(&last_stop_cause,PLAYER_STOP_NONE,__ATOMIC_RELEASE);
    printf("player: request path=%s mode=%u/%s\n",path,(unsigned)mzf_stream_get_loader_mode(),loader_mode_name(mzf_stream_get_loader_mode()));

    if (s_state == PLAYER_STATE_PLAYING || s_state == PLAYER_STATE_PAUSED) {
        player_finish_stop(PLAYER_STATE_STOPPED);
    }

    snprintf(s_current_path,sizeof(s_current_path),"%s",path);
    metadata_reset(file_format_detect_from_name(path));
    s_binary_tape = path_is_binary_tape(path);
    s_edge_tape = path_is_edge_tape(path);
    s_tap_tape = tap_stream_is_tap_path(path);
    memset(&s_tap_info, 0, sizeof(s_tap_info));
    s_tap_ticks = s_tap_queued_us = s_tap_final_us = 0;
    s_binary_elapsed_us = s_binary_final_us = 0;
    s_binary_source_bytes=s_binary_final_bytes=0;
    s_binary_waiting = s_binary_motor_low_seen = false;
    memset(&s_info, 0, sizeof(s_info));
    memset(&s_mzf_info, 0, sizeof(s_mzf_info));
    memset(&s_edge_info, 0, sizeof(s_edge_info));
    s_eof = 0;
    s_read_error = 0;
    player_reset_pulse_pending();

    if (s_binary_tape) {
        if (!mzf_stream_open(path, &s_mzf_info)) {
            player_error("SHARP / IMAGE OPEN");
            s_state = PLAYER_STATE_ERROR;
            player_unlock();
            return;
        }
    } else if (s_edge_tape) {
        if (!edge_stream_open(path, &s_edge_info)) {
            if(edge_stream_analysis_cancelled()){player_finish_stop(PLAYER_STATE_STOPPED);s_edge_final_us=0;player_unlock();return;}
            player_error("EDGE FILE OPEN");
            s_state = PLAYER_STATE_ERROR;
            player_unlock();
            return;
        }
    } else if (s_tap_tape) {
        if (!tap_stream_open(path, s_tap_speed, &s_tap_info)) {
            player_error("TAP OPEN");
            s_state = PLAYER_STATE_ERROR;
            player_unlock();
            return;
        }
    } else {
        if (!wav_reader_open(path, &s_info)) {
            player_error("WAV OPEN");
            s_state = PLAYER_STATE_ERROR;
            player_unlock();
            return;
        }

        if (s_info.audio_format != 1U ||
            s_info.num_channels != 1U ||
            s_info.bits_per_sample != 8U) {
            player_error("WAV FORMAT");
            wav_reader_close();
            s_state = PLAYER_STATE_ERROR;
            player_unlock();
            return;
        }
    }

    if(player_is_wav() && !wav_pulse_stream_begin(s_info.sample_rate)) {
        wav_reader_close();
        s_state=PLAYER_STATE_ERROR;
        player_unlock();
        return;
    }
    if(s_binary_tape)program_metadata_publish(&metadata_store,&mzf_stream_details()->metadata);
    else if(s_edge_tape || player_is_wav()){
        metadata_audio=true;
        tape_metadata_begin(file_format_detect_from_name(path));
    }
    s_bytes_queued = 0U;

    raw_pulse_init();
    if(raw_pulse_error()){
        player_error(raw_pulse_error());
        player_finish_stop(PLAYER_STATE_ERROR);player_unlock();return;
    }
    raw_pulse_set_output_enabled(true);
    raw_pulse_clear_underrun();

    printf("player: START sr=%" PRIu32 "%s\n",
           s_tap_tape ? 0U : s_binary_tape ? s_mzf_info.sample_rate :
           s_edge_tape ? s_edge_info.sample_rate :
           s_info.sample_rate,
           s_tap_tape ? " TAP" : s_binary_tape ? " binary" : s_edge_tape ? " edge" : "");

    s_playing = 1;
    s_state = PLAYER_STATE_PLAYING;
    s_pause_reason = PLAYER_PAUSE_NONE;

    /* Prefill bounded source work before starting the physical output. */
    while (raw_pulse_level()<PULSE_PREFILL_PULSES && !s_read_error &&
           (!player_is_wav() || raw_pulse_level()<WAV_PREFILL_MIN_PULSES ||
            wav_pulse_stream_queued_us()<WAV_PREFILL_US)) {
        if(!player_feed_one_pulse_chunk()) break;
    }

    if (s_read_error) {
        player_error("SOURCE READ");
        player_finish_stop(PLAYER_STATE_ERROR);
        player_unlock();
        return;
    }

    if (raw_pulse_level() == 0U) {
        player_error("NO PULSES");
        player_finish_stop(PLAYER_STATE_ERROR);
        player_unlock();
        return;
    }

    if (!raw_pulse_start()) {
        player_error(raw_pulse_error() ? raw_pulse_error():"TX START");
        player_finish_stop(PLAYER_STATE_ERROR);
        player_unlock();
        return;
    }

    if (player_uses_pulse_output() && !raw_pulse_service()) {
        player_error(raw_pulse_error() ? raw_pulse_error():"TX SERVICE");
        player_finish_stop(PLAYER_STATE_ERROR);
        player_unlock();
        return;
    }

    printf("player: TX STARTED state=%s fifo=%lu completed_us=%llu\n",player_state_name(s_state),(unsigned long)raw_pulse_level(),(unsigned long long)raw_pulse_get_completed_us());
    player_unlock();
}

void player_stop_for(player_stop_cause_t cause)
{
    player_lock();
    __atomic_store_n(&last_stop_cause,(unsigned)cause,__ATOMIC_RELEASE);
    printf("player: STOP cause=%s from=%s path=%s\n",player_stop_cause_name(),player_state_name(s_state),s_current_path);
    player_finish_stop(PLAYER_STATE_STOPPED);
    player_unlock();
}
/* Called by the first STOP while a QD transport is open. A second STOP in
 * READY uses player_stop_for(), leaving the image and invalidating its index. */
bool player_stop_to_selector(void) {
    player_lock();
    bool handled=s_binary_tape && mzf_stream_details()->format==FILE_FORMAT_MZQ &&
        (s_state==PLAYER_STATE_PLAYING || s_state==PLAYER_STATE_PAUSED ||
         s_state==PLAYER_STATE_EOF || s_state==PLAYER_STATE_READY);
    if(handled){
        __atomic_store_n(&last_stop_cause,PLAYER_STOP_BUTTON,__ATOMIC_RELEASE);
        if(s_state!=PLAYER_STATE_READY)player_finish_stop(PLAYER_STATE_READY);
    }
    player_unlock();
    return handled;
}
void player_stop(void){player_stop_for(PLAYER_STOP_API);}

void player_pause_user(void)
{
    player_lock();

    if (s_state != PLAYER_STATE_PLAYING) {
        player_unlock();
        return;
    }

    if (s_binary_tape) {
        mzf_stream_pause_ul(true);
        s_binary_pause_remaining=s_binary_waiting ? s_binary_wait_until-esp_timer_get_time() : 0;
        if(s_binary_pause_remaining<0) s_binary_pause_remaining=0;
    }
    raw_pulse_pause();

    s_playing = 0;
    s_state = PLAYER_STATE_PAUSED;
    s_pause_reason = PLAYER_PAUSE_USER;
    player_unlock();
}

void player_resume_user(void)
{
    player_lock();

    if (s_state != PLAYER_STATE_PAUSED ||
        s_pause_reason != PLAYER_PAUSE_USER) {
        player_unlock();
        return;
    }

    if (s_binary_tape) {
        mzf_stream_pause_ul(false);
        if(s_binary_waiting) s_binary_wait_until=esp_timer_get_time()+s_binary_pause_remaining;
    }
    raw_pulse_resume();

    s_playing = 1;
    s_state = PLAYER_STATE_PLAYING;
    s_pause_reason = PLAYER_PAUSE_NONE;
    player_unlock();
}

void player_pause_motor(void)
{
    player_lock();
    if (s_binary_tape) {
        s_binary_motor_low_seen = true;
        if(mzf_stream_is_ultrafast_ready() || mzf_stream_is_ultrafast_active()) { player_unlock();
        return;
        }
    }

    if (s_state != PLAYER_STATE_PLAYING) {
        player_unlock();
        return;
    }

    raw_pulse_pause();

    s_playing = 0;
    s_state = PLAYER_STATE_PAUSED;
    s_pause_reason = PLAYER_PAUSE_MOTOR;
    player_unlock();
}

void player_resume_motor(void)
{
    player_lock();

    if (s_state != PLAYER_STATE_PAUSED ||
        s_pause_reason != PLAYER_PAUSE_MOTOR) {
        player_unlock();
        return;
    }

    if(s_binary_tape && s_binary_waiting) s_binary_wait_until=esp_timer_get_time()+mzf_stream_boundary_delay_us(true);
    raw_pulse_resume();

    s_playing = 1;
    s_state = PLAYER_STATE_PLAYING;
    s_pause_reason = PLAYER_PAUSE_NONE;
    player_unlock();
}

bool player_is_playing(void)
{
    return (s_playing != 0);
}

player_state_t player_get_state(void)
{
    return s_state;
}

player_pause_reason_t player_get_pause_reason(void)
{
    return s_pause_reason;
}

bool player_is_edge_tape(void)
{
    return s_edge_tape;
}

bool player_is_binary_tape(void)
{
    return s_binary_tape;
}

void player_set_ultrafast_enabled(bool enabled)
{
    s_ultrafast_enabled = enabled;
    mzf_stream_set_ultrafast_enabled(enabled);
}

bool player_get_ultrafast_enabled(void)
{
    return s_ultrafast_enabled;
}

bool player_is_ultrafast_active(void)
{
    return mzf_stream_is_ultrafast_active();
}

/* MZT follows the reference continuation contract; QD additionally plays
 * through its validated record index, as requested by the user. */
static int player_continue_record(bool after_ul) {
    const sharp_stream_info_t *current=mzf_stream_details();
    if(!file_format_is_record_container(current->format) || current->record>=current->records) return 0;
    uint16_t next=current->record+1;
    loader_mode_t mode=mzf_stream_get_loader_mode();
    if(s_record_modes && !strcmp(s_record_modes_path,s_current_path) && s_record_modes[next]!=255)
        mode=(loader_mode_t)s_record_modes[next];
    loader_mode_t resolved=sidecar_resolve(s_current_path,next,mode,NULL);
    if(after_ul && current->format==FILE_FORMAT_MZT && (resolved==LOADER_MODE_UL || resolved==LOADER_MODE_UL_MZ800 || resolved==LOADER_MODE_UL_MZ700)) return 0;
    if(!mzf_stream_continue_record(s_current_path,mode,&s_mzf_info)) return -1;
    program_metadata_publish(&metadata_store,&mzf_stream_details()->metadata);
    s_binary_elapsed_us=s_binary_final_us=0;
    s_binary_source_bytes=s_binary_final_bytes=0;
    s_binary_waiting=false;
    s_binary_motor_low_seen=false;
    s_eof=s_read_error=0;
    s_bytes_queued=0;
    player_reset_pulse_pending();
    raw_pulse_init();
    while(raw_pulse_level()<PULSE_PREFILL_PULSES && !s_read_error) {
        if(!player_feed_one_pulse_chunk()) break;
    }
    return !s_read_error && raw_pulse_start() ? 1:-1;
}
static void player_service_locked(void)
{
    if(metadata_audio || s_tap_tape)metadata_confirm();
    if (!s_playing) {
        return;
    }

    if (mzf_stream_is_ultrafast_active()) {
        mzf_stream_service_ultrafast();
        if (mzf_stream_has_error()) {
            const char *why=mzf_stream_error();
            player_error(why[0] ? why:"UL ERROR");
            player_finish_stop(PLAYER_STATE_ERROR);
        } else if (mzf_stream_is_eof()) {
            int next=player_continue_record(true);
            if(next<=0) player_finish_stop(next<0 ? PLAYER_STATE_ERROR:PLAYER_STATE_EOF);
        }
        return;
    }

    if (player_uses_pulse_output()) {
        if (!raw_pulse_service()) {
            player_error(raw_pulse_error() ? raw_pulse_error():"TX SERVICE");
            player_finish_stop(PLAYER_STATE_ERROR);
            return;
        }

        uint64_t wav_read_start=wav_pulse_stream_samples();
        while (!s_read_error && raw_pulse_free() != 0U &&
               (!s_eof || player_pulse_has_pending()) &&
               (!player_is_wav() || wav_pulse_stream_samples()-wav_read_start<65536)) {
            if (!player_feed_one_pulse_chunk()) {
                break;
            }

            if (!raw_pulse_service()) {
                printf("player: PULSE SERVICE ERROR\n");
                player_finish_stop(PLAYER_STATE_ERROR);
                return;
            }
        }

        if (!raw_pulse_service()) {
            printf("player: PULSE SERVICE ERROR\n");
            player_finish_stop(PLAYER_STATE_ERROR);
            return;
        }

        if(metadata_audio || s_tap_tape)metadata_confirm();
        if (s_read_error) {
            printf("player: READ ERROR\n");
            player_finish_stop(PLAYER_STATE_ERROR);
            return;
        }

        if (raw_pulse_is_underrun()) {
            if (s_binary_tape && mzf_stream_is_ultrafast_ready()) {
                raw_pulse_stop();
                raw_pulse_clear_underrun();
                if (!mzf_stream_begin_ultrafast()) {
                    player_error(mzf_stream_error()[0] ? mzf_stream_error():"UL START");
                    player_finish_stop(PLAYER_STATE_ERROR);
                }
            } else if (s_eof) {
                printf("player: END\n");
                player_finish_stop(PLAYER_STATE_EOF);
            } else {
                printf("player: UNDERRUN\n");
                player_finish_stop(PLAYER_STATE_UNDERRUN);
            }
            return;
        }

        if (s_binary_tape && mzf_stream_at_boundary() &&
            !mzf_stream_is_ultrafast_ready() && raw_pulse_level() == 0U) {
            if (!s_binary_waiting) {
                s_binary_elapsed_us += raw_pulse_get_completed_us();
                s_binary_source_bytes += raw_pulse_completed_source_bytes();
                raw_pulse_stop();
                s_binary_wait_until = esp_timer_get_time() +
                    mzf_stream_boundary_delay_us(s_binary_motor_low_seen);
                s_binary_waiting = true;
            }
            if (esp_timer_get_time() < s_binary_wait_until) return;
            if (!mzf_stream_advance()) { player_finish_stop(PLAYER_STATE_ERROR);
            return;
            }
            if (mzf_stream_is_eof()) {
                int next=player_continue_record(false);
                if(next<=0) player_finish_stop(next<0 ? PLAYER_STATE_ERROR:PLAYER_STATE_EOF);
                return;
            }
            s_binary_motor_low_seen = false;
            raw_pulse_init();
            s_binary_waiting = false;
            s_eof = 0;
            while (raw_pulse_level() < PULSE_PREFILL_PULSES && !s_read_error) {
                if (!player_feed_one_pulse_chunk()) break;
            }
            if (s_read_error || !raw_pulse_start()) player_finish_stop(PLAYER_STATE_ERROR);
            return;
        }

        if (s_binary_tape &&
            mzf_stream_is_ultrafast_ready() &&
            raw_pulse_level() == 0U && !raw_pulse_is_running()) {
            raw_pulse_stop();
            if (!mzf_stream_begin_ultrafast()) {
                printf("player: ULTRA START ERROR\n");
                player_finish_stop(PLAYER_STATE_ERROR);
            }
            return;
        }

        if (s_eof && raw_pulse_level() == 0U) {
            printf("player: END\n");
            player_finish_stop(PLAYER_STATE_EOF);
        }
        return;
    }


}

void player_service(void)
{
    player_lock();
    player_service_locked();
    player_unlock();
}

void player_pause(void)
{
    if (!s_playing) {
        return;
    }

    raw_pulse_pause();
    s_playing = 0;
    s_state = PLAYER_STATE_PAUSED;
}

void player_resume(void)
{
    if (s_state != PLAYER_STATE_PAUSED) {
        return;
    }

    raw_pulse_resume();
    s_playing = 1;
    s_state = PLAYER_STATE_PLAYING;
}

const char *player_get_current_path(void)
{
    return s_current_path;
}

uint32_t player_get_sample_rate(void)
{
    if (s_tap_tape) return 0U;
    return s_binary_tape ? s_mzf_info.sample_rate :
           s_edge_tape ? s_edge_info.sample_rate :
           s_info.sample_rate;
}

uint32_t player_get_baud_rate(void)
{
    if (s_tap_tape) return s_tap_info.baud;
    if (!s_binary_tape) {
        return 0U;
    }

    return mzf_stream_get_baud_rate();
}

uint32_t player_get_pulses_per_second(void)
{
    if (!s_edge_tape) {
        return 0U;
    }

    return raw_pulse_get_pulses_per_second();
}

uint32_t player_get_total_seconds(void)
{
    if(s_state==PLAYER_STATE_READY || s_binary_tape) {
        const sharp_stream_info_t *details=mzf_stream_details();
        /* UL timing covers only the loader; deferred timing is not a duration. */
        if(details->ultrafast || details->duration_deferred)return 0;
        return (uint32_t)(details->total_ticks/16000000ULL);
    }
    if (s_tap_tape) return (uint32_t)(s_tap_info.total_ticks / 16000000ULL);
    if (s_binary_tape) {
        return s_mzf_info.total_seconds;
    }

    if (s_edge_tape) {
        return s_edge_info.total_seconds;
    }

    if (s_info.byte_rate == 0U) {
        return 0U;
    }

    return s_info.data_size / s_info.byte_rate;
}

static uint64_t player_binary_elapsed_us(void)
{
    if (s_state != PLAYER_STATE_PLAYING && s_state != PLAYER_STATE_PAUSED) return s_binary_final_us;
    return s_binary_elapsed_us + (s_binary_waiting ? 0 : raw_pulse_get_completed_us());
}

static uint32_t player_binary_completed_bytes(void)
{
    if(s_state!=PLAYER_STATE_PLAYING && s_state!=PLAYER_STATE_PAUSED)return s_binary_final_bytes;
    return s_binary_source_bytes+(s_binary_waiting ? 0:raw_pulse_completed_source_bytes());
}

static uint64_t player_tap_elapsed_us(void)
{
    return (s_state == PLAYER_STATE_PLAYING || s_state == PLAYER_STATE_PAUSED) ?
        raw_pulse_get_completed_us() : s_tap_final_us;
}

uint32_t player_get_position_seconds(void)
{
    if(s_state==PLAYER_STATE_READY)return 0;
    if(player_is_wav()) {
        uint64_t elapsed=s_state==PLAYER_STATE_EOF || s_state==PLAYER_STATE_STOPPED || s_state==PLAYER_STATE_ERROR ? s_wav_final_us:raw_pulse_get_completed_us();
        return (uint32_t)(elapsed/1000000);
    }
    if (s_tap_tape) return (uint32_t)(player_tap_elapsed_us() / 1000000ULL);
    if (s_binary_tape) return (uint32_t)(player_binary_elapsed_us() / 1000000ULL);
    if (s_binary_tape) {
        uint32_t played_samples;

        if (s_state == PLAYER_STATE_EOF) {
            return s_mzf_info.total_seconds;
        }

        played_samples = mzf_stream_get_samples_emitted();
        if (played_samples > s_mzf_info.total_samples) {
            played_samples = s_mzf_info.total_samples;
        }

        return (s_mzf_info.sample_rate == 0U) ? 0U :
            played_samples / s_mzf_info.sample_rate;
    }

    if (s_edge_tape) {
        uint64_t elapsed=(s_state==PLAYER_STATE_PLAYING || s_state==PLAYER_STATE_PAUSED) ? raw_pulse_get_completed_us():s_edge_final_us;
        return (uint32_t)(elapsed/1000000);
    }

    return 0;
}

uint8_t player_get_progress_percent(void)
{
    if(s_state==PLAYER_STATE_READY)return 0;
    if(player_is_wav()) {
        if(s_state==PLAYER_STATE_EOF) return 100;
        uint64_t total=s_info.sample_rate ? ((uint64_t)s_info.data_size*1000000+s_info.sample_rate/2)/s_info.sample_rate:0;
        uint64_t elapsed=s_state==PLAYER_STATE_STOPPED || s_state==PLAYER_STATE_ERROR ? s_wav_final_us:raw_pulse_get_completed_us();
        uint64_t percent=total ? elapsed*100/total:0;
        return percent>=100 ? 99:(uint8_t)percent;
    }
    if (s_tap_tape) {
        if (s_state == PLAYER_STATE_EOF) return 100U;
        uint64_t total_us = (s_tap_info.total_ticks + 8U) / 16U;
        if (!total_us) return 0U;
        uint64_t percent = player_tap_elapsed_us() * 100U / total_us;
        return percent >= 100U ? 99U : (uint8_t)percent;
    }
    if(s_binary_tape && !mzf_stream_details()->ultrafast && (mzf_stream_details()->duration_deferred || mzf_stream_details()->format==FILE_FORMAT_MZF)){
        player_lock();
        bool eof=s_state==PLAYER_STATE_EOF;
        uint32_t total=mzf_stream_transfer_bytes(),done=player_binary_completed_bytes();
        player_unlock();
        if(eof)return 100;
        uint64_t percent=total ? (uint64_t)done*100/total:0;
        return percent>=100 ? 99:(uint8_t)percent;
    }
    if (s_binary_tape && !mzf_stream_details()->ultrafast) {
        if (s_state == PLAYER_STATE_EOF) return 100U;
        uint64_t total=(mzf_stream_details()->total_ticks+8)/16;
        uint64_t percent=total ? player_binary_elapsed_us()*100U/total : 0;
        return percent>=100U ? 99U : (uint8_t)percent;
    }
    if (s_binary_tape) {
        if (mzf_stream_is_ultrafast_active() ||
            mzf_stream_get_ultrafast_total_bytes() != 0U) {
            uint32_t total = mzf_stream_get_ultrafast_total_bytes();
            uint32_t sent = mzf_stream_get_ultrafast_sent_bytes();

            if (s_state == PLAYER_STATE_EOF) {
                return 100U;
            }
            if (total == 0U) {
                return 0U;
            }
            if (sent >= total) {
                return 100U;
            }
            return (uint8_t)((sent * 100U) / total);
        }

        if (s_mzf_info.total_samples == 0U) {
            return 0U;
        }

        if (s_state == PLAYER_STATE_EOF) {
            return 100U;
        }

        uint32_t played_samples = mzf_stream_get_samples_emitted();
        if (played_samples > s_mzf_info.total_samples) {
            played_samples = s_mzf_info.total_samples;
        }

        return (uint8_t)((played_samples * 100U) /
                         s_mzf_info.total_samples);
    }

    if (s_edge_tape) {
        uint32_t total = edge_stream_get_total_bytes();
        uint32_t consumed = raw_pulse_completed_source_bytes();

        if (s_state == PLAYER_STATE_EOF) {
            return 100U;
        }

        if (total == 0U) {
            return 0U;
        }

        if (consumed >= total) {
            return 100U;
        }

        return (uint8_t)((consumed * 100U) / total);
    }

    return 0;
}

bool player_is_tap(void) { return s_tap_tape;
}
bool player_set_tap_speed(tap_speed_t speed)
{
    if (!tap_stream_speed_baud(speed)) return false;
    player_lock();
    s_tap_speed=speed;
    player_unlock();
    return true;
}
tap_speed_t player_get_tap_speed(void) { return s_tap_speed;
}

bool player_set_loader_mode(loader_mode_t mode) {
    player_lock();
    bool ok=mzf_stream_set_loader_mode(mode);
    if(ok) mzf_stream_clear_selection();
    if(ok) s_ultrafast_enabled=mzf_stream_get_ultrafast_enabled();
    player_unlock();
    return ok;
}
loader_mode_t player_get_loader_mode(void) { return mzf_stream_get_loader_mode();
}
const sharp_stream_info_t *player_sharp_details(void) { return mzf_stream_details();
}
bool player_prepare_record(const char *path,uint16_t record) {
    player_lock();
    if(s_state==PLAYER_STATE_PLAYING || s_state==PLAYER_STATE_PAUSED) { player_unlock();
    return false;
    }
    if (!path || !record || !s_record_modes || strlen(path)>=sizeof(s_record_modes_path)) { player_unlock();
    return false;
    }
    if(strcmp(path,s_record_modes_path)) {
        memset(s_record_modes,255,65536U);
        snprintf(s_record_modes_path,sizeof(s_record_modes_path),"%s",path);
    }
    loader_mode_t mode=s_record_modes[record]==255 ? mzf_stream_get_loader_mode() : (loader_mode_t)s_record_modes[record];
    __atomic_store_n(&last_error,NULL,__ATOMIC_RELEASE);
    int64_t started=esp_timer_get_time();
    bool ok=mzf_stream_select_mode(path,record,mode);
    printf("player: select path=%s record=%u mode=%u result=%s elapsed_ms=%lu\n",path,record,(unsigned)mode,ok ? "READY":"ERROR",(unsigned long)((esp_timer_get_time()-started)/1000));
    if(ok)printf("player: source_reused=%u duration_cached=%u duration_deferred=%u records=%u\n",(unsigned)mzf_stream_details()->source_reused,(unsigned)mzf_stream_details()->duration_cached,(unsigned)mzf_stream_details()->duration_deferred,mzf_stream_details()->records);
    if(!ok){player_error(sharp_stream_analysis_cancelled() ? "CANCELLED":"IMAGE FORMAT / IO");s_state=PLAYER_STATE_ERROR;}
    if(ok) {
        snprintf(s_current_path,sizeof(s_current_path),"%s",path);
        program_metadata_publish(&metadata_store,&mzf_stream_details()->metadata);
        s_binary_tape=true;
        s_edge_tape=s_tap_tape=false;
        s_playing=0;
        s_state=PLAYER_STATE_READY;
    }
    player_unlock();
    return ok;
}

bool player_set_record_mode(loader_mode_t mode) {
    if((unsigned)mode>=LOADER_MODE_COUNT || s_state!=PLAYER_STATE_READY || !s_record_modes) return false;
    uint16_t record=mzf_stream_details()->record;
    char path[1024];
    snprintf(path,sizeof(path),"%s",s_current_path);
    uint8_t previous=s_record_modes[record];
    s_record_modes[record]=(uint8_t)mode;
    if(player_prepare_record(path,record)) return true;
    s_record_modes[record]=previous;
    return false;
}

bool player_monitor_active(void) {return player_get_state()==PLAYER_STATE_PLAYING && !player_is_ultrafast_active() && raw_pulse_monitor_active();
}
