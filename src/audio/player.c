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

#include "audio/wav_reader.h"
#include "audio/raw_out.h"
#include "audio/raw_pulse.h"
#include "audio/mzf_stream.h"
#include "audio/edge_stream.h"
#include "audio/tap_stream.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#define WAV_CHUNK 256U
#define PULSE_CHUNK 256U
#define PULSE_PREFILL_PULSES (PULSE_CHUNK * 12U)
#define PLAYER_PREFILL_BYTES 8192U
#define PLAYER_TASK_STACK_WORDS 8192U
#define PLAYER_TASK_PRIORITY 5U
#define PLAYER_TASK_CORE 0
#define PLAYER_TASK_ACTIVE_DELAY_MS 1U
#define PLAYER_TASK_IDLE_DELAY_MS 5U

/* Threshold plus hysteresis for 8-bit PCM WAV -> one-bit GPIO. */
#define WAV_LOW_THR   0x40U
#define WAV_HIGH_THR  0xC0U

static uint8_t wav_buf[WAV_CHUNK];
static uint8_t wav_gpio_buf[WAV_CHUNK];
static raw_pulse_t pulse_buf[PULSE_CHUNK];

static wav_info_t s_info;
static mzf_stream_info_t s_mzf_info;
static edge_stream_info_t s_edge_info;
static bool s_binary_tape = false;
static bool s_edge_tape = false;
static bool s_tap_tape = false;
static tap_speed_t s_tap_speed = TAP_SPEED_1400;
static tap_stream_info_t s_tap_info;
static uint64_t s_tap_ticks, s_tap_queued_us, s_tap_final_us;
static uint64_t s_binary_elapsed_us, s_binary_final_us;
/* 64 KiB UI metadata, allocated once in PSRAM at init, never used by ISR/DMA. */
static uint8_t *s_record_modes;
static char s_record_modes_path[256];
static bool s_binary_waiting, s_binary_motor_low_seen;
static int64_t s_binary_wait_until, s_binary_pause_remaining;
static uint64_t player_binary_elapsed_us(void);
static bool s_ultrafast_enabled = false;
static int s_playing = 0;
static int s_eof = 0;
static int s_read_error = 0;
static uint8_t s_hyst = 0;
static player_state_t s_state = PLAYER_STATE_IDLE;
static player_pause_reason_t s_pause_reason = PLAYER_PAUSE_NONE;
static SemaphoreHandle_t s_player_mutex = NULL;
static TaskHandle_t s_player_task = NULL;

/*
 * WAV staging state:
 *
 * A read block is retained until raw_out accepted every byte.  The old code
 * read a block and discarded its unwritten suffix if raw_out_write() ever
 * returned a partial count.  Normally the FIFO is large enough that this is
 * rare, but EOF must not depend on that assumption.
 */
static size_t s_wav_pending_pos = 0;
static size_t s_wav_pending_len = 0;

/*
 * Pulse staging state.  A stream pulse is never discarded after it was read
 * from MZF/L16/LEP.  raw_pulse_write() may accept less than a whole block;
 * the remaining pulses stay here until the GPTimer FIFO accepts them.
 */
static size_t s_pulse_pending_pos = 0;
static size_t s_pulse_pending_len = 0;

/* Playback information for UI. */
static uint32_t s_bytes_queued = 0;
static char s_current_path[256];

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

static void player_task_loop(void *arg)
{
    (void)arg;

    for (;;) {
        bool active;
        TickType_t delay_ticks;

        player_lock();
        player_service_locked();
        active = (s_playing != 0);
        player_unlock();

        delay_ticks = pdMS_TO_TICKS(active ?
            PLAYER_TASK_ACTIVE_DELAY_MS : PLAYER_TASK_IDLE_DELAY_MS);
        if (delay_ticks == 0) {
            delay_ticks = 1;
        }
        vTaskDelay(delay_ticks);
    }
}

static bool player_uses_pulse_output(void)
{
    return s_binary_tape || s_edge_tape || s_tap_tape;
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
           (strcasecmp(ext, ".mzt") == 0) ||
           (strcasecmp(ext, ".m12") == 0);
}

