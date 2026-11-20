/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui/sidecar_editor.h"
#include "formats/sidecar_edit.h"
#include "storage/fs.h"
#include "ui/policy.h"
#include "ui/key_icons.h"
#include <stdio.h>
#include <string.h>
typedef enum {VIEW,PICK_RECORD,PICK_FAMILY,PICK_SPEED,DISCARD,RECOVER_CONFIRM,FAILURE} page_t;
static sc_document_t doc;
static page_t page;
static bool active,available_now;
static unsigned cursor,proposal,draft_family;
static uint16_t record,metadata_record;
static uint8_t pending;
static program_metadata_t metadata;
static char rows[8][64];
static ui_marquee_t file_scroll,title_scroll;
/* Release/acquire publishes the storage job's document updates to the UI. */
static unsigned job; /* 0 idle, 1 running, 2 successful, 3 failed */
static unsigned operation;
static void worker(void *unused){
 (void)unused;bool ok;
 if(operation==0){char source[1024];strcpy(source,doc.source);uint16_t records=doc.records;ok=sc_load(&doc,source,records);}
 else if(operation==1)ok=sc_save(&doc);
 else {ok=sc_recover(&doc);if(ok){char source[1024];strcpy(source,doc.source);uint16_t records=doc.records;ok=sc_load(&doc,source,records);}}
 __atomic_store_n(&job,ok ? 2U:3U,__ATOMIC_RELEASE);
}
static bool start_job(unsigned op){
 operation=op;__atomic_store_n(&job,1U,__ATOMIC_RELEASE);
 if(!cmt_storage_execute_async(worker,NULL)){doc.error="STORAGE BUSY";__atomic_store_n(&job,3U,__ATOMIC_RELEASE);return false;}return true;
}
bool sc_editor_busy(void){return active && __atomic_load_n(&job,__ATOMIC_ACQUIRE)==1;}
bool sc_editor_active(void){return active;}
uint16_t sc_editor_record(void){return record;}
uint16_t sc_editor_preview_record(void){return page==PICK_RECORD ? (uint16_t)proposal:record;}
const char *sc_editor_path(void){return doc.source;}
void sc_editor_metadata(const program_metadata_t *m){metadata=*m;metadata_record=sc_editor_preview_record();}
bool sc_editor_open(const char *path,uint16_t records,uint16_t selected){
 if(active || !path || strlen(path)>=sizeof(doc.source) || !records || !selected || selected>records)return false;
 memset(&doc,0,sizeof(doc));strcpy(doc.source,path);doc.records=records;record=selected;
 memset(&metadata,0,sizeof(metadata));metadata_record=0;cursor=0;page=VIEW;active=true;available_now=true;return start_job(0);
}
static uint8_t current(void){return sc_profile(&doc,record);}
static const char *family(uint8_t mode){const char *f;return sc_mode_fields(mode,&f,NULL) ? f:"--";}
static const char *speed(uint8_t mode){const char *s;return sc_mode_fields(mode,NULL,&s) ? s:"--";}
static unsigned family_index(uint8_t mode){for(unsigned f=0;f<7;++f)for(unsigned s=0;s<4;++s)if(sc_make_mode(f,s)==mode && mode!=SC_NONE)return f;return 0;}
static unsigned speed_index(uint8_t mode){for(unsigned f=0;f<7;++f)for(unsigned s=0;s<4;++s)if(sc_make_mode(f,s)==mode && mode!=SC_NONE)return s;return 0;}
static uint8_t proposed_mode(void){
 if(page==PICK_FAMILY){
  unsigned selected_speed=proposal>=4 ? 0:speed_index(current());
  uint8_t mode=sc_make_mode(proposal,selected_speed);
  return mode==SC_NONE ? sc_make_mode(proposal,0):mode;
 }
 return sc_make_mode(draft_family,proposal);
}
static void accept_profile(void){
 if(pending!=SC_NONE && pending!=current() && !sc_accept(&doc,record,pending)){page=FAILURE;return;}
 page=VIEW;
}
static void cancel(void){if(sc_changes(&doc))page=DISCARD;else active=false;}
void sc_editor_event(btn_event_t e,bool available){
 if(!active)return;
 available_now=available;
 unsigned state=__atomic_load_n(&job,__ATOMIC_ACQUIRE);
 if(state==1)return;
 if(state>=2){__atomic_store_n(&job,0U,__ATOMIC_RELEASE);page=state==3 ? FAILURE:VIEW;return;}
 bool stop=e==BTN_STOP || e==BTN_STOP_LONG;
 if(!available && page!=DISCARD){if(stop)cancel();return;}
 if(stop){
  if(page==VIEW || page==FAILURE)cancel();
  else page=page==RECOVER_CONFIRM ? FAILURE:VIEW;
  return;
 }
 int delta=e==BTN_NEXT ? 1:e==BTN_PREV ? -1:0;
 if(page==VIEW){
  if(delta)cursor=delta>0 ? (cursor+1)%3:(cursor+2)%3;
  if(e==BTN_PLAY){
   if(cursor==0){proposal=record;page=PICK_RECORD;}
   else if(cursor==1){proposal=family_index(current());page=PICK_FAMILY;}
   else if(cursor==2){if(sc_changes(&doc))(void)start_job(1);}
  }
 }else if(page==PICK_RECORD){
  if(delta)proposal=delta>0 ? proposal==doc.records ? 1:proposal+1:proposal==1 ? doc.records:proposal-1;
  if(e==BTN_PLAY){record=(uint16_t)proposal;page=VIEW;}
 }else if(page==PICK_FAMILY || page==PICK_SPEED){
  if(delta){unsigned count=page==PICK_FAMILY ? 7:4;
   for(unsigned tries=0;tries<count;++tries){proposal=delta>0 ? (proposal+1)%count:(proposal+count-1)%count;if(proposed_mode()!=SC_NONE)break;}
  }
  if(e==BTN_PLAY){pending=proposed_mode();
   if(page==PICK_FAMILY && proposal<4){
    draft_family=proposal;proposal=speed_index(pending);page=PICK_SPEED;
   }else accept_profile();
  }
 }
 else if(page==DISCARD){if(e==BTN_PLAY)active=false;}
 else if(page==FAILURE){if(e==BTN_PLAY && doc.recovery)page=RECOVER_CONFIRM;}
 else if(page==RECOVER_CONFIRM){if(e==BTN_PLAY)(void)start_job(2);}
}
void sc_editor_render(ui_model_t *model,uint32_t now){
 for(unsigned i=0;i<8;++i){rows[i][0]=0;model->line[i]=rows[i];}
 strcpy(rows[0],"SIDECAR EDIT");
 if(sc_editor_busy()){strcpy(rows[3],operation==1 ? "SAVING...":"SD WORKING...");return;}
 const char *leaf=strrchr(doc.source,'/');leaf=leaf ? leaf+1:doc.source;
 size_t offset=ui_marquee_offset(&file_scroll,leaf,21,now,record);
 snprintf(rows[1],sizeof(rows[1]),"%.21s",leaf+offset);
 if(!available_now){strcpy(rows[3],"EDITOR UNAVAILABLE");strcpy(rows[4],"CHECK SD / TRANSPORT");strcpy(rows[7],UI_KEY_STOP " CANCEL");return;}
 if(page==VIEW || page==PICK_RECORD){
  const char *title=metadata_record!=sc_editor_preview_record() ? "LOADING NAME...":
      metadata.confidence==PROGRAM_INVALID ? "INVALID RECORD":
      metadata.valid&PROGRAM_TITLE ? metadata.title:"NO PROGRAM TITLE";
  offset=ui_marquee_offset(&title_scroll,title,21,now,sc_editor_preview_record());
  snprintf(rows[3],sizeof(rows[3]),"%.21s",title+offset);
 }
 if(page==VIEW){
  snprintf(rows[2],sizeof(rows[2]),"%cREC %u/%u %s",cursor==0 ? '}':' ',record,doc.records,current()==SC_NONE ? "NO PROFILE":doc.existed ? "EXISTING":"NEW");
  snprintf(rows[4],sizeof(rows[4]),"%cLOADER: %s %s",cursor==1 ? '}':' ',family(current()),speed(current()));
  snprintf(rows[5],sizeof(rows[5]),"%c" UI_KEY_PLAY " SAVE  " UI_KEY_STOP " CANCEL",cursor==2 ? '}':' ');
 }else if(page==PICK_RECORD){strcpy(rows[2],"SELECT RECORD");snprintf(rows[4],sizeof(rows[4]),"%u / %u",proposal,doc.records);strcpy(rows[7],UI_HELP_OK_CANCEL);}
 else if(page==PICK_FAMILY || page==PICK_SPEED){strcpy(rows[2],page==PICK_FAMILY ? "CHOOSE LOADER":"CHOOSE SPEED");
  uint8_t mode=proposed_mode();
  if(page==PICK_FAMILY)snprintf(rows[4],sizeof(rows[4]),"%s",family(mode));
  else snprintf(rows[4],sizeof(rows[4]),"%s %s",family(mode),speed(mode));
  strcpy(rows[5],"USED IN AUTO MODE");strcpy(rows[7],UI_HELP_OK_CANCEL);
 }else if(page==DISCARD){strcpy(rows[2],"DISCARD CHANGES?");snprintf(rows[4],sizeof(rows[4]),"%u UNSAVED RECORDS",sc_changes(&doc));strcpy(rows[7],UI_KEY_PLAY " DISCARD  " UI_KEY_STOP " BACK");}
 else if(page==FAILURE){strcpy(rows[2],"INVALID / SD ERROR");snprintf(rows[4],sizeof(rows[4]),"%s",doc.error ? doc.error:"ERROR");strcpy(rows[7],doc.recovery ? UI_KEY_PLAY " RECOVER  " UI_KEY_STOP " BACK":UI_KEY_STOP " CANCEL");}
 else if(page==RECOVER_CONFIRM){strcpy(rows[2],"CONFIRM RECOVERY");strcpy(rows[3],"KEEP VERIFIED NEW");strcpy(rows[4],"OR RESTORE BACKUP");strcpy(rows[5],"RELOADS SD PROFILES");strcpy(rows[7],UI_KEY_PLAY " RECOVER  " UI_KEY_STOP " CANCEL");}
}
