#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "play/loader_mode.h"
#include "formats/file_format.h"
#include "formats/program_metadata.h"
typedef struct {
    program_metadata_t metadata;
    file_format_t format;
    uint16_t record, records;
    char title[22];
    loader_mode_t requested,effective;
    bool sidecar, fallback, ultrafast;
    bool source_reused,duration_cached,duration_deferred; /* Diagnostics for open-session reuse. */
    uint64_t total_ticks;
    uint32_t payload_bytes,transfer_bytes;
} sharp_stream_info_t;
bool sharp_stream_open(const char *path,uint16_t record,loader_mode_t requested,sharp_stream_info_t *info);
void sharp_stream_close(void);
bool sharp_stream_next_pulse(uint8_t *level,uint32_t *ticks,bool *count);
bool sharp_stream_at_boundary(void);
bool sharp_stream_advance(void);
uint32_t sharp_stream_boundary_delay_us(bool motor_low_seen);
bool sharp_stream_ul_ready(void);
bool sharp_stream_begin_ul(uint64_t now_us);
bool sharp_stream_service_ul(uint64_t now_us,uint8_t write_level);
uint8_t sharp_stream_bus_read(void);
uint8_t sharp_stream_bus_sense(void);
uint8_t sharp_stream_ul_progress(void);
uint32_t sharp_stream_ul_bytes(void);
bool sharp_stream_is_eof(void);
bool sharp_stream_has_error(void);

void sharp_stream_pause_ul(uint64_t now_us,bool pause);

/* Foreground analysis only: callback may yield; true cancels before GPIO ownership. */
typedef bool (*sharp_analysis_cancel_fn)(void *context);
void sharp_stream_set_analysis_control(sharp_analysis_cancel_fn cancel,void *context);
bool sharp_stream_analysis_cancelled(void);

bool sharp_stream_continue_record(const char *path,loader_mode_t mode,sharp_stream_info_t *info);

/* Header/loader metadata only; no complete waveform or body duration scan. */
bool sharp_stream_preview(const char *path,uint16_t record,loader_mode_t requested,sharp_stream_info_t *info);

/* Immutable failure reason, safe to retain after stream close. */
const char *sharp_stream_error(void);

/* True only on the final physical half of an original data-byte stop bit. */
bool sharp_stream_last_pulse_source_byte(void);
