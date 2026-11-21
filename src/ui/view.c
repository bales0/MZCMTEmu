/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui/view.h"
#include "ui/policy.h"
#include "ui/key_icons.h"
#include "drivers/oled.h"
#include "formats/mz_tape_profiles.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
static enum {TEXT,BROWSER,PLAY,RECORD,ANALYSIS} mode;
static ui_transport_view_t transport;
static const char *rows[FILE_LIST_VIEW_ROWS];
static int selected;
static unsigned directories,item,total;
static bool external,browser_has_info;
static unsigned analysis_phase;
static ui_marquee_t marquee,details_marquee;
static ui_headroom_t recovery;
/* Estimate from decoded payload deltas, excluding the preceding header. */
static struct {bool valid,estimated;uint32_t size,record,bytes,seconds,last_bytes,total;} record_eta;
static void update_record_eta(const ui_transport_view_t *v){
 const program_metadata_t *m=&v->program;
 if(!(m->valid&PROGRAM_SIZE) || !m->declared_size || m->confidence==PROGRAM_INVALID){memset(&record_eta,0,sizeof(record_eta));return;}
 if(!record_eta.valid || record_eta.size!=m->declared_size || record_eta.record!=m->record ||
    m->received_bytes<record_eta.last_bytes || v->seconds<record_eta.seconds){
  memset(&record_eta,0,sizeof(record_eta));record_eta.valid=true;record_eta.size=m->declared_size;record_eta.record=m->record;
  record_eta.bytes=m->received_bytes;record_eta.seconds=v->seconds;
 }
 if(m->received_bytes>=m->declared_size){record_eta.total=v->seconds;record_eta.estimated=true;}
 else if(m->received_bytes>record_eta.last_bytes && m->received_bytes>record_eta.bytes && v->seconds>record_eta.seconds){
  uint32_t bytes=m->received_bytes-record_eta.bytes;
  uint64_t estimate=((uint64_t)(m->declared_size-m->received_bytes)*(v->seconds-record_eta.seconds)+bytes-1)/bytes;
  estimate+=v->seconds;record_eta.total=estimate>UINT32_MAX ? UINT32_MAX:(uint32_t)estimate;record_eta.estimated=true;
 }
 record_eta.last_bytes=m->received_bytes;
}
static char overlay[48];
static uint32_t overlay_started,overlay_duration;
static unsigned overlay_priority;
static uint32_t now_ms(void){return (uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS);
}
void ui_use_text_view(void){if(mode!=TEXT)marquee.valid=false;
mode=TEXT;
}
void ui_set_analysis_view(unsigned phase){
 if(mode!=ANALYSIS)marquee.valid=false;
 mode=ANALYSIS;analysis_phase=phase&7U;
}
void ui_set_external(bool value){external=value;
}
void ui_set_browser_view(const char *const names[FILE_LIST_VIEW_ROWS],int current,unsigned dirs,unsigned index,unsigned count,bool has_info_sidecar){
 if(mode!=BROWSER)marquee.valid=false;
 mode=BROWSER;
 for(unsigned i=0;i<FILE_LIST_VIEW_ROWS;++i)rows[i]=names[i];
 selected=current;
 directories=dirs;
 item=index;
 total=count;browser_has_info=has_info_sidecar;
}
void ui_set_transport_view(bool record,const ui_transport_view_t *view){
 if(transport.session!=view->session || mode!=(record ? RECORD:PLAY)){recovery.valid=false;memset(&record_eta,0,sizeof(record_eta));
 marquee.valid=false;details_marquee.valid=false;
 }
 transport=*view;
 mode=record ? RECORD:PLAY;
 if(record){(void)ui_headroom_update(&recovery,view->rx_free,now_ms());update_record_eta(view);}
}
void ui_overlay_show(const char *text,uint32_t duration,unsigned priority){
 uint32_t now=now_ms();
 (void)ui_overlay_current(now);
 if(overlay[0] && priority<overlay_priority)return;
 snprintf(overlay,sizeof(overlay),"%s",text ? text:"");
 overlay_started=now;
 overlay_duration=duration;
 overlay_priority=priority;
}
void ui_overlay_clear(bool acknowledge){if(acknowledge || overlay_priority<3)overlay[0]=0;
}
const char *ui_overlay_current(uint32_t now){if(overlay[0] && overlay_duration && (uint32_t)(now-overlay_started)>=overlay_duration)overlay[0]=0;
return overlay[0] ? overlay:NULL;
}
void ui_view_reset(void){mode=TEXT;
memset(&marquee,0,sizeof(marquee));
memset(&details_marquee,0,sizeof(details_marquee));
memset(&recovery,0,sizeof(recovery));
memset(&record_eta,0,sizeof(record_eta));
memset(&transport,0,sizeof(transport));
overlay[0]=0;
external=false;browser_has_info=false;analysis_phase=0;
}
static void icon(int kind){
 if(kind==1){const uint8_t width[]={1,3,5,6,5,3,1};
  for(int y=0;y<7;++y)oled_rect(0,1+y,width[y],1,true,true);
 }
 else if(kind==2){oled_rect(0,1,2,7,true,true);
 oled_rect(4,1,2,7,true,true);
 }
 else if(kind==3){const uint8_t width[]={3,5,7,7,7,5,3};
  for(int y=0;y<7;++y)oled_rect((7-width[y])/2,1+y,width[y],1,true,true);
 }
 else if(kind==4){oled_rect(1,2,5,5,true,true);
 }
 else {oled_rect(0,0,7,8,false,true);
 oled_pixel(3,2,true);
 oled_pixel(3,3,true);
 oled_pixel(3,6,true);
 }
}
static const char *format(file_format_t f){switch(f){case FILE_FORMAT_MZF:return "MZF";
case FILE_FORMAT_MZT:return "MZT";
case FILE_FORMAT_M12:return "M12";
case FILE_FORMAT_MZQ:return "QD";
case FILE_FORMAT_WAV:return "WAV";
case FILE_FORMAT_LEP:return "LEP";
case FILE_FORMAT_L16:return "L16";
case FILE_FORMAT_TAP:return "TAP";
default:return "--";
}}
/* Both browser and QD reserve the rightmost cells for their index. */
static void context_header(int x,const char *label,unsigned index,unsigned count,bool path,bool has_info){
 char number[24],text[32];
 snprintf(number,sizeof(number),external ? "E %u%c%u":"%u%c%u",index,has_info ? 'I':'/',count);
 int cell=6;
 int right=128-(int)strlen(number)*cell;
 unsigned room=right>x+4 ? (unsigned)(right-x-4)/(unsigned)cell:0;
 if(path)ui_compact_path(text,sizeof(text),label,room);
 else snprintf(text,sizeof(text),"%.*s",(int)room,label ? label:"");
 oled_text_xy(x,0,text,1,true);
 oled_text_xy(right,0,number,1,true);
}
/* 128x64 = eight rows of readable 6x8 text. Row 7 is always the footer. */
static void row_text(unsigned row,const char *text){oled_text_xy(0,(int)row*8,text,1,true);}
static const char *compact_loader(loader_mode_t value){
 static const char *const names[]={"N1:1","AUTO","UL","UL800","UL700","700x3","IC1:4","IC1:3","IC1:2","TC1:3","TC1:2","N1:2","N1:3","700x1","N1:4","IC1:1","TC1:1"};
 return (unsigned)value<sizeof(names)/sizeof(names[0]) ? names[value]:"--";
}

