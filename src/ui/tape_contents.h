/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "formats/tape_analyzer.h"
#include "ui/ui.h"
#include "ui/policy.h"
#include "ui/view.h"
#include "drivers/buttons.h"
typedef enum {TAPE_UI_CLOSED,TAPE_UI_SCAN,TAPE_UI_LIST,TAPE_UI_DETAIL,
              TAPE_UI_TRANSPORT,TAPE_UI_SEEK,TAPE_UI_ERROR} tape_ui_page_t;
typedef enum {TAPE_ACTION_NONE,TAPE_ACTION_CLOSE,TAPE_ACTION_CANCEL_SCAN,
              TAPE_ACTION_START,TAPE_ACTION_PAUSE,TAPE_ACTION_RESUME,
              TAPE_ACTION_STOP,TAPE_ACTION_SEEK,TAPE_ACTION_BEGIN_SEEK,
              TAPE_ACTION_START_TIME,TAPE_ACTION_GAP_NEXT,TAPE_ACTION_GAP_PREV} tape_ui_action_t;
typedef struct {
    tape_ui_page_t page;
    unsigned selected; /* 0=whole tape, 1..N=programs */
    uint64_t seek_us,seek_origin_us,seek_offset,seek_origin_offset;
    uint32_t held_at;
    int8_t seek_direction;
    bool seek_unverified,seek_origin_unverified,start_unavailable,seek_by_byte;
    char rows[8][22];
    ui_marquee_t title_marquee;
} tape_contents_t;
typedef struct {
    bool playing,user_paused,motor_paused,finished,waiting,error;
    uint64_t position_us,total_us,tape_position_us,source_offset;
    bool absolute_time;
    unsigned buffer_percent;
    const char *error_message;
} tape_transport_view_t;
void tape_contents_open(tape_contents_t *);
tape_ui_action_t tape_contents_event(tape_contents_t *,const tape_index_t *,btn_event_t,
                                     uint32_t now_ms,const tape_transport_view_t *);
void tape_contents_render(tape_contents_t *,const tape_index_t *,ui_model_t *,
                          uint32_t now_ms,const tape_transport_view_t *);
/* Overlay seek controls on the existing PLAY metadata and fixed footer. */
void tape_contents_apply_transport(const tape_contents_t *,const tape_index_t *,ui_transport_view_t *);
