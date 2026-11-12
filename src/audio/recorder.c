#include "audio/recorder.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio/edge_capture.h"
#include "audio/edge_writer.h"
#include "audio/wav_writer.h"

#define RECORDER_DEFAULT_SAMPLE_RATE             44100U
#define RECORDER_LEP_SAMPLE_RATE                 20000U
#define RECORDER_L16_SAMPLE_RATE                 62500U
#define RECORDER_CAPTURE_UNIT_US                 1U
#define RECORDER_EDGE_SERVICE_BUDGET_EVENTS      1024U
#define RECORDER_WAV_SERVICE_BUDGET_EVENTS       2048U
#define RECORDER_WAV_RENDER_BUFFER_SIZE          4096U
#define RECORDER_SIGNAL_TIMEOUT_MS               5000U
#ifndef RECORDINGS_DIR
#define RECORDINGS_DIR                           "/sd/RECORDINGS"
#endif
#define RECORDINGS_MAX_INDEX                     9999U

static recorder_state_t s_state = RECORDER_STATE_IDLE;
static recorder_format_t s_format = RECORDER_FORMAT_WAV;
static uint32_t s_sample_rate = RECORDER_DEFAULT_SAMPLE_RATE;
static char s_path[256];

static TickType_t s_started_tick;
static uint32_t s_elapsed_before_pause_ms;
static uint32_t s_elapsed_final_ms;

static bool s_auto_signal_enabled;
static bool s_waiting_signal;
static bool s_signal_detected;
static TickType_t s_last_pulse_tick;

/* WAV is rendered from measured edge intervals outside every ISR. */
static uint8_t s_wav_buffer[RECORDER_WAV_RENDER_BUFFER_SIZE];
static size_t s_wav_buffer_len;
static uint32_t s_wav_render_rate;
static uint64_t s_wav_render_remainder;

static uint32_t recorder_edge_unit_us(void)
{
    return s_format == RECORDER_FORMAT_L16 ? 16U : 50U;
}

static void recorder_reset_signal_session(void)
{
    s_waiting_signal = false;
    s_signal_detected = false;
    s_last_pulse_tick = 0;
}

static void recorder_reset_wav_render(uint32_t sample_rate)
{
    s_wav_buffer_len = 0U;
    s_wav_render_rate = sample_rate == 0U ? RECORDER_DEFAULT_SAMPLE_RATE : sample_rate;
    s_wav_render_remainder = 500000ULL;
}

static void recorder_mark_signal_detected(void)
{
    if (!s_signal_detected) {
        s_signal_detected = true;
        s_waiting_signal = false;
        s_started_tick = xTaskGetTickCount();
        s_elapsed_before_pause_ms = 0U;
        s_elapsed_final_ms = 0U;
    }
    s_last_pulse_tick = xTaskGetTickCount();
}

static uint32_t recorder_elapsed_ms_now(void)
{
    if (s_waiting_signal && !s_signal_detected) {
        return 0U;
    }

    if (s_state == RECORDER_STATE_RECORDING) {
        const TickType_t now = xTaskGetTickCount();
        return s_elapsed_before_pause_ms +
            (uint32_t)((now - s_started_tick) * portTICK_PERIOD_MS);
    }

    if (s_state == RECORDER_STATE_FINISHED) {
        return s_elapsed_final_ms;
    }

    return s_elapsed_before_pause_ms;
}

static void recorder_add_elapsed_if_running(void)
{
    if (s_state == RECORDER_STATE_RECORDING &&
        !(s_waiting_signal && !s_signal_detected)) {
        const TickType_t now = xTaskGetTickCount();
        s_elapsed_before_pause_ms +=
            (uint32_t)((now - s_started_tick) * portTICK_PERIOD_MS);
    }
}

static const char *recorder_path_pattern(void)
{
    switch (s_format) {
        case RECORDER_FORMAT_LEP: return RECORDINGS_DIR "/REC%04lu.LEP";
        case RECORDER_FORMAT_L16: return RECORDINGS_DIR "/REC%04lu.L16";
        case RECORDER_FORMAT_WAV:
        default:                  return RECORDINGS_DIR "/REC%04lu.WAV";
    }
}

