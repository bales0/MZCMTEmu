/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {bool valid;uint8_t shown;uint32_t recovered_at;} ui_headroom_t;
uint8_t ui_headroom_update(ui_headroom_t *,uint8_t actual,uint32_t now_ms);
typedef enum {UI_BUFFER_OK,UI_BUFFER_LOW,UI_BUFFER_CRITICAL,UI_BUFFER_BAD} ui_buffer_alarm_t;
ui_buffer_alarm_t ui_buffer_alarm(uint8_t actual);
typedef struct {uint32_t key,changed_at;bool valid;} ui_marquee_t;
size_t ui_marquee_offset(ui_marquee_t *,const char *text,size_t width,uint32_t now_ms,uint32_t context);
