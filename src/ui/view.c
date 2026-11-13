/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui/view.h"
#include "ui/policy.h"
#include "drivers/oled.h"
#include "formats/mz_tape_profiles.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
static enum {TEXT,BROWSER,PLAY,RECORD} mode;
static ui_transport_view_t transport;
static const char *rows[6];
static int selected;
static unsigned directories,item,total;
static bool external;
static ui_marquee_t marquee;
static ui_headroom_t recovery;
static char overlay[48];
static uint32_t overlay_started,overlay_duration;
static unsigned overlay_priority;
static uint32_t now_ms(void){return (uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS);
}
void ui_use_text_view(void){if(mode!=TEXT)marquee.valid=false;
mode=TEXT;
}
void ui_set_external(bool value){external=value;
}
void ui_set_browser_view(const char *const names[6],int current,unsigned dirs,unsigned index,unsigned count){
 if(mode!=BROWSER)marquee.valid=false;
 mode=BROWSER;
 for(unsigned i=0;i<6;++i)rows[i]=names[i];
 selected=current;
 directories=dirs;
 item=index;
 total=count;
}
void ui_set_transport_view(bool record,const ui_transport_view_t *view){
 if(transport.session!=view->session || mode!=(record ? RECORD:PLAY)){recovery.valid=false;
 marquee.valid=false;
 }
 transport=*view;
 mode=record ? RECORD:PLAY;
 if(record)(void)ui_headroom_update(&recovery,view->rx_free,now_ms());
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
memset(&recovery,0,sizeof(recovery));
memset(&transport,0,sizeof(transport));
overlay[0]=0;
external=false;
}
static void icon(int kind){
 if(kind==1){for(int x=0;x<6;++x)oled_rect(x,3+x/2,1,7-x,false,true);
 }
 else if(kind==2){oled_rect(0,1,2,7,true,true);
 oled_rect(4,1,2,7,true,true);
 }
 else if(kind==3){oled_rect(1,2,5,5,true,true);
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
static void draw_transport(uint32_t now){
 const ui_transport_view_t *v=&transport;
 const program_metadata_t *m=&v->program;
 char line[48],load[8],exec[16];
 const char *state=external ? "EXTERNAL":v->status ? v->status:"";
 icon(strstr(state,"PAUSE") || strstr(state,"WAIT") ? 2:mode==RECORD ? 3:1);
 char caption[16];
 if(v->playback_baud)snprintf(caption,sizeof(caption),"TAP %uBD",v->playback_baud);
 else if(m->format==FILE_FORMAT_WAV && v->sample_rate)snprintf(caption,sizeof(caption),"WAV %luK",(unsigned long)(v->sample_rate/1000));
 else snprintf(caption,sizeof(caption),"%s",format(m->format));
 if(m->records)snprintf(line,sizeof(line),"%s %s %lu/%lu",state,caption,(unsigned long)m->record,(unsigned long)m->records);
 else if(m->record)snprintf(line,sizeof(line),"%s %s #%lu",state,caption,(unsigned long)m->record);
 else snprintf(line,sizeof(line),"%s %s",state,caption);
 oled_text_xy(9,1,line,0,true);
 oled_rect(0,8,128,1,true,true);
 const char *title=(m->valid&PROGRAM_TITLE) ? m->title:v->filename ? v->filename:"DETECTING...";
 if(m->confidence==PROGRAM_LOADER_ONLY)title="LOADER / WAIT PROGRAM";
 size_t offset=ui_marquee_offset(&marquee,title,21,now,(uint32_t)mode^v->session^m->record);
 oled_text_xy(0,10,title+offset,1,true);
 const char *type=program_metadata_type(m);
 char rawtype[12];
 if(!strcmp(type,"TYPE")){snprintf(rawtype,sizeof(rawtype),"TYPE %02X",m->file_type);
 type=rawtype;
 }
 const char *loader="--";
 if(mode==PLAY && (m->valid&PROGRAM_TRANSFER_MODE))loader=loader_mode_name(m->transfer_loader_mode);
 else if(m->valid&PROGRAM_RECORDED_MODE)loader=loader_mode_name(m->recorded_loader_mode);
 bool transfer=mode==PLAY && (m->valid&PROGRAM_TRANSFER_MODE);
 snprintf(line,sizeof(line),"%s %s %s",type,transfer ? "TX":"SRC",loader);
 if(transfer && (m->valid&PROGRAM_RECORDED_MODE))snprintf(line,sizeof(line),"%s TX %s SRC %s",type,loader,loader_mode_name(m->recorded_loader_mode));
 if(m->confidence==PROGRAM_WAITING)snprintf(line,sizeof(line),"DETECTING... / FILE");
 else if(m->confidence==PROGRAM_UNKNOWN)snprintf(line,sizeof(line),"UNKNOWN / FILENAME");
 else if(m->confidence==PROGRAM_LOADER_ONLY)snprintf(line,sizeof(line),"LOADER ONLY / WAIT PROGRAM");
 else if(m->confidence==PROGRAM_INVALID)snprintf(line,sizeof(line),"INVALID HEADER / DATA");
 oled_text_xy(0,20,line,0,true);
 if(m->valid&PROGRAM_LOAD)snprintf(load,sizeof(load),"%04X",m->load);
 else strcpy(load,"--");
 if(m->valid&PROGRAM_AUTOSTART)snprintf(exec,sizeof(exec),"AUTO %u",m->autostart);
 else if(m->valid&PROGRAM_EXEC)snprintf(exec,sizeof(exec),"EXEC %04X",m->exec);
 else strcpy(exec,"EXEC --");
 if(m->valid&PROGRAM_SIZE)snprintf(line,sizeof(line),"SIZE %lu B  LOAD %s",(unsigned long)m->declared_size,load);
 else snprintf(line,sizeof(line),"SIZE --  LOAD %s",load);
 oled_text_xy(0,27,line,0,true);
 snprintf(line,sizeof(line),"%s  %02lu:%02lu",exec,(unsigned long)(v->seconds/60),(unsigned long)(v->seconds%60));
 oled_text_xy(0,34,line,0,true);
 unsigned progress=v->progress;
 if(mode==RECORD && (m->valid&PROGRAM_SIZE))progress=m->declared_size ? m->received_bytes*100U/m->declared_size:100;
 oled_text_xy(0,41,mode==RECORD ? "D":"P",0,true);
 oled_bar(8,42,88,5,progress);
 snprintf(line,sizeof(line),"%u%%",progress>100 ? 100:progress);
 oled_text_xy(100,41,line,0,true);
 unsigned reserve=0;
 ui_buffer_alarm_t alarm=UI_BUFFER_OK;
 if(mode==RECORD){
  reserve=recovery.shown;
  alarm=v->record_active ? ui_buffer_alarm(v->rx_free):UI_BUFFER_OK;
  snprintf(line,sizeof(line),"RX FREE %u%% MIN %u%%",v->rx_free,v->rx_min);
 }
else if(!v->tx_expected && strstr(state,"UL")){strcpy(line,"UL HANDSHAKE");
 }
 else if(v->tx.time_valid){
  reserve=v->tx.reserve_us>=100000 ? 100:(unsigned)(v->tx.reserve_us/1000);
  if(v->tx_expected)alarm=ui_buffer_alarm((uint8_t)reserve);
  snprintf(line,sizeof(line),"TX %lu ms SW %lu%%",(unsigned long)(v->tx.reserve_us/1000),
    v->tx.fifo_capacity ? (unsigned long)(v->tx.fifo_used*100U/v->tx.fifo_capacity):0UL);
 }
else {reserve=v->tx.fifo_capacity ? v->tx.fifo_used*100U/v->tx.fifo_capacity:0;
 snprintf(line,sizeof(line),"TX SW FIFO %u%%",reserve);
 }
 oled_text_xy(0,49,line,0,true);
 oled_bar(0,57,128,6,reserve);
 if(alarm!=UI_BUFFER_OK){const char *label=alarm==UI_BUFFER_BAD ? "BAD":alarm==UI_BUFFER_CRITICAL ? "CRIT":"LOW";
 oled_rect(108,48,20,7,true,false);
 oled_text_xy(108,49,label,0,true);
 }
 if(v->warning && v->warning[0]){oled_rect(0,33,128,7,true,false);
 oled_text_xy(0,34,v->warning,0,true);
 }
}
void ui_view_draw(const ui_model_t *model,uint32_t now){
 if(mode==BROWSER){
  oled_text_xy(0,0,model->line[0],1,true);
  oled_rect(0,8,128,1,true,true);
  for(unsigned i=0;i<6;++i){int y=9+(int)i*8;
  bool active=(int)i==selected;
   if(active)oled_rect(0,y,125,8,true,true);
   const char *name=rows[i] ? rows[i]:"";
   size_t offset=active ? ui_marquee_offset(&marquee,name,19,now,item^0x100000U):0;
   if(directories&(1U<<i))oled_rect(1,y+2,5,4,false,!active);
   char visible[20];
   snprintf(visible,sizeof(visible),"%.19s",name+offset);
   oled_text_xy(7,y,visible,1,!active);
  }
  char status[32];
  snprintf(status,sizeof(status),"%u/%u%s",total ? item+1:0,total,external ? " EXTERNAL":"");
  oled_text_xy(0,58,status,0,true);
  if(total>6){int top=10+(int)((uint64_t)item*43/(total-1));
  oled_rect(126,9,2,47,false,true);
  oled_rect(126,top,2,4,true,true);
  }
 }
else if(mode==PLAY || mode==RECORD)draw_transport(now);
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
  if(n>21)oled_text_xy(1,55,message+21,0,true);
 }
}