static uint32_t recorder_parse_rec_number(const char *name)
{
    uint32_t value = 0U;
    size_t pos = 3U;

    if (name == NULL ||
        toupper((unsigned char)name[0]) != 'R' ||
        toupper((unsigned char)name[1]) != 'E' ||
        toupper((unsigned char)name[2]) != 'C' ||
        !isdigit((unsigned char)name[3])) {
        return 0U;
    }

    while (isdigit((unsigned char)name[pos])) {
        const uint32_t digit = (uint32_t)(name[pos] - '0');
        if (value > (UINT32_MAX - digit) / 10U) {
            return UINT32_MAX;
        }
        value = value * 10U + digit;
        ++pos;
    }

    return value;
}

static uint32_t recorder_next_sequence(void)
{
    DIR *dir = opendir(RECORDINGS_DIR);
    struct dirent *ent;
    uint32_t max_seen = 0U;

    if (dir == NULL) {
        return 1U;
    }

    while ((ent = readdir(dir)) != NULL) {
        const uint32_t number = recorder_parse_rec_number(ent->d_name);
        if (number > max_seen) {
            max_seen = number;
        }
    }
    closedir(dir);

    return max_seen >= RECORDINGS_MAX_INDEX ?
        RECORDINGS_MAX_INDEX : max_seen + 1U;
}

static void recorder_generate_target_path(const char *current_dir)
{
    (void)current_dir;
    snprintf(s_path, sizeof(s_path), recorder_path_pattern(),
             (unsigned long)recorder_next_sequence());
}

static bool recorder_flush_wav_buffer(void)
{
    if (s_wav_buffer_len == 0U) {
        return true;
    }

    if (wav_writer_write(s_wav_buffer, s_wav_buffer_len) != s_wav_buffer_len) {
        return false;
    }

    s_wav_buffer_len = 0U;
    return true;
}

static bool recorder_append_wav_repeat(uint8_t sample, uint32_t count)
{
    while (count != 0U) {
        size_t free_bytes = sizeof(s_wav_buffer) - s_wav_buffer_len;
        size_t chunk;

        if (free_bytes == 0U) {
            if (!recorder_flush_wav_buffer()) {
                return false;
            }
            free_bytes = sizeof(s_wav_buffer);
        }

        chunk = count < free_bytes ? (size_t)count : free_bytes;
        memset(&s_wav_buffer[s_wav_buffer_len], sample, chunk);
        s_wav_buffer_len += chunk;
        count -= (uint32_t)chunk;
    }
    return true;
}

static bool recorder_render_wav_interval(uint8_t level, uint32_t duration_us)
{
    uint64_t scaled;
    uint32_t samples;

    if (duration_us == 0U) {
        return true;
    }

    scaled = (uint64_t)duration_us * (uint64_t)s_wav_render_rate +
             s_wav_render_remainder;
    samples = (uint32_t)(scaled / 1000000ULL);
    s_wav_render_remainder = scaled % 1000000ULL;

    return recorder_append_wav_repeat(level ? 235U : 20U, samples);
}

static bool recorder_quantize_interval(uint32_t duration_us, uint32_t *units_out)
{
    const uint32_t unit_us = recorder_edge_unit_us();
    uint32_t units;

    if (duration_us == 0U || units_out == NULL) {
        return false;
    }

    /* LEP/L16 store physical run widths. Round each interval independently:
     * carrying error into the next level changes HIGH/LOW duty cycles.
     * Widen before adding half a unit so UINT32_MAX durations cannot wrap.
     * The WAV remainder above instead tracks one absolute sample-clock phase;
     * it must survive edges to avoid sample-rate drift. */
    units = (uint32_t)(((uint64_t)duration_us + unit_us / 2U) / unit_us);
    if (units == 0U) {
        units = 1U;
    }

    *units_out = units;
    return true;
}