void ui_build_edge_analysis(ui_model_t *model,const edge_analysis_t *scan,unsigned percent){
 static char text[8][24];
 for(unsigned i=0;i<8;++i){text[i][0]=0;model->line[i]=text[i];}
 snprintf(text[0],sizeof(text[0]),"COUNTING %u %%",percent>100 ? 100:percent);
 strcpy(text[1],"LENGTH ONLY");
 uint64_t divisor=scan->total_bytes>=1048576 ? 1048576:1024;
 uint64_t tenths=((uint64_t)scan->total_bytes*10+divisor/2)/divisor;
 snprintf(text[2],sizeof(text[2]),"SIZE %lu.%lu %s",(unsigned long)(tenths/10),(unsigned long)(tenths%10),divisor==1048576 ? "MB":"KB");
 strcpy(text[4],"NO CONTENT SCAN");
 if(scan->cache_hit)strcpy(text[5],"LENGTH CACHED");
 strcpy(text[7],UI_HELP_SCAN_SKIP_CANCEL);
}

static void draw_transport(uint32_t now){
 const ui_transport_view_t *v=&transport;
 const program_metadata_t *m=&v->program;
 char line[80],load[8],exec[12],caption[20];
 const char *status=v->status ? v->status:"";
 bool blocked=external && mode==PLAY;
 const char *state=blocked ? "EXTERNAL":status;
 bool qd=mode==PLAY && m->format==FILE_FORMAT_MZQ;
 bool stopped=!blocked && (!strcmp(status,"FINISHED") || !strcmp(status,"SAVED") || !strcmp(status,"CANCELLED") || !strcmp(status,"STOPPED") || !strcmp(status,"SELECT RECORD"));
 bool failed=strstr(state,"ERROR") || strstr(state,"UNDERRUN") || blocked;
 int symbol=stopped ? 4:failed ? 0:strstr(state,"PAUSE") || strstr(state,"WAIT") ? 2:mode==RECORD ? 3:1;
 icon(symbol);
 {
  if(v->playback_baud)snprintf(caption,sizeof(caption),"TAP %u",v->playback_baud);
  else if(m->format==FILE_FORMAT_WAV && v->sample_rate)snprintf(caption,sizeof(caption),"WAV %luK",(unsigned long)(v->sample_rate/1000));
  else snprintf(caption,sizeof(caption),"%s",format(m->format));
  if(mode==RECORD && external){size_t n=strlen(caption);snprintf(caption+n,sizeof(caption)-n," EXT");}
  const char *header_state=!strcmp(state,"BETWEEN BLOCKS") ? "WAIT":state;
  if(!strcmp(state,"PLAY") || (qd && !strcmp(state,"SELECT RECORD")) || (mode==RECORD && !strcmp(state,"REC")))snprintf(line,sizeof(line),"%s",caption);
  else snprintf(line,sizeof(line),"%s %s",header_state,caption);
  if(m->records)context_header(9,line,m->record,m->records,false,v->has_info_sidecar);
  else oled_text_xy(9,0,line,1,true);
 }
 const char *title=(m->valid&PROGRAM_TITLE) ? m->title:v->filename ? v->filename:"DETECTING...";
 if(m->confidence==PROGRAM_LOADER_ONLY)title="LOADER / WAIT PROGRAM";
 uint32_t context=(uint32_t)mode^v->session^m->record;
 size_t offset=ui_marquee_offset(&marquee,title,21,now,context);
 row_text(1,title+offset);
 const char *type=program_metadata_type(m);char rawtype[12];
 if(!strcmp(type,"TYPE")){snprintf(rawtype,sizeof(rawtype),"TYPE %02X",m->file_type);type=rawtype;}
 const char *loader="--";
 bool transfer=mode==PLAY && (m->valid&PROGRAM_TRANSFER_MODE);
 if(transfer)loader=compact_loader(m->transfer_loader_mode);
 else if(m->valid&PROGRAM_RECORDED_MODE)loader=compact_loader(m->recorded_loader_mode);
 if(transfer && (m->valid&PROGRAM_RECORDED_MODE) && m->transfer_loader_mode!=m->recorded_loader_mode)
  snprintf(line,sizeof(line),"%s %s SRC %s",type,loader,compact_loader(m->recorded_loader_mode));
 else snprintf(line,sizeof(line),"%s %s %s",type,transfer ? "TX":"SRC",loader);
 if(m->confidence==PROGRAM_WAITING)strcpy(line,"DETECTING...");
 else if(m->confidence==PROGRAM_UNKNOWN)strcpy(line,"UNKNOWN / FILENAME");
 else if(m->confidence==PROGRAM_LOADER_ONLY)strcpy(line,"LOADER / WAIT DATA");
 else if(m->confidence==PROGRAM_INVALID)strcpy(line,"INVALID HEADER / DATA");
 offset=ui_marquee_offset(&details_marquee,line,21,now,context^0x400000U);
 row_text(2,line+offset);
 if(m->valid&PROGRAM_SIZE)snprintf(line,sizeof(line),"SIZE %lu",(unsigned long)m->declared_size);
 else strcpy(line,"SIZE --");
 row_text(3,line);
 if(m->valid&PROGRAM_LOAD)snprintf(load,sizeof(load),"%04X",m->load);else strcpy(load,"--");
 if(m->valid&PROGRAM_AUTOSTART)snprintf(exec,sizeof(exec),"AUTO %u",m->autostart);
 else if(m->valid&PROGRAM_EXEC)snprintf(exec,sizeof(exec),"EXEC %04X",m->exec);
 else strcpy(exec,"EXEC --");
 snprintf(line,sizeof(line),"LOAD %s %s",load,exec);row_text(4,line);
 ui_buffer_alarm_t alarm=UI_BUFFER_OK;
 unsigned fill=v->tx.fifo_capacity ? (unsigned)((uint64_t)v->tx.fifo_used*100/v->tx.fifo_capacity):0;
 if(fill>100)fill=100;
 if(mode==RECORD){if(v->record_active)alarm=ui_buffer_alarm(v->rx_free);}
 else if(v->tx_expected){unsigned reserve=v->tx.time_valid ? (v->tx.reserve_us>=100000 ? 100:(unsigned)(v->tx.reserve_us/1000)):fill;
  alarm=ui_buffer_alarm((uint8_t)reserve);
 }
 /* Transport state is in the shared header; row 5 is for warnings. */
 if(mode==RECORD)snprintf(line,sizeof(line),"MIN %3u%%",v->rx_min);
 else snprintf(line,sizeof(line),"%s",v->preview_metadata ? "PREVIEW METADATA":"");
 if(v->warning && v->warning[0])row_text(5,v->warning);
 else {
  if(alarm!=UI_BUFFER_OK){const char *label=alarm==UI_BUFFER_BAD ? "BAD":alarm==UI_BUFFER_CRITICAL ? "CRIT":"LOW";
   char clipped[17];snprintf(clipped,sizeof(clipped),"%.15s",line);row_text(5,clipped);
   oled_text_xy(128-(int)strlen(label)*6,40,label,1,true);
  }else row_text(5,line);
 }
 unsigned progress=v->progress;
 if(v->seek_preview && v->preview_metadata)row_text(6,"INDEX PREVIEW");
 /* The QD selector is a minibrowser, without transfer statistics. */
 if(qd && !strcmp(status,"SELECT RECORD"))return;
 /* Shared footer: time/progress at left, fixed BUF at x=80. RX uses free space. */
 {
  char buffer[16];
  if(mode==RECORD)snprintf(buffer,sizeof(buffer),"BUF %3u%%",v->rx_free);
  else if(strstr(state,"UL") || !v->tx.fifo_capacity)strcpy(buffer,"BUF  -- ");
  else snprintf(buffer,sizeof(buffer),"BUF %3u%%",fill);
  if(mode==RECORD){
   if(record_eta.estimated){
    uint32_t left=record_eta.total<v->seconds ? v->seconds:record_eta.total;
    if(v->seconds<6000 && left<6000)snprintf(line,sizeof(line),"%02lu:%02lu/~%02lu:%02lu",(unsigned long)(v->seconds/60),(unsigned long)(v->seconds%60),(unsigned long)(left/60),(unsigned long)(left%60));
    else snprintf(line,sizeof(line),"%luh%02lu/~%luh%02lu",(unsigned long)(v->seconds/3600),(unsigned long)(v->seconds/60%60),(unsigned long)(left/3600),(unsigned long)(left/60%60));
   }else snprintf(line,sizeof(line),"%02lu:%02lu",(unsigned long)(v->seconds/60),(unsigned long)(v->seconds%60));
  }
  else if(v->total_seconds){
   uint32_t total=v->total_seconds;
   if(v->seconds<6000 && total<6000)
    snprintf(line,sizeof(line),"%02lu:%02lu/%02lu:%02lu",(unsigned long)(v->seconds/60),(unsigned long)(v->seconds%60),(unsigned long)(total/60),(unsigned long)(total%60));
   else /* Keep long recordings within the 13-character left field. */
    snprintf(line,sizeof(line),"%luh%02lu/%luh%02lu",(unsigned long)(v->seconds/3600),(unsigned long)(v->seconds/60%60),(unsigned long)(total/3600),(unsigned long)(total/60%60));
  }else snprintf(line,sizeof(line),"%u %%",progress>100 ? 100:progress);
  char left[14];snprintf(left,sizeof(left),"%.13s",line);
  row_text(7,left);oled_text_xy(80,56,buffer,1,true);
 }

}
void ui_view_draw(const ui_model_t *model,uint32_t now){
 if(mode==BROWSER){
  context_header(0,model->line[0],total ? item+1:0,total,true,browser_has_info);
  for(unsigned i=0;i<FILE_LIST_VIEW_ROWS;++i){int y=8+(int)i*8;
   bool active=(int)i==selected;
   if(active)oled_rect(0,y,125,8,true,true);
   const char *name=rows[i] ? rows[i]:"";
   size_t offset=active ? ui_marquee_offset(&marquee,name,19,now,item^0x100000U):0;
   if(directories&(1U<<i))oled_rect(1,y+2,5,4,false,!active);
   char visible[20];snprintf(visible,sizeof(visible),"%.19s",name+offset);
   oled_text_xy(7,y,visible,1,!active);
  }
  if(total>FILE_LIST_VIEW_ROWS){
   int top=9+(int)((uint64_t)item*50/(total-1));
   oled_rect(126,8,2,56,false,true);oled_rect(126,top,2,4,true,true);
  }
 }
else if(mode==PLAY || mode==RECORD)draw_transport(now);
 else if(mode==ANALYSIS){
  for(unsigned row=0;row<8;++row)oled_text_xy(0,(int)row*8,model->line[row],1,true);
  static const int8_t point[8][2]={{0,-3},{2,-2},{3,0},{2,2},{0,3},{-2,2},{-3,0},{-2,-2}};
  for(unsigned i=0;i<8;++i)oled_pixel(112+point[i][0],4+point[i][1],true);
  oled_rect(111+point[analysis_phase][0],3+point[analysis_phase][1],3,3,true,true);
 }
 else for(unsigned row=0;row<8;++row)oled_text_xy(0,(int)row*8,model->line[row],1,true);
 const char *message=ui_overlay_current(now);
 if(message){
  oled_rect(0,44,128,20,true,false);
  oled_rect(0,44,128,20,false,true);
  char first[22];
  size_t n=strlen(message);
  size_t a=n<21 ? n:21;
  memcpy(first,message,a);
  first[a]=0;
  oled_text_xy(1,46,first,1,true);
  if(n>21)oled_text_xy(1,54,message+21,1,true);
 }
}
