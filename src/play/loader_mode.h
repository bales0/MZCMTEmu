/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from bales0/MZ-SD2CMT2-Reborn, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * ESP32-S3: native constant data and foreground algorithms; no AVR peripherals.
 */
#ifndef SD2CMT2_LOADER_MODE_H
#define SD2CMT2_LOADER_MODE_H

#include <stdint.h>

typedef enum
{
    LOADER_MODE_NORMAL_1_1 = 0,
    LOADER_MODE_AUTO,
    LOADER_MODE_UL,
    LOADER_MODE_UL_MZ800,
    LOADER_MODE_UL_MZ700,
    LOADER_MODE_MZ700_3X,
    LOADER_MODE_IC_1_4,
    LOADER_MODE_IC_1_3,
    LOADER_MODE_IC_1_2,
    LOADER_MODE_TC_1_3,
    LOADER_MODE_TC_1_2,
    LOADER_MODE_NORMAL_1_2,
    LOADER_MODE_NORMAL_1_3,
    LOADER_MODE_MZ700_1X,
    LOADER_MODE_NORMAL_1_4,
    /* Append-only: loader_mode is persisted in EEPROM. */
    LOADER_MODE_IC_1_1,
    LOADER_MODE_TC_1_1,
    LOADER_MODE_COUNT
} loader_mode_t;

#endif
