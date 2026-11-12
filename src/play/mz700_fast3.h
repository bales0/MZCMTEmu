/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from bales0/MZ-SD2CMT2-Reborn, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * ESP32-S3: native constant data and foreground algorithms; no AVR peripherals.
 */
#ifndef SD2CMT2_MZ700_FAST3_H
#define SD2CMT2_MZ700_FAST3_H

#include <stdbool.h>
#include <stdint.h>

#define MZ700_FAST3_HEADER_BYTES 128U
#define MZ700_FAST3_NAME_BYTES 17U
#define MZ700_FAST3_RUNTIME_LOW_ADDR 0x1108U
#define MZ700_FAST3_RUNTIME_HIGH_ADDR 0xC000U
#define MZ700_FAST3_RUNTIME_CAPACITY 104U

uint8_t mz700_fast3_runtime_size(
    const uint8_t original_name[MZ700_FAST3_NAME_BYTES]);

bool mz700_fast3_stage_is_encodable(uint16_t runtime_address,
                                    uint8_t runtime_size);

bool mz700_header_only_build(
    uint8_t header[MZ700_FAST3_HEADER_BYTES],
    uint16_t runtime_address,
    const uint8_t *runtime,
    uint8_t runtime_size);

bool mz700_fast3_build_header(
    uint8_t header[MZ700_FAST3_HEADER_BYTES],
    uint16_t runtime_address,
    uint8_t runtime_size,
    uint16_t payload_size,
    uint16_t payload_load,
    uint16_t payload_exec,
    const uint8_t original_name[MZ700_FAST3_NAME_BYTES]);

#endif
