/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from bales0/MZ-SD2CMT2-Reborn, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * ESP32-S3: native constant data and foreground algorithms; no AVR peripherals.
 */
#ifndef SD2CMT2_MZF_LOADER_H
#define SD2CMT2_MZF_LOADER_H

#include <stdbool.h>
#include <stdint.h>

#include "../formats/file_format.h"
#include "loader_mode.h"

/*
   Test-tuning addresses for the small Z80 ultrafast receiver.
   The automatic selector uses LOW first, then HIGH, only when the chosen
   receiver range does not overlap the MZF payload destination range.
*/
#ifndef MZF_LOADER_LOW_LOAD_ADDR
#define MZF_LOADER_LOW_LOAD_ADDR 0x1100U
#endif

#ifndef MZF_LOADER_HIGH_LOAD_ADDR
#define MZF_LOADER_HIGH_LOAD_ADDR 0xC000U
#endif

#ifndef MZF_LOADER_PUMP_BYTES
#define MZF_LOADER_PUMP_BYTES 64U
#endif

typedef enum
{
    MZF_LOADER_VARIANT_NONE = 0,
    MZF_LOADER_VARIANT_LOW,
    MZF_LOADER_VARIANT_HIGH,
    MZF_LOADER_VARIANT_MZ800_HEADER,
    MZF_LOADER_VARIANT_MZ700_UL_LOW,
    MZF_LOADER_VARIANT_MZ700_UL_HIGH,
    MZF_LOADER_VARIANT_MZ700_FAST3_LOW,
    MZF_LOADER_VARIANT_MZ700_FAST3_HIGH,
    MZF_LOADER_VARIANT_IC_1_4,
    MZF_LOADER_VARIANT_IC_1_3,
    MZF_LOADER_VARIANT_IC_1_2,
    MZF_LOADER_VARIANT_TC_1_3,
    MZF_LOADER_VARIANT_TC_1_2,
    /* Append-only to preserve existing variant values used by diagnostics. */
    MZF_LOADER_VARIANT_IC_1_1,
    MZF_LOADER_VARIANT_TC_1_1
} mzf_loader_variant_t;

void mzf_loader_reset(void);

/* Optional record-data source used by container formats whose payload is not
   byte-contiguous in the backing file (for example an MFM QuickDisk track). */
typedef bool (*mzf_loader_source_seek_t)(uint32_t position);
typedef int16_t (*mzf_loader_source_read_t)(void *buffer, uint16_t size);
void mzf_loader_set_source_io(mzf_loader_source_seek_t seek_fn,
                              mzf_loader_source_read_t read_fn);

bool mzf_loader_prepare(file_format_t format,
                           loader_mode_t mode,
                           const uint8_t *header,
                           uint32_t file_size,
                           uint32_t data_offset);

bool mzf_loader_is_active(void);
bool mzf_loader_is_ul_active(void);
bool mzf_loader_is_header_only(void);
bool mzf_loader_is_mz800_header_high(void);
bool mzf_loader_is_mz700_ul(void);
bool mzf_loader_is_mz700_ul_high(void);
bool mzf_loader_is_mz700_fast3(void);
bool mzf_loader_is_mz700_fast3_high(void);
bool mzf_loader_is_ic_turbo(void);
bool mzf_loader_is_tc_turbo(void);
bool mzf_loader_is_tape_turbo(void);
mzf_loader_variant_t mzf_loader_get_variant(void);

uint16_t mzf_loader_get_loader_size(void);
bool mzf_loader_patch_loader_header(uint8_t *header);
uint16_t mzf_loader_build_loader(uint8_t *destination, uint16_t capacity);

bool mzf_loader_begin(uint64_t now_us);
/* Foreground bounded event step: no busy wait, source read outside ISR. */
bool mzf_loader_bus_step(uint64_t now_us, uint8_t write_level);
typedef struct { const char *phase; int8_t expected_write; uint8_t pair; } mzf_loader_bus_debug_t;
/* Foreground snapshot, take before a failed step resets bus outputs. */
mzf_loader_bus_debug_t mzf_loader_bus_debug(void);
bool mzf_loader_bus_data_phase(void);
uint8_t mzf_loader_bus_read(void);
uint8_t mzf_loader_bus_sense(void);
bool mzf_loader_bus_error(void);
void mzf_loader_bus_pause(uint64_t now_us,bool pause);
bool mzf_loader_is_finished(void);

uint8_t mzf_loader_get_progress_percent(void);
uint32_t mzf_loader_get_transferred(void);
const char *mzf_loader_get_error_text(void);

#endif
