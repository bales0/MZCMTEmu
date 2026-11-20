/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
typedef enum {
 MENU_ITEM_MODE, MENU_ITEM_SIGNAL_INVERT, MENU_ITEM_PLAY_ULTRAFAST,
 MENU_ITEM_REC_FORMAT, MENU_ITEM_REC_AUTO_SIGNAL, MENU_ITEM_TAP_SPEED,
 MENU_ITEM_AUTONAME, MENU_ITEM_LOADER_SPEED, MENU_ITEM_CMT_SOURCE,
 MENU_ITEM_BRIGHTNESS, MENU_ITEM_ABOUT, MENU_ITEM_KEYMAP, MENU_ITEM_EDIT_SIDECAR, MENU_ITEM_COUNT
} menu_item_t;
typedef enum {APP_MENU_PLAY,APP_MENU_RECORD,APP_MENU_SYSTEM} app_menu_section_t;
typedef struct {app_menu_section_t section;unsigned index;bool edit_sidecar;} app_menu_t;
static inline unsigned app_menu_count(const app_menu_t *m){return m->section==APP_MENU_RECORD ? 3U:m->section==APP_MENU_PLAY ? (m->edit_sidecar ? 6U:5U):4U;}
static inline menu_item_t app_menu_item(const app_menu_t *m,unsigned row){
 static const menu_item_t play[]={MENU_ITEM_SIGNAL_INVERT,MENU_ITEM_MODE,MENU_ITEM_PLAY_ULTRAFAST,MENU_ITEM_LOADER_SPEED,MENU_ITEM_TAP_SPEED,MENU_ITEM_EDIT_SIDECAR};
 static const menu_item_t record[]={MENU_ITEM_REC_FORMAT,MENU_ITEM_REC_AUTO_SIGNAL,MENU_ITEM_AUTONAME};
 static const menu_item_t system[]={MENU_ITEM_CMT_SOURCE,MENU_ITEM_BRIGHTNESS,MENU_ITEM_ABOUT,MENU_ITEM_KEYMAP};
 if(m->section==APP_MENU_RECORD)return row<3 ? record[row]:MENU_ITEM_COUNT;
 if(m->section==APP_MENU_PLAY)return row<(m->edit_sidecar ? 6U:5U) ? play[row]:MENU_ITEM_COUNT;
 return row<4 ? system[row]:MENU_ITEM_COUNT;
}
static inline unsigned app_menu_index(const app_menu_t *m){return m->index;}
static inline menu_item_t app_menu_selected(const app_menu_t *m){return app_menu_item(m,m->index);}
static inline void app_menu_move(app_menu_t *m,int delta){unsigned n=app_menu_count(m);m->index=delta>0 ? (m->index+1)%n:(m->index+n-1)%n;}
static inline unsigned app_menu_next_record_control(unsigned value){return value==0 ? 2:value==2 ? 1:0;}
