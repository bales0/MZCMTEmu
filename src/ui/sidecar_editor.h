/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "drivers/buttons.h"
#include "formats/program_metadata.h"
#include "ui/ui.h"
bool sc_editor_open(const char *path,uint16_t records,uint16_t record);
bool sc_editor_active(void);
bool sc_editor_busy(void);
uint16_t sc_editor_record(void);
/* Metadata preview follows the highlighted candidate, not the accepted record. */
uint16_t sc_editor_preview_record(void);
const char *sc_editor_path(void);
void sc_editor_metadata(const program_metadata_t *);
void sc_editor_event(btn_event_t event,bool available);
void sc_editor_render(ui_model_t *,uint32_t now_ms);
