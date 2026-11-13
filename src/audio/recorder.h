#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RECORDER_STATE_IDLE = 0,
    RECORDER_STATE_WAIT_MOTOR,
    RECORDER_STATE_RECORDING,
    RECORDER_STATE_PAUSED_USER,
    RECORDER_STATE_PAUSED_MOTOR,
    RECORDER_STATE_FINISHED,
    RECORDER_STATE_DELETED,
    RECORDER_STATE_ERROR,
    RECORDER_STATE_FINALIZING
} recorder_state_t;

typedef enum {
    RECORDER_FORMAT_WAV = 0,
    RECORDER_FORMAT_LEP,
    RECORDER_FORMAT_L16,
    RECORDER_FORMAT_MZF
} recorder_format_t;

void recorder_init(void);
bool recorder_is_active(void);
recorder_state_t recorder_get_state(void);
const char *recorder_get_path(void);
uint32_t recorder_get_elapsed_seconds(void);
uint32_t recorder_get_sample_rate(void);
uint8_t recorder_get_buffer_ok_percent(void);
void recorder_set_sample_rate(uint32_t sample_rate);
void recorder_set_format(recorder_format_t format);
recorder_format_t recorder_get_format(void);
void recorder_set_auto_signal(bool enable);
bool recorder_get_auto_signal(void);
bool recorder_is_waiting_signal(void);
bool recorder_has_valid_signal(void);
void recorder_service(void);
void recorder_handle_rec(const char *current_dir, bool motor_high);
void recorder_handle_play(bool motor_high);
void recorder_handle_stop(void);
void recorder_handle_abort(void);
void recorder_service_motor(bool motor_high);

#ifdef __cplusplus
}
#endif

void recorder_set_autoname(bool enabled);
bool recorder_get_autoname(void);

bool recorder_metadata_warning(void);

void recorder_media_removed(void);

#include "formats/program_metadata.h"
void recorder_metadata(program_metadata_t *out);

#include "audio/edge_capture.h"
void recorder_capture_status(edge_capture_status_t *out);
const char *recorder_capture_error(void);