static bool recorder_process_event(const edge_capture_event_t *event)
{
    uint32_t units;

    if (event == NULL || event->units == 0U) {
        return true;
    }

    /* The event holds the completed LOW interval before the rising edge. */
    if (event->rising != 0U) {
        if (s_auto_signal_enabled && s_waiting_signal && !s_signal_detected) {
            recorder_mark_signal_detected();
            recorder_reset_wav_render(s_sample_rate);
            /* Do not discard this first completed LOW interval. */
        } else if (s_auto_signal_enabled && s_signal_detected) {
            s_last_pulse_tick = xTaskGetTickCount();
        }
    }

    if (s_auto_signal_enabled && s_waiting_signal && !s_signal_detected) {
        return true;
    }

    if (s_format == RECORDER_FORMAT_WAV) {
        return recorder_render_wav_interval(event->level, event->units);
    }

    if (!recorder_quantize_interval(event->units, &units)) {
        return false;
    }
    return edge_writer_write_interval_units(event->level, units);
}

static bool recorder_flush_events(size_t budget)
{
    edge_capture_event_t events[64];
    size_t total = 0U;

    while (budget == 0U || total < budget) {
        size_t request = sizeof(events) / sizeof(events[0]);
        size_t got;

        if (budget != 0U && request > budget - total) {
            request = budget - total;
        }
        if (request == 0U) {
            break;
        }

        got = edge_capture_read(events, request);
        if (got == 0U) {
            break;
        }

        for (size_t i = 0U; i < got; ++i) {
            if (!recorder_process_event(&events[i])) {
                return false;
            }
        }
        total += got;
    }

    return true;
}

static bool recorder_flush_budgeted(void)
{
    return recorder_flush_events(s_format == RECORDER_FORMAT_WAV ?
        RECORDER_WAV_SERVICE_BUDGET_EVENTS : RECORDER_EDGE_SERVICE_BUDGET_EVENTS);
}

static bool recorder_flush_all(void)
{
    return recorder_flush_events(0U);
}

static bool recorder_finalize_writer(void)
{
    if (s_format == RECORDER_FORMAT_WAV) {
        return recorder_flush_wav_buffer() && wav_writer_finalize();
    }
    return edge_writer_finalize();
}

static void recorder_abort_to_error(void)
{
    edge_capture_stop();
    wav_writer_abort_delete();
    edge_writer_abort_delete();
    s_waiting_signal = false;
    s_state = RECORDER_STATE_ERROR;
}

static bool recorder_open_writer(void)
{
    if (s_format == RECORDER_FORMAT_WAV) {
        return wav_writer_open(s_path, s_sample_rate, 8U, 1U);
    }
    return edge_writer_open(s_path, recorder_get_sample_rate());
}

static void recorder_start_now(void)
{
    if (s_path[0] == '\0') {
        recorder_generate_target_path(RECORDINGS_DIR);
    }

    if (!recorder_open_writer()) {
        s_state = RECORDER_STATE_ERROR;
        return;
    }

    recorder_reset_wav_render(s_sample_rate);
    recorder_reset_signal_session();

    /* Actual edges only: no fixed window, no RMT RX, no clock GPIO. */
    edge_capture_init(RECORDER_CAPTURE_UNIT_US);
    edge_capture_start();
    if (!edge_capture_is_running() || edge_capture_is_overrun()) {
        printf("recorder: edge capture start fail\n");
        wav_writer_abort_delete();
        edge_writer_abort_delete();
        s_state = RECORDER_STATE_ERROR;
        return;
    }

    s_started_tick = xTaskGetTickCount();
    s_elapsed_before_pause_ms = 0U;
    s_elapsed_final_ms = 0U;
    s_waiting_signal = s_auto_signal_enabled;
    s_state = RECORDER_STATE_RECORDING;
}

static void recorder_pause_common(recorder_state_t paused_state)
{
    if (s_state != RECORDER_STATE_RECORDING) {
        return;
    }

    recorder_add_elapsed_if_running();
    edge_capture_pause();

    /* Save every completed interval and the WAV staging buffer before resume. */
    if (!recorder_flush_all() ||
        (s_format == RECORDER_FORMAT_WAV && !recorder_flush_wav_buffer())) {
        recorder_abort_to_error();
        return;
    }

    s_state = paused_state;
}

