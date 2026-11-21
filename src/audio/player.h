#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "audio/tap_stream.h"
#include "audio/sharp_stream.h"
#include "formats/tape_analyzer.h"

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

const char *player_error_message(void);

typedef enum {
 PLAYER_STOP_NONE,PLAYER_STOP_API,PLAYER_STOP_BUTTON,PLAYER_STOP_LONG_BUTTON,
 PLAYER_STOP_S1_EXTERNAL,PLAYER_STOP_SD_REMOVED,PLAYER_STOP_SD_REINIT,
 PLAYER_STOP_SD_RESTORE_FAILED,PLAYER_STOP_RECORD_START
} player_stop_cause_t;
void player_stop_for(player_stop_cause_t cause);
const char *player_stop_cause_name(void);
const char *player_state_name(player_state_t state);

/* Physical connector level; false return means UL retains ownership. */
bool player_set_sense_level(bool high);

/* Stop QD output and retain the validated image for the record selector. */
bool player_stop_to_selector(void);

/* Analyzer-program preview only. Ordinary PLAY has no preflight metadata. */
bool player_preflight_metadata(program_metadata_t *out);
bool player_can_time_seek(void);
/* Lightweight navigation index, never forces a length/content scan. Duration
 * can be unknown; callers must inspect duration_known. Call paused/EOF. */
const tape_index_t *player_time_index(tape_analyzer_control_fn,void *);
/* Indexed waveform transport. Refuses active PLAY and motor-paused transport. Stops old
 * TX, clears pending/FIFO and metadata, restores source, prefills, then stays
 * USER_PAUSED until an explicit resume and application MOTOR/S1 permission.
 * NO_ENTRY selects whole tape; time_seek is an explicitly unverified start. */
bool player_prepare_tape(const tape_index_t *,uint32_t program,uint64_t time_us,
                         bool time_seek,tape_analyzer_control_fn,void *);
bool player_prepare_tape_offset(const tape_index_t *,uint64_t offset,tape_analyzer_control_fn,void *);
uint64_t player_tape_source_offset(void); /* Physically completed source bytes, not prefill. */
bool player_tape_absolute_time(void);
uint64_t player_tape_position_us(void); /* Original tape clock, not prefetch. */
uint64_t player_tape_elapsed_us(void);  /* Physical selected-session elapsed. */
uint64_t player_tape_total_us(void); /* Whole original waveform, excludes synthetic silence. */
bool player_tape_unverified_start(void); /* Time seek is not a verified program anchor. */
