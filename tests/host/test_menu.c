#include <assert.h>
#include <stdio.h>
#include "app/menu.h"
int main(void){
 const menu_item_t expected[][5]={{MENU_ITEM_SIGNAL_INVERT,MENU_ITEM_MODE,MENU_ITEM_PLAY_ULTRAFAST,MENU_ITEM_LOADER_SPEED,MENU_ITEM_TAP_SPEED},{MENU_ITEM_REC_FORMAT,MENU_ITEM_REC_AUTO_SIGNAL,MENU_ITEM_AUTONAME},{MENU_ITEM_CMT_SOURCE,MENU_ITEM_BRIGHTNESS,MENU_ITEM_ABOUT,MENU_ITEM_KEYMAP}};
 for(unsigned section=0;section<3;++section){
  app_menu_t m={.section=(app_menu_section_t)section};unsigned n=section==0 ? 5:section==1 ? 3:4;
  assert(app_menu_count(&m)==n);
  for(unsigned i=0;i<n;++i){assert(app_menu_selected(&m)==expected[section][i]);app_menu_move(&m,1);}
  assert(app_menu_index(&m)==0);app_menu_move(&m,-1);assert(app_menu_selected(&m)==expected[section][n-1]);
  assert(app_menu_item(&m,n)==MENU_ITEM_COUNT);
  for(unsigned i=0;i<n;++i)assert(section==1 || (app_menu_item(&m,i)!=MENU_ITEM_REC_FORMAT && app_menu_item(&m,i)!=MENU_ITEM_REC_AUTO_SIGNAL && app_menu_item(&m,i)!=MENU_ITEM_AUTONAME));
 }
 assert(app_menu_next_record_control(0)==2 && app_menu_next_record_control(2)==1 && app_menu_next_record_control(1)==0);
 puts("PASS separate reference PLAY/RECORD/SYSTEM menus; record settings absent from PLAY, bounded rows/wrap; MOTOR -> AUTO -> MANUAL");
 return 0;
}