static bool recorder_resume_common(recorder_state_t expected_state, bool motor_high)
{
    if (s_state != expected_state || !motor_high) {
        return false;
    }

    /* Pause must have left no valid event queued before capture is reset. */
    if (!recorder_flush_all()) {
        recorder_abort_to_error();
        return false;
    }

    edge_capture_resume();
    if (!edge_capture_is_running() || edge_capture_is_overrun()) {
        recorder_abort_to_error();
        return false;
    }

    s_started_tick = xTaskGetTickCount();
    s_state = RECORDER_STATE_RECORDING;
    return true;
}

static void recorder_finish_success(void)
{
    recorder_add_elapsed_if_running();

    /* On STOP, close the active interval before saving the FIFO and SD buffer. */
    edge_capture_pause();
    if (!recorder_flush_all()) {
        recorder_abort_to_error();
        return;
    }
    edge_capture_stop();

    if (!recorder_finalize_writer()) {
        s_waiting_signal = false;
        s_state = RECORDER_STATE_ERROR;
        return;
    }

    s_elapsed_final_ms = s_elapsed_before_pause_ms;
    s_waiting_signal = false;
    s_state = RECORDER_STATE_FINISHED;
}

static void recorder_abort_delete(void)
{
    edge_capture_stop();
    wav_writer_abort_delete();
    edge_writer_abort_delete();
    s_waiting_signal = false;
    s_state = RECORDER_STATE_DELETED;
}

static void recorder_check_auto_stop(void)
{
    TickType_t now;
    uint32_t elapsed_ms;

    if (!s_auto_signal_enabled || s_state != RECORDER_STATE_RECORDING ||
        s_waiting_signal || !s_signal_detected || s_last_pulse_tick == 0) {
        return;
    }

    now = xTaskGetTickCount();
    elapsed_ms = (uint32_t)((now - s_last_pulse_tick) * portTICK_PERIOD_MS);
    if (elapsed_ms >= RECORDER_SIGNAL_TIMEOUT_MS) {
        recorder_finish_success();
    }
}

void recorder_init(void)
{
    s_state = RECORDER_STATE_IDLE;
    s_format = RECORDER_FORMAT_WAV;
    s_sample_rate = RECORDER_DEFAULT_SAMPLE_RATE;
    s_path[0] = '\0';
    s_started_tick = 0;
    s_elapsed_before_pause_ms = 0U;
    s_elapsed_final_ms = 0U;
    s_auto_signal_enabled = false;
    recorder_reset_signal_session();
    recorder_reset_wav_render(s_sample_rate);
    edge_capture_stop();
    wav_writer_abort_delete();
    edge_writer_abort_delete();
}

bool recorder_is_active(void)
{
    return s_state != RECORDER_STATE_IDLE;
}

recorder_state_t recorder_get_state(void)
{
    return s_state;
}

const char *recorder_get_path(void)
{
    return s_path;
}

uint32_t recorder_get_elapsed_seconds(void)
{
    return recorder_elapsed_ms_now() / 1000U;
}

uint32_t recorder_get_sample_rate(void)
{
    if (s_format == RECORDER_FORMAT_LEP) {
        return RECORDER_LEP_SAMPLE_RATE;
    }
    if (s_format == RECORDER_FORMAT_L16) {
        return RECORDER_L16_SAMPLE_RATE;
    }
    return s_sample_rate;
}

uint8_t recorder_get_buffer_ok_percent(void)
{
    const size_t free_events = edge_capture_free();
    const size_t used_events = edge_capture_level();
    const size_t total = free_events + used_events;

    return total == 0U ? 100U :
        (uint8_t)((free_events * 100U) / total);
}

void recorder_set_sample_rate(uint32_t sample_rate)
{
    if (sample_rate == 22050U || sample_rate == 44100U) {
        s_sample_rate = sample_rate;
    }
}

