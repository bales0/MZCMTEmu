#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "formats/program_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif


#define EDGE_ANALYSIS_VERSION 3U
#ifndef CMT_EDGE_DIAGNOSTICS
#define CMT_EDGE_DIAGNOSTICS 0
#endif
typedef enum {EDGE_ANALYSIS_IDLE,EDGE_ANALYSIS_RUNNING,EDGE_ANALYSIS_COMPLETE,EDGE_ANALYSIS_SKIPPED,EDGE_ANALYSIS_CANCELLED,EDGE_ANALYSIS_ERROR} edge_analysis_state_t;
typedef enum {EDGE_CACHE_MISS,EDGE_CACHE_HIT,EDGE_CACHE_INVALID} edge_cache_status_t;
typedef struct {
 program_metadata_t program; /* Reserved/empty: ordinary preflight counts time only. */
 uint64_t total_us,discovered_us,read_us,decode_us,callback_us,ui_us,yield_us;
 uint32_t scanned_bytes,total_bytes,programs;
 edge_analysis_state_t state;
 edge_cache_status_t cache_status;
 uint8_t aux_profile; /* Existing AutoName SIN/CPM 1..8; not a Sharp loader enum. */
 bool cache_hit,programs_exact;
} edge_analysis_t;
/* Foreground scan callback/owner only. PLAY uses the immutable copy in info. */
void edge_stream_analysis_snapshot(edge_analysis_t *out);
/* Scan owner only; disabled builds perform no timing work. */
void edge_stream_analysis_note_ui(uint64_t us,bool yielding);
void edge_stream_invalidate_cache(void);

typedef struct {
    edge_analysis_t analysis;
    uint64_t total_us; /* Exact waveform duration, including preroll and tail; 0 if skipped. */
    uint32_t sample_rate;
    uint32_t total_seconds;
    uint32_t total_samples;
    uint32_t pulses_per_second;
} edge_stream_info_t;

typedef enum {EDGE_SCAN_CONTINUE,EDGE_SCAN_SKIP,EDGE_SCAN_CANCEL} edge_scan_action_t;
typedef edge_scan_action_t (*edge_scan_control_fn)(uint32_t done,uint32_t total,void *context);
void edge_stream_set_scan_control(edge_scan_control_fn control,void *context);
bool edge_stream_analysis_cancelled(void);

bool edge_stream_open(const char *path, edge_stream_info_t *info);
void edge_stream_close(void);
size_t edge_stream_read(uint8_t *dst, size_t max_len);
bool edge_stream_is_eof(void);
bool edge_stream_has_error(void);
uint32_t edge_stream_get_total_bytes(void);
uint32_t edge_stream_get_consumed_bytes(void);
bool edge_stream_is_edge_path(const char *path);
bool edge_stream_next_pulse(uint8_t *level, uint32_t *duration_us, bool *count_pulse);

#ifdef __cplusplus
}
#endif

bool edge_stream_monitor_muted(void);