static bool path_is_edge_tape(const char *path)
{
    return edge_stream_is_edge_path(path);
}

static void player_reset_wav_pending(void)
{
    s_wav_pending_pos = 0;
    s_wav_pending_len = 0;
}

static bool player_wav_has_pending(void)
{
    return s_wav_pending_pos < s_wav_pending_len;
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

    if (edge_stream_has_error()) {
        s_read_error = 1;
    } else if (edge_stream_is_eof() ||
               edge_stream_get_consumed_bytes() >= edge_stream_get_total_bytes()) {
        s_eof = 1;
    } else {
        s_read_error = 1;
    }
}

/* Convert 8-bit WAV PCM to one-bit GPIO using threshold + hysteresis. */
static void convert_block(const uint8_t *src, uint8_t *dst, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t v = src[i];

        if (s_hyst == 0U) {
            if (v >= WAV_HIGH_THR) {
                s_hyst = 1U;
            }
        } else {
            if (v <= WAV_LOW_THR) {
                s_hyst = 0U;
            }
        }

        dst[i] = s_hyst ? 0xFFU : 0x00U;
    }
}

/*
 * Mark the stream finished only after every last WAV byte is queued in the
 * GPTimer FIFO.  raw_timer_finish() then lets raw_timer hold the final
 * sample for its full duration and emit the final transition to idle LOW.
 */
static void player_finish_wav_source_if_drained(void)
{
    if (s_eof || s_read_error || player_wav_has_pending()) {
        return;
    }

    if (wav_reader_has_error()) {
        s_read_error = 1;
        return;
    }

    if (wav_reader_is_eof()) {
        s_eof = 1;
        raw_out_finish();
    }
}

