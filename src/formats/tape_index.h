/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "formats/program_metadata.h"

/* RAM-only schema. No source-file writes, allocation or transport ownership.
 * Capacities are upper bounds, not embedded arrays or allocation requests.
 * Zero-initialized entries are unverified and cannot authorize program PLAY. */
#define TAPE_INDEX_SCHEMA_VERSION 3U
#define TAPE_INDEX_PROGRAM_CAPACITY 256U
#define TAPE_INDEX_BLOCK_CAPACITY 2048U
#define TAPE_INDEX_CHECKPOINT_CAPACITY 4096U
#define TAPE_INDEX_PATH_CAPACITY 1024U
#define TAPE_INDEX_NO_ENTRY UINT32_MAX

typedef enum {
    TAPE_INDEX_IDLE = 0,
    TAPE_INDEX_SCANNING,
    TAPE_INDEX_FINISHED,
    TAPE_INDEX_CANCELLED,
    TAPE_INDEX_ERROR
} tape_index_state_t;

typedef enum {
    TAPE_VALIDITY_UNCERTAIN = 0,
    TAPE_VALIDITY_VERIFIED,
    TAPE_VALIDITY_INVALID
} tape_validity_t;

typedef enum {
    TAPE_BLOCK_UNKNOWN = 0,
    TAPE_BLOCK_LEADER,
    TAPE_BLOCK_HEADER,
    TAPE_BLOCK_PAYLOAD,
    TAPE_BLOCK_CHECKSUM,
    TAPE_BLOCK_DATA,
    TAPE_BLOCK_GAP
} tape_block_kind_t;

typedef enum {
    TAPE_CHECKPOINT_NONE = 0,
    TAPE_CHECKPOINT_EDGE,
    TAPE_CHECKPOINT_WAV
} tape_checkpoint_kind_t;

enum {
    TAPE_RANGE_OFFSET_VALID = 1U,
    TAPE_RANGE_TIME_VALID = 2U
};

typedef struct {
    /* Half-open boundaries in the original source, never prefetch positions.
     * Times exclude synthetic preroll/tail. Invalid boundaries are not zero
     * timestamps: consult the owning entry's range_flags first. */
    uint64_t offset_begin, offset_end;
    uint64_t time_begin_us, time_end_us;
} tape_range_t;

typedef struct {
    char path[TAPE_INDEX_PATH_CAPACITY];
    uint64_t size_bytes;
    int64_t mtime_seconds, ctime_seconds;
    uint64_t device, inode;
    uint32_t storage_epoch;
    uint32_t fingerprint;
    uint32_t fingerprint_version;
    file_format_t format;
    bool stat_valid;
    bool fingerprint_valid;
    /* A sampled fingerprint is a cache hint, not proof of full identity.
     * Media/write invalidation and revalidation remain mandatory. */
} tape_source_identity_t;

typedef struct {
    tape_range_t range;
    uint32_t program_index; /* NO_ENTRY unless a program assignment is known. */
    uint32_t next_in_program; /* NO_ENTRY terminates a noncontiguous chain. */
    tape_block_kind_t kind;
    tape_validity_t validity;
    uint8_t range_flags;
    uint8_t protocol; /* program_protocol_t; UNKNOWN until proven. */
    uint8_t source;   /* program_source_t; preserves provenance. */
    bool checksum_present;
    bool checksum_verified;
} tape_block_t;

typedef struct {
    program_metadata_t metadata;
    tape_range_t chain;
    uint64_t chain_begin_ticks16,chain_end_ticks16;
    uint32_t first_block, block_count;
    uint32_t start_checkpoint;
    tape_validity_t validity;
    uint8_t range_flags;
    uint8_t aux_profile; /* Separate from the Sharp loader-mode enum. */
    bool chain_complete;
    bool dependencies_verified;
    bool seekable;
    /* seekable requires VERIFIED validity, known whole-chain boundaries,
     * verified dependencies and an exact restorable start checkpoint.
     * A valid header/payload alone is insufficient. */
} tape_program_t;

typedef struct {
    /* State immediately BEFORE reading source_offset (next unread byte).
     * time_us is the cursor in the original waveform, excluding synthetic
     * preroll/tail. Zero remaining_ticks16 means no partially emitted run.
     * Edge: level restores zero-byte continuation; remaining ticks preserve
     * any consumed interval. WAV: source_offset is sample-aligned, and the
     * absolute sample_index plus hysteresis restore rounding and deadband.
     * All decoder/read-ahead queues must be empty at a published checkpoint.
     * A valid time checkpoint does not establish a safe program start. */
    uint64_t source_offset;
    uint64_t time_us;
    uint64_t time_ticks16; /* Exact WAV phase; time_us alone loses fractions. */
    uint64_t sample_index;
    uint32_t remaining_ticks16;
    uint8_t kind; /* tape_checkpoint_kind_t */
    uint8_t level;
    uint8_t hysteresis;
    bool valid;
} tape_checkpoint_t;

/* Physical gaps, independent of program validity. Times exclude TX padding. */
#define TAPE_GAP_CAPACITY 256U
#define TAPE_GAP_MIN_US 500000ULL
#define TAPE_GAP_LEAD_US 200000ULL
typedef struct {uint64_t begin_us,end_us,begin_offset,end_offset,target_offset;} tape_gap_t;

typedef struct {
    tape_source_identity_t source;
    uint32_t schema_version;
    uint32_t algorithm_version;
    tape_index_state_t state;
    uint64_t scanned_bytes;
    uint64_t cursor_read_calls,cursor_read_bytes; /* Caller reads, not VFS metrics. */
    uint64_t waveform_us; /* Original source only; not a program duration. */
    uint64_t waveform_ticks16;
    uint32_t synthetic_preroll_us, synthetic_tail_us;
    uint32_t program_count_recognized, program_count_verified;
    uint32_t block_count, checkpoint_count;
    uint32_t program_capacity, block_capacity, checkpoint_capacity;
    bool duration_known;
    bool scan_complete;  /* Requested preparation done; navigation may use identity/header only. */
    bool index_complete; /* Whole-source coverage/chain classification proven. */
    bool truncated;      /* A bounded pool filled; preserve existing entries. */
    bool cache_hit;
    bool time_only; /* Duration/checkpoints only: no content decoder was run. */
    bool gaps_ready,gaps_truncated;
    uint32_t gap_count,gap_capacity;
    const tape_gap_t *gaps;
    /* One foreground analyzer owner mutates bounded PSRAM pools. Readers
     * receive a stable published result; these pointers convey no ownership.
     * NULL pools and zero capacities describe an empty/unallocated index. */
    const tape_program_t *programs;
    const tape_block_t *blocks;
    const tape_checkpoint_t *checkpoints;
} tape_index_t;
