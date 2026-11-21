/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "formats/tape_index.h"
#include "audio/wav_reader.h"

/* Foreground, caller-owned cursor. No live PLAY/RECORD globals are borrowed.
 * Buffer size matches the existing storage cache. Keep cursors off RTOS stack. */
typedef struct {
    cmt_file_t *file;
    wav_info_t wav;
    file_format_t format;
    uint64_t offset,end,ticks16,sample_index,limit_ticks16;
    uint64_t read_calls,read_bytes;
    uint32_t remaining_ticks16;
    uint8_t level,hysteresis;
    uint8_t buffer[1024];
    unsigned position,available;
    bool error;
} tape_cursor_t;
typedef bool (*tape_analyzer_control_fn)(void *context); /* false cancels */

bool tape_analyzer_supported(const char *path);
bool tape_analyzer_begin(const char *path);
/* Bounded foreground work; false means finished, cancelled or failed. */
bool tape_analyzer_step(void);
void tape_analyzer_cancel(void);
void tape_analyzer_invalidate(void);
const tape_index_t *tape_analyzer_result(void);
bool tape_index_source_valid(const tape_index_t *index);
uint8_t tape_analyzer_percent(void);
size_t tape_analyzer_pool_bytes(void);

bool tape_cursor_open(tape_cursor_t *,const tape_index_t *,const tape_checkpoint_t *);
void tape_cursor_close(tape_cursor_t *);
void tape_cursor_checkpoint(const tape_cursor_t *,tape_checkpoint_t *);
bool tape_cursor_next(tape_cursor_t *,uint8_t *level,uint32_t *ticks16);
/* Clamp to original tape time and restore waveform state. Time-only WAV
 * resolves hysteresis lazily; no content decoder or TX is used here.
 * On cancellation the cursor is closed. */
bool tape_cursor_seek(tape_cursor_t *,const tape_index_t *,uint64_t target_us,
                      tape_analyzer_control_fn,void *);
/* Active recognized candidate, immutable snapshot for scan UI. */
void tape_analyzer_preview(program_metadata_t *);

/* Separate lightweight cache. These functions never invoke tape decoders or
 * allocate program/block pools. Ordinary LEP/L16 preflight feeds raw blocks;
 * WAV duration is read from RIFF only. Initialize caller-owned cursors to 0. */
bool tape_time_scan_begin(const char *path,file_format_t,uint64_t bytes);
bool tape_time_scan_feed(const uint8_t *,size_t);
bool tape_time_scan_end(void);
void tape_time_scan_cancel(void);
const tape_index_t *tape_time_result(void);
const tape_index_t *tape_time_prepare(const char *,tape_analyzer_control_fn,void *);
/* -1 outside a potentially long WAV deadband-state lookup. */
int tape_time_seek_percent(void);