static void player_finish_stop(player_state_t final_state)
{
    if (s_tap_tape) s_tap_final_us = raw_pulse_get_completed_us();
    if (s_binary_tape) s_binary_final_us = player_binary_elapsed_us();
    if (player_uses_pulse_output()) {
        raw_pulse_stop();
    } else {
        raw_out_stop();
    }

    wav_reader_close();
    mzf_stream_close();
    edge_stream_close();
    tap_stream_close();

    s_playing = 0;
    s_eof = 0;
    s_read_error = 0;
    s_hyst = 0;
    player_reset_wav_pending();
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

/*
 * Feed sample-output FIFO.  The output buffer is never advanced past a byte
 * that raw_out did not accept, including the final non-full WAV block.
 */
static bool player_feed_one_wav_chunk(void)
{
    size_t written;

    if (s_eof || s_read_error) {
        return false;
    }

    if (!player_wav_has_pending()) {
        size_t free_bytes = raw_out_free();
        size_t request;
        size_t n;

        if (free_bytes == 0U) {
            return false;
        }

        request = (free_bytes < WAV_CHUNK) ? free_bytes : WAV_CHUNK;
        n = wav_reader_read(wav_buf, request);

        if (n == 0U) {
            if (wav_reader_has_error()) {
                s_read_error = 1;
            } else if (wav_reader_is_eof()) {
                s_eof = 1;
                raw_out_finish();
            } else {
                s_read_error = 1;
            }
            return false;
        }

        convert_block(wav_buf, wav_gpio_buf, n);
        s_wav_pending_pos = 0U;
        s_wav_pending_len = n;
    }

    written = raw_out_write(&wav_gpio_buf[s_wav_pending_pos],
                            s_wav_pending_len - s_wav_pending_pos);

    if (written > 0U) {
        s_wav_pending_pos += written;
        s_bytes_queued += (uint32_t)written;
    }

    if (!player_wav_has_pending()) {
        player_reset_wav_pending();
        player_finish_wav_source_if_drained();
    }

    return written > 0U;
}

/* Kept as the common sample-output feeder entry point. */
static bool player_feed_one_chunk(void)
{
    if (s_binary_tape || s_edge_tape) {
        return false;
    }

    return player_feed_one_wav_chunk();
}

static bool player_fill_pulse_pending(void)
{
    size_t count = 0U;

    if (player_pulse_has_pending() || s_eof || s_read_error) {
        return false;
    }

    while (count < PULSE_CHUNK) {
        uint8_t level = 0U;
        uint32_t duration_us = 0U;
        bool count_pulse = true;
        bool ok;

        if (s_binary_tape) {
            ok = mzf_stream_next_pulse(&level, &duration_us);
        } else if (s_tap_tape) {
            uint32_t ticks;
            ok = tap_stream_next_pulse(&level, &ticks, &count_pulse);
            if (ok) {
                /* Quantize absolute transition times, not each half pulse:
                 * total error remains <= 0.5 us at this legacy 1 MHz HAL.
                 * The source retains 16 MHz ticks for a future native HAL. */
                s_tap_ticks += ticks;
                uint64_t end_us = (s_tap_ticks + 8U) / 16U;
                duration_us = (uint32_t)(end_us - s_tap_queued_us);
                s_tap_queued_us = end_us;
            }
        } else {
            ok = edge_stream_next_pulse(&level, &duration_us, &count_pulse);
        }

        if (!ok) {
            player_mark_pulse_source_end();
            break;
        }

        pulse_buf[count].level = level ? 1U : 0U;
        pulse_buf[count].count_pulse = count_pulse ? 1U : 0U;
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
        written = raw_pulse_write(&pulse_buf[s_pulse_pending_pos],
                                  s_pulse_pending_len - s_pulse_pending_pos);
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
    s_record_modes_path[0]=0;
    if (s_player_mutex == NULL) {
        s_player_mutex = xSemaphoreCreateMutex();
    }

    mzf_stream_set_ultrafast_enabled(false);

    s_playing = 0;
    s_eof = 0;
    s_read_error = 0;
    s_hyst = 0U;
    s_state = PLAYER_STATE_IDLE;
    s_pause_reason = PLAYER_PAUSE_NONE;
    player_reset_wav_pending();
    player_reset_pulse_pending();
    s_bytes_queued = 0U;
    s_binary_tape = false;
    s_edge_tape = false;
    s_tap_tape = false;
    memset(&s_tap_info, 0, sizeof(s_tap_info));
    s_tap_ticks = s_tap_queued_us = s_tap_final_us = 0;
    s_binary_elapsed_us = s_binary_final_us = 0;
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
    player_lock();

    if (s_state == PLAYER_STATE_PLAYING || s_state == PLAYER_STATE_PAUSED) {
        player_finish_stop(PLAYER_STATE_STOPPED);
    }

    s_binary_tape = path_is_binary_tape(path);
    s_edge_tape = path_is_edge_tape(path);
    s_tap_tape = tap_stream_is_tap_path(path);
    memset(&s_tap_info, 0, sizeof(s_tap_info));
    s_tap_ticks = s_tap_queued_us = s_tap_final_us = 0;
    s_binary_elapsed_us = s_binary_final_us = 0;
    s_binary_waiting = s_binary_motor_low_seen = false;
    memset(&s_info, 0, sizeof(s_info));
    memset(&s_mzf_info, 0, sizeof(s_mzf_info));
    memset(&s_edge_info, 0, sizeof(s_edge_info));
    s_eof = 0;
    s_read_error = 0;
    s_hyst = 0U;
    player_reset_wav_pending();
    player_reset_pulse_pending();

    if (s_binary_tape) {
        if (!mzf_stream_open(path, &s_mzf_info)) {
            printf("player: binary tape open fail\n");
            s_state = PLAYER_STATE_ERROR;
            player_unlock();
            return;
        }
    } else if (s_edge_tape) {
        if (!edge_stream_open(path, &s_edge_info)) {
            printf("player: edge tape open fail\n");
            s_state = PLAYER_STATE_ERROR;
            player_unlock();
            return;
        }
    } else if (s_tap_tape) {
        if (!tap_stream_open(path, s_tap_speed, &s_tap_info)) {
            s_state = PLAYER_STATE_ERROR;
            player_unlock();
            return;
        }
    } else {
        if (!wav_reader_open(path, &s_info)) {
            printf("player: open fail\n");
            s_state = PLAYER_STATE_ERROR;
            player_unlock();
            return;
        }

        if (s_info.audio_format != 1U ||
            s_info.num_channels != 1U ||
            s_info.bits_per_sample != 8U) {
            printf("player: unsupported format\n");
            wav_reader_close();
            s_state = PLAYER_STATE_ERROR;
            player_unlock();
            return;
        }
    }

    snprintf(s_current_path, sizeof(s_current_path), "%s", path ? path : "");
    s_bytes_queued = 0U;

    if (player_uses_pulse_output()) {
        raw_out_stop();
        raw_out_set_sample_output_enabled(false);
        raw_pulse_set_rmt_enabled(false);
        raw_pulse_init();
        raw_pulse_set_output_enabled(true);
        raw_pulse_clear_underrun();
    } else {
        raw_pulse_set_rmt_enabled(false);
        raw_pulse_set_output_enabled(false);
        raw_out_set_sample_output_enabled(true);
        raw_out_init((int)s_info.sample_rate);
        raw_out_stop();
        raw_out_clear_underrun();
    }

    printf("player: START sr=%" PRIu32 "%s\n",
           s_tap_tape ? 0U : s_binary_tape ? s_mzf_info.sample_rate :
           s_edge_tape ? s_edge_info.sample_rate :
           s_info.sample_rate,
           s_tap_tape ? " TAP" : s_binary_tape ? " binary" : s_edge_tape ? " edge" : "");

    s_playing = 1;
    s_state = PLAYER_STATE_PLAYING;
    s_pause_reason = PLAYER_PAUSE_NONE;

    /* Prefill before starting the physical output stream. */
    if (player_uses_pulse_output()) {
        while (raw_pulse_level() < PULSE_PREFILL_PULSES && !s_read_error) {
            if (!player_feed_one_pulse_chunk()) {
                break;
            }
        }
    } else {
        while (raw_out_level() < PLAYER_PREFILL_BYTES && !s_eof) {
            if (!player_feed_one_chunk()) {
                break;
            }
        }
    }

    if (s_read_error) {
        printf("player: READ ERROR\n");
        player_finish_stop(PLAYER_STATE_ERROR);
        player_unlock();
        return;
    }

    if ((player_uses_pulse_output() && raw_pulse_level() == 0U) ||
        (!player_uses_pulse_output() && raw_out_level() == 0U)) {
        printf("player: no data\n");
        player_finish_stop(PLAYER_STATE_ERROR);
        player_unlock();
        return;
    }

    if (player_uses_pulse_output() && !raw_pulse_start()) {
        printf("player: pulse start fail\n");
        player_finish_stop(PLAYER_STATE_ERROR);
        player_unlock();
        return;
    }

    if (player_uses_pulse_output() && !raw_pulse_service()) {
        printf("player: pulse service fail\n");
        player_finish_stop(PLAYER_STATE_ERROR);
        player_unlock();
        return;
    }

    if (!player_uses_pulse_output() && !raw_out_start()) {
        printf("player: raw start fail\n");
        player_finish_stop(PLAYER_STATE_ERROR);
        player_unlock();
        return;
    }

    player_unlock();
}

void player_stop(void)
{
    player_lock();
    player_finish_stop(PLAYER_STATE_STOPPED);
    player_unlock();
}

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
    if (player_uses_pulse_output()) {
        raw_pulse_pause();
    } else {
        raw_out_pause();
    }

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
    if (player_uses_pulse_output()) {
        raw_pulse_resume();
    } else {
        raw_out_resume();
    }

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
        if(mzf_stream_is_ultrafast_ready() || mzf_stream_is_ultrafast_active()) { player_unlock(); return; }
    }

    if (s_state != PLAYER_STATE_PLAYING) {
        player_unlock();
        return;
    }

    if (player_uses_pulse_output()) {
        raw_pulse_pause();
    } else {
        raw_out_pause();
    }

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
    if (player_uses_pulse_output()) {
        raw_pulse_resume();
    } else {
        raw_out_resume();
    }

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

static void player_service_locked(void)
{
    if (!s_playing) {
        return;
    }

    if (mzf_stream_is_ultrafast_active()) {
        mzf_stream_service_ultrafast();
        if (mzf_stream_has_error()) {
            printf("player: ULTRA ERROR\n");
            player_finish_stop(PLAYER_STATE_ERROR);
        } else if (mzf_stream_is_eof()) {
            printf("player: ULTRA END\n");
            player_finish_stop(PLAYER_STATE_EOF);
        }
        return;
    }

    if (player_uses_pulse_output()) {
        if (!raw_pulse_service()) {
            printf("player: PULSE SERVICE ERROR\n");
            player_finish_stop(PLAYER_STATE_ERROR);
            return;
        }

        while (!s_read_error && raw_pulse_free() != 0U &&
               (!s_eof || player_pulse_has_pending())) {
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
                    printf("player: ULTRA START ERROR\n");
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
                raw_pulse_stop();
                s_binary_wait_until = esp_timer_get_time() +
                    mzf_stream_boundary_delay_us(s_binary_motor_low_seen);
                s_binary_waiting = true;
            }
            if (esp_timer_get_time() < s_binary_wait_until) return;
            if (!mzf_stream_advance()) { player_finish_stop(PLAYER_STATE_ERROR); return; }
            if (mzf_stream_is_eof()) { player_finish_stop(PLAYER_STATE_EOF); return; }
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
            raw_pulse_level() == 0U) {
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

    /*
     * Feed until the FIFO is full or the source is completely handed to the
     * backend.  Unlike the old >= WAV_CHUNK condition, this also flushes a
     * retained partial block when only a few FIFO positions are free.
     */
    while (!s_eof && !s_read_error && raw_out_free() != 0U) {
        if (!player_feed_one_chunk()) {
            break;
        }
    }

    if (s_read_error) {
        printf("player: READ ERROR\n");
        player_finish_stop(PLAYER_STATE_ERROR);
        return;
    }

    if (raw_out_is_underrun()) {
        if (s_eof) {
            printf("player: END\n");
            player_finish_stop(PLAYER_STATE_EOF);
        } else {
            printf("player: UNDERRUN\n");
            player_finish_stop(PLAYER_STATE_UNDERRUN);
        }
        return;
    }

    /*
     * raw_timer_level() includes a final sample held for its last period, so
     * this cannot turn the line LOW before the terminal waveform is complete.
     */
    if (s_eof && raw_out_level() == 0U) {
        printf("player: END\n");
        player_finish_stop(PLAYER_STATE_EOF);
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

    if (player_uses_pulse_output()) {
        raw_pulse_pause();
    } else {
        raw_out_pause();
    }
    s_playing = 0;
    s_state = PLAYER_STATE_PAUSED;
}

void player_resume(void)
{
    if (s_state != PLAYER_STATE_PAUSED) {
        return;
    }

    if (player_uses_pulse_output()) {
        raw_pulse_resume();
    } else {
        raw_out_resume();
    }
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
    if (s_binary_tape) return (uint32_t)(mzf_stream_details()->total_ticks / 16000000ULL);
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

static uint64_t player_tap_elapsed_us(void)
{
    return (s_state == PLAYER_STATE_PLAYING || s_state == PLAYER_STATE_PAUSED) ?
        raw_pulse_get_completed_us() : s_tap_final_us;
}

uint32_t player_get_position_seconds(void)
{
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
        size_t level = player_uses_pulse_output() ?
            raw_pulse_level() : raw_out_level();
        uint32_t played_samples = 0U;

        if (s_state == PLAYER_STATE_EOF) {
            return s_edge_info.total_seconds;
        }

        if (s_bytes_queued > (uint32_t)level) {
            played_samples = s_bytes_queued - (uint32_t)level;
        }

        if (played_samples > s_edge_info.total_samples) {
            played_samples = s_edge_info.total_samples;
        }

        return (s_edge_info.sample_rate == 0U) ? 0U :
            played_samples / s_edge_info.sample_rate;
    }

    if (s_info.byte_rate == 0U) {
        return 0U;
    }

    if (s_state == PLAYER_STATE_EOF) {
        return s_info.data_size / s_info.byte_rate;
    }

    size_t level = raw_out_level();
    uint32_t played_bytes = 0U;

    if (s_bytes_queued > (uint32_t)level) {
        played_bytes = s_bytes_queued - (uint32_t)level;
    }

    if (played_bytes > s_info.data_size) {
        played_bytes = s_info.data_size;
    }

    return played_bytes / s_info.byte_rate;
}

uint8_t player_get_progress_percent(void)
{
    if (s_tap_tape) {
        if (s_state == PLAYER_STATE_EOF) return 100U;
        uint64_t total_us = (s_tap_info.total_ticks + 8U) / 16U;
        if (!total_us) return 0U;
        uint64_t percent = player_tap_elapsed_us() * 100U / total_us;
        return percent >= 100U ? 99U : (uint8_t)percent;
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
        uint32_t consumed = edge_stream_get_consumed_bytes();

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

    if (s_info.data_size == 0U) {
        return 0U;
    }

    if (s_state == PLAYER_STATE_EOF) {
        return 100U;
    }

    size_t level = raw_out_level();
    uint32_t played_bytes = 0U;

    if (s_bytes_queued > (uint32_t)level) {
        played_bytes = s_bytes_queued - (uint32_t)level;
    }

    if (played_bytes > s_info.data_size) {
        played_bytes = s_info.data_size;
    }

    return (uint8_t)((played_bytes * 100U) / s_info.data_size);
}

bool player_is_tap(void) { return s_tap_tape; }
bool player_set_tap_speed(tap_speed_t speed)
{
    if (!tap_stream_speed_baud(speed)) return false;
    player_lock(); s_tap_speed=speed; player_unlock(); return true;
}
tap_speed_t player_get_tap_speed(void) { return s_tap_speed; }

bool player_set_loader_mode(loader_mode_t mode) {
    player_lock(); bool ok=mzf_stream_set_loader_mode(mode);
    if(ok) mzf_stream_clear_selection();
    if(ok) s_ultrafast_enabled=mzf_stream_get_ultrafast_enabled();
    player_unlock(); return ok;
}
loader_mode_t player_get_loader_mode(void) { return mzf_stream_get_loader_mode(); }
const sharp_stream_info_t *player_sharp_details(void) { return mzf_stream_details(); }
bool player_prepare_record(const char *path,uint16_t record) {
    player_lock();
    if(s_state==PLAYER_STATE_PLAYING || s_state==PLAYER_STATE_PAUSED) { player_unlock(); return false; }
    if (!path || !record || !s_record_modes || strlen(path)>=sizeof(s_record_modes_path)) { player_unlock(); return false; }
    if(strcmp(path,s_record_modes_path)) {
        memset(s_record_modes,255,65536U); snprintf(s_record_modes_path,sizeof(s_record_modes_path),"%s",path);
    }
    loader_mode_t mode=s_record_modes[record]==255 ? mzf_stream_get_loader_mode() : (loader_mode_t)s_record_modes[record];
    bool ok=mzf_stream_select_mode(path,record,mode);
    if(ok) {
        snprintf(s_current_path,sizeof(s_current_path),"%s",path);
        s_binary_tape=true; s_edge_tape=s_tap_tape=false; s_playing=0; s_state=PLAYER_STATE_READY;
    }
    player_unlock(); return ok;
}

bool player_set_record_mode(loader_mode_t mode) {
    if((unsigned)mode>=LOADER_MODE_COUNT || s_state!=PLAYER_STATE_READY || !s_record_modes) return false;
    uint16_t record=mzf_stream_details()->record;
    char path[256]; snprintf(path,sizeof(path),"%s",s_current_path);
    uint8_t previous=s_record_modes[record]; s_record_modes[record]=(uint8_t)mode;
    if(player_prepare_record(path,record)) return true;
    s_record_modes[record]=previous; return false;
}
