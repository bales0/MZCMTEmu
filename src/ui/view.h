/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "ui/ui.h"
#include "file_list.h"
#include "formats/program_metadata.h"
#include "audio/raw_pulse.h"
typedef struct {
 program_metadata_t program;
 raw_pulse_status_t tx;
 const char *status,*filename,*warning;
 uint32_t seconds,total_seconds,session,sample_rate;
 uint16_t playback_baud;
 uint8_t progress,rx_free,rx_min;
 bool tx_expected,record_active,has_info_sidecar;
} ui_transport_view_t;
void ui_use_text_view(void);
void ui_set_browser_view(const char *const rows[FILE_LIST_VIEW_ROWS],int selected,unsigned directories,unsigned index,unsigned total,bool has_info_sidecar);
void ui_set_transport_view(bool record,const ui_transport_view_t *);
void ui_set_external(bool external);
void ui_overlay_show(const char *,uint32_t duration_ms,unsigned priority);
void ui_overlay_clear(bool acknowledge);
const char *ui_overlay_current(uint32_t now_ms);
void ui_view_reset(void);
void ui_view_draw(const ui_model_t *,uint32_t now_ms);

/* Foreground preflight animation: eight graphical phases, no font glyphs. */
void ui_set_analysis_view(unsigned phase);
