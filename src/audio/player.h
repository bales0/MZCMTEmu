#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio/tap_stream.h"
#include "audio/sharp_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAYER_STATE_IDLE = 0,
    PLAYER_STATE_PLAYING,
    PLAYER_STATE_PAUSED,
    PLAYER_STATE_STOPPED,
    PLAYER_STATE_EOF,
    PLAYER_STATE_UNDERRUN,
    PLAYER_STATE_ERROR,
    PLAYER_STATE_READY
} player_state_t;

typedef enum {
    PLAYER_PAUSE_NONE = 0,
    PLAYER_PAUSE_USER,
    PLAYER_PAUSE_MOTOR
} player_pause_reason_t;

void player_init(void);
void player_task_start(void);
void player_start(const char *path);
void player_stop(void);
void player_pause_user(void);
void player_resume_user(void);
void player_pause_motor(void);
void player_resume_motor(void);
void player_service(void);

bool player_is_playing(void);
player_state_t player_get_state(void);
player_pause_reason_t player_get_pause_reason(void);

const char *player_get_current_path(void);
uint32_t player_get_sample_rate(void);
uint32_t player_get_total_seconds(void);
uint32_t player_get_position_seconds(void);
uint8_t player_get_progress_percent(void);
uint32_t player_get_baud_rate(void);
uint32_t player_get_pulses_per_second(void);
bool player_is_binary_tape(void);
bool player_is_edge_tape(void);
void player_set_ultrafast_enabled(bool enabled);
bool player_get_ultrafast_enabled(void);
bool player_is_ultrafast_active(void);

/* Compatibility shortcuts used by older UI code. */
void player_pause(void);
void player_resume(void);

bool player_is_tap(void);
bool player_set_tap_speed(tap_speed_t speed);
tap_speed_t player_get_tap_speed(void);

bool player_set_loader_mode(loader_mode_t mode);
loader_mode_t player_get_loader_mode(void);
const sharp_stream_info_t *player_sharp_details(void);
bool player_prepare_record(const char *path,uint16_t record);
bool player_set_record_mode(loader_mode_t mode);

#ifdef __cplusplus
}
#endif

bool player_monitor_active(void);

void player_metadata(program_metadata_t *out);
