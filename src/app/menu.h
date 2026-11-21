/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
typedef enum {
 MENU_ITEM_MODE, MENU_ITEM_SIGNAL_INVERT, MENU_ITEM_PLAY_ULTRAFAST,
 MENU_ITEM_REC_FORMAT, MENU_ITEM_REC_AUTO_SIGNAL, MENU_ITEM_TAP_SPEED,
 MENU_ITEM_AUTONAME, MENU_ITEM_LOADER_SPEED, MENU_ITEM_CMT_SOURCE,
 MENU_ITEM_BRIGHTNESS, MENU_ITEM_ABOUT, MENU_ITEM_KEYMAP, MENU_ITEM_EDIT_SIDECAR, MENU_ITEM_TAPE_ANALYZER, MENU_ITEM_COUNT
} menu_item_t;
typedef enum {APP_MENU_PLAY,APP_MENU_RECORD,APP_MENU_SYSTEM} app_menu_section_t;
typedef struct {app_menu_section_t section;unsigned index;bool edit_sidecar,tape_analyzer;} app_menu_t;
static inline unsigned app_menu_count(const app_menu_t *m){return m->section==APP_MENU_RECORD ? 3U:m->section==APP_MENU_PLAY ? 5U+(unsigned)m->edit_sidecar+(unsigned)m->tape_analyzer:4U;}
static inline menu_item_t app_menu_item(const app_menu_t *m,unsigned row){
 static const menu_item_t play[]={MENU_ITEM_SIGNAL_INVERT,MENU_ITEM_MODE,MENU_ITEM_PLAY_ULTRAFAST,MENU_ITEM_LOADER_SPEED,MENU_ITEM_TAP_SPEED,MENU_ITEM_EDIT_SIDECAR};
 static const menu_item_t record[]={MENU_ITEM_REC_FORMAT,MENU_ITEM_REC_AUTO_SIGNAL,MENU_ITEM_AUTONAME};
 static const menu_item_t system[]={MENU_ITEM_CMT_SOURCE,MENU_ITEM_BRIGHTNESS,MENU_ITEM_ABOUT,MENU_ITEM_KEYMAP};
 if(m->section==APP_MENU_RECORD)return row<3 ? record[row]:MENU_ITEM_COUNT;
 if(m->section==APP_MENU_PLAY){
  if(row<5)return play[row];
  if(m->edit_sidecar && row==5)return MENU_ITEM_EDIT_SIDECAR;
  if(m->tape_analyzer && row==5U+(unsigned)m->edit_sidecar)return MENU_ITEM_TAPE_ANALYZER;
  return MENU_ITEM_COUNT;
 }
 return row<4 ? system[row]:MENU_ITEM_COUNT;
}
static inline unsigned app_menu_index(const app_menu_t *m){return m->index;}
static inline menu_item_t app_menu_selected(const app_menu_t *m){return app_menu_item(m,m->index);}
static inline void app_menu_move(app_menu_t *m,int delta){unsigned n=app_menu_count(m);m->index=delta>0 ? (m->index+1)%n:(m->index+n-1)%n;}
static inline unsigned app_menu_next_record_control(unsigned value){return value==0 ? 2:value==2 ? 1:0;}

/* ABOUT is page 1; only KEYMAP pages 2..5 form a navigation cycle. */
static inline unsigned app_menu_info_move(unsigned page,int delta){
 if(page<2 || page>5)return page;
 return delta>0 ? (page==5 ? 2:page+1):(page==2 ? 5:page-1);
}