void recorder_set_format(recorder_format_t format)
{
    if (format == RECORDER_FORMAT_WAV ||
        format == RECORDER_FORMAT_LEP ||
        format == RECORDER_FORMAT_L16) {
        s_format = format;
    }
}

recorder_format_t recorder_get_format(void)
{
    return s_format;
}

void recorder_set_auto_signal(bool enabled)
{
    s_auto_signal_enabled = enabled;
}

bool recorder_get_auto_signal(void)
{
    return s_auto_signal_enabled;
}

bool recorder_is_waiting_signal(void)
{
    return s_waiting_signal;
}

bool recorder_has_valid_signal(void)
{
    return s_signal_detected;
}

void recorder_service(void)
{
    if (s_state != RECORDER_STATE_RECORDING &&
        s_state != RECORDER_STATE_PAUSED_USER &&
        s_state != RECORDER_STATE_PAUSED_MOTOR) {
        return;
    }

    if (edge_capture_is_overrun()) {
        recorder_abort_to_error();
        return;
    }

    if (!recorder_flush_budgeted()) {
        recorder_abort_to_error();
        return;
    }

    recorder_check_auto_stop();
}

void recorder_handle_rec(const char *current_dir, bool motor_high)
{
    if (s_state != RECORDER_STATE_IDLE) {
        return;
    }

    if (s_path[0] == '\0') {
        recorder_generate_target_path(current_dir);
    }

    s_elapsed_before_pause_ms = 0U;
    s_elapsed_final_ms = 0U;
    recorder_reset_signal_session();

    if (motor_high) {
        recorder_start_now();
    } else {
        s_state = RECORDER_STATE_WAIT_MOTOR;
    }
}

void recorder_handle_play(bool motor_high)
{
    if (s_state == RECORDER_STATE_WAIT_MOTOR) {
        if (motor_high) {
            recorder_start_now();
        }
        return;
    }

    if (s_state == RECORDER_STATE_RECORDING) {
        recorder_pause_common(RECORDER_STATE_PAUSED_USER);
        return;
    }

    if (s_state == RECORDER_STATE_PAUSED_USER) {
        /* USER pause has priority; MOTOR changes cannot resume it by themselves. */
        (void)recorder_resume_common(RECORDER_STATE_PAUSED_USER, motor_high);
        return;
    }

    if (s_state == RECORDER_STATE_PAUSED_MOTOR) {
        (void)recorder_resume_common(RECORDER_STATE_PAUSED_MOTOR, motor_high);
    }
}

void recorder_handle_stop(void)
{
    if (s_state == RECORDER_STATE_IDLE) {
        return;
    }

    if (s_state == RECORDER_STATE_FINISHED ||
        s_state == RECORDER_STATE_DELETED ||
        s_state == RECORDER_STATE_ERROR) {
        s_state = RECORDER_STATE_IDLE;
        s_path[0] = '\0';
        s_elapsed_before_pause_ms = 0U;
        s_elapsed_final_ms = 0U;
        recorder_reset_signal_session();
        return;
    }

    if (s_state == RECORDER_STATE_WAIT_MOTOR) {
        recorder_abort_delete();
        return;
    }

    recorder_finish_success();
}

void recorder_handle_abort(void)
{
    if (s_state != RECORDER_STATE_IDLE &&
        s_state != RECORDER_STATE_FINISHED &&
        s_state != RECORDER_STATE_DELETED &&
        s_state != RECORDER_STATE_ERROR) {
        recorder_abort_delete();
    }
}

void recorder_service_motor(bool motor_high)
{
    if (s_state == RECORDER_STATE_WAIT_MOTOR && motor_high) {
        recorder_start_now();
        return;
    }

    if (s_state == RECORDER_STATE_RECORDING && !motor_high) {
        recorder_pause_common(RECORDER_STATE_PAUSED_MOTOR);
        return;
    }

    if (s_state == RECORDER_STATE_PAUSED_MOTOR && motor_high) {
        (void)recorder_resume_common(RECORDER_STATE_PAUSED_MOTOR, true);
    }
}
