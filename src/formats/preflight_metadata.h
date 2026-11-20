/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "formats/program_metadata.h"
/* Exclusive foreground preflight context, independent of PLAY/RECORD. */
void preflight_tape_metadata_begin(file_format_t);
void preflight_tape_metadata_feed(uint8_t,uint32_t,bool);
void preflight_tape_metadata_end(void);
void preflight_tape_metadata_snapshot(program_metadata_t *);
uint32_t preflight_record_autoname_metadata_revision(void);
bool preflight_record_autoname_get_detected_aux_profile(uint8_t *profile);
