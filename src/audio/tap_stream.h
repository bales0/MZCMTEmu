#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ESP32 port of SD2CMT2-Reborn TAP protocol (GPL-3.0, see LICENSE).
 * Timings remain exact 16 MHz ticks, independently of the output HAL. */
typedef enum {
    TAP_SPEED_1400 = 0, TAP_SPEED_1800, TAP_SPEED_2100, TAP_SPEED_2500,
    TAP_SPEED_COUNT
} tap_speed_t;
typedef struct {
    uint32_t total_bytes;
    uint32_t block_count;
    uint64_t total_ticks;
    uint16_t baud;
} tap_stream_info_t;

bool tap_stream_is_tap_path(const char *path);
uint16_t tap_stream_speed_baud(tap_speed_t speed);
bool tap_stream_open(const char *path, tap_speed_t speed, tap_stream_info_t *info);
void tap_stream_close(void);
bool tap_stream_next_pulse(uint8_t *level, uint32_t *ticks, bool *count_pulse);
bool tap_stream_is_eof(void);
bool tap_stream_has_error(void);

#ifdef __cplusplus
}
#endif

/* Only during preflight analysis, never during playback. */
typedef bool (*tap_stream_cancel_fn)(void *context);
void tap_stream_set_scan_control(tap_stream_cancel_fn fn,void *context);
bool tap_stream_analysis_cancelled(void);

#include "formats/program_metadata.h"
void tap_stream_metadata(program_metadata_t *out);
uint32_t tap_stream_metadata_revision(void);
