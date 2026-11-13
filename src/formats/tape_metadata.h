/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "formats/program_metadata.h"
/* Exclusive PLAY/RECORD foreground decoder owner, no FS, no second decoder. */
void tape_metadata_begin(file_format_t);
void tape_metadata_feed(uint8_t level,uint32_t duration_us,bool synthetic);
void tape_metadata_end(void);
void tape_metadata_snapshot(program_metadata_t *);
