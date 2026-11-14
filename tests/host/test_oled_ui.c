#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "drivers/oled.h"
#include "drivers/i2c.h"
#include "ui/ui.h"
#include "ui/view.h"
#include "freertos/task.h"
static TickType_t tick;
static unsigned calls,fail_at,mode_seen,page_count;
static uint8_t previous,last_page;
TickType_t xTaskGetTickCount(void){return tick;
}
void vTaskDelay(TickType_t delay){tick+=delay;
}
esp_err_t i2c_write_to_device(uint8_t address,const uint8_t *bytes,size_t n,uint32_t timeout){
 assert(address==0x3C && timeout==10 && bytes && n);
 ++calls;
 if(fail_at==calls)return -1;
 if(n==2 && bytes[0]==0){if(previous==0x20){assert(bytes[1]==2);
 ++mode_seen;
 }
previous=bytes[1];
 }
 else if(n==4){assert(bytes[0]==0 && (bytes[1]&0xF8)==0xB0 && bytes[2]==0 && bytes[3]==0x10);
 last_page=bytes[1]&7;
 ++page_count;
 }
 else if(n==129){assert(bytes[0]==0x40);
 }
 else if(n==3){assert(bytes[0]==0 && bytes[1]==0x81);
 }
 else assert(!"Unexpected OLED packet");
 return ESP_OK;
}
static bool pixel(int x,int y){return (oled_framebuffer()[(y/8)*128+x]&(1U<<(y%8)))!=0;
}
static void artifact(const char *name){
#ifdef UI_RENDER_DIR
 char path[1024];
 snprintf(path,sizeof(path),"%s/%s.pbm",UI_RENDER_DIR,name);
 FILE *f=fopen(path,"wb");
 assert(f);
 fprintf(f,"P4\n128 64\n");
 for(int y=0;y<64;++y)for(int x=0;x<128;x+=8){uint8_t bits=0;
 for(unsigned b=0;b<8;++b)if(pixel(x+(int)b,y))bits|=(uint8_t)(0x80U>>b);
 assert(fputc(bits,f)!=EOF);
 }
assert(!fclose(f));
#else
 (void)name;
#endif
}
/* Compare a header field with glyphs at the requested physical coordinates. */
static void expect_text(int x,int y,const char *text,int size){
 uint8_t original[128],expected[128],restore[128];
 unsigned page=(unsigned)y/8,local=(unsigned)y%8;
 memcpy(original,oled_framebuffer()+page*128,sizeof(original));
 oled_canvas_begin((uint8_t)page,expected);oled_text_xy(x,y,text,size,true);
 unsigned height=size ? 8:6,width=(unsigned)strlen(text)*(size ? 6:4);
 assert(local+height<=8 && x+(int)width<=128);
 unsigned mask=((1U<<height)-1U)<<local;
 for(unsigned i=0;i<width;++i)assert((original[x+i]&mask)==(expected[x+i]&mask));
 oled_canvas_end();oled_canvas_begin((uint8_t)page,restore);memcpy(restore,original,sizeof(restore));oled_canvas_end();
}
static unsigned count_text(const char *text){
 uint8_t original[128],glyph[128],restore[128];
 memcpy(original,oled_framebuffer(),128);oled_canvas_begin(0,glyph);oled_text_xy(0,0,text,1,true);oled_canvas_end();
 oled_canvas_begin(0,restore);memcpy(restore,original,128);oled_canvas_end();
 unsigned found=0,width=(unsigned)strlen(text)*6;
 for(unsigned page=0;page<8;++page)for(unsigned x=0;x+width<=128;++x)
  if(!memcmp(oled_framebuffer()+page*128+x,glyph,width))++found;
 return found;
}
static void expect_stop_icon(void){for(int y=2;y<=6;++y)for(int x=1;x<=5;++x)assert(pixel(x,y));}
static void graphics(void){
 ui_model_t model={{"/RECORDINGS","","","","","","",""}};
 const char *names[]={"FIRST.MZF","SELECTED_LONG_FILENAME.MZT","DIR","FOURTH.TAP","FIFTH.WAV","SIXTH.L16","SEVENTH.MZF"};
 ui_set_browser_view(names,1,4,1,10000,false);tick+=50;ui_render(&model);
 assert(pixel(124,16) && !pixel(124,8));artifact("browser");
 ui_set_browser_view(names,6,4,9999,10000,false);tick+=50;ui_render(&model);
 assert(pixel(124,56) && pixel(124,63) && !pixel(124,48));artifact("browser-last");
 ui_set_browser_view(names,3,4,3,15,false);tick+=50;ui_render(&model);
 expect_text(104,0,"4/15",1);expect_text(0,0,"/RECORDINGS",1);artifact("browser-centered");
 model.line[0]="/sd/very/long/directory/RECORDINGS";
 ui_set_browser_view(names,3,4,65534,65535,false);tick+=50;ui_render(&model);
 expect_text(62,0,"65535/65535",1);expect_text(0,0,"../RDINGS",1);artifact("browser-long-path");
 model.line[0]="/RECORDINGS";
 ui_set_browser_view(names,1,4,1,15,true);tick+=50;ui_render(&model);
 expect_text(104,0,"2I15",1);artifact("browser-sidecar");
 ui_transport_view_t qd={0};qd.filename="MULTI.QD";qd.status="SELECT RECORD";qd.session=2;
 qd.program.format=FILE_FORMAT_MZQ;qd.program.protocol=PROGRAM_SHARP;qd.program.record=2;qd.program.records=3;
 qd.program.confidence=PROGRAM_HEADER_VALID;qd.program.valid=PROGRAM_TITLE;strcpy(qd.program.title,"SECOND PROGRAM");
 ui_set_transport_view(false,&qd);tick+=50;ui_render(&model);
 expect_text(9,0,"MULTI.QD",1);expect_text(110,0,"2/3",1);expect_text(0,40,"SELECT RECORD",1);
 expect_stop_icon();artifact("qd-selector");
 qd.status="PLAY";qd.progress=35;qd.tx_expected=true;
 qd.tx=(raw_pulse_status_t){.fifo_used=3000,.fifo_capacity=4095,.reserve_us=82000,.time_valid=true,.running=true};
 ui_set_transport_view(false,&qd);tick+=50;ui_render(&model);
 expect_text(9,0,"MULTI.QD",1);expect_text(110,0,"2/3",1);
 expect_text(0,56,"35 %",1);expect_text(80,56,"BUF  73%",1);assert(!count_text("PLAY"));artifact("qd-play");
 qd.status="PAUSE USER";qd.tx_expected=false;ui_set_transport_view(false,&qd);tick+=50;ui_render(&model);
 expect_text(0,40,"PAUSE USER",1);expect_text(80,56,"BUF  73%",1);artifact("qd-paused");
 qd.status="WAIT MOTOR";ui_set_transport_view(false,&qd);tick+=50;ui_render(&model);
 expect_text(0,40,"WAIT MOTOR",1);artifact("qd-wait");
 ui_transport_view_t view={0};view.status="REC";view.filename="DIZZY 5";view.program.format=FILE_FORMAT_WAV;
 view.program.protocol=PROGRAM_SHARP;view.program.confidence=PROGRAM_HEADER_VALID;view.sample_rate=44100;
 view.program.valid=PROGRAM_TYPE|PROGRAM_SIZE|PROGRAM_TITLE|PROGRAM_LOAD|PROGRAM_EXEC|PROGRAM_RECORDED_MODE;
 strcpy(view.program.title,"DIZZY 5");view.program.file_type=1;view.program.declared_size=100;view.program.received_bytes=20;
 view.program.load=view.program.exec=0x1200;view.program.recorded_loader_mode=LOADER_MODE_IC_1_4;
 view.rx_free=80;view.rx_min=40;view.record_active=true;view.session=1;view.seconds=38;
 ui_set_transport_view(true,&view);tick+=50;ui_render(&model);
 expect_text(0,24,"SIZE 100",1);expect_text(0,32,"LOAD 1200 EXEC 1200",1);
 expect_text(9,0,"WAV 44K",1);assert(!count_text("REC"));
 /* Filled round RECORD spans y=1..7 and has rounded corners; STOP is 5x5. */
 for(int y=0;y<8;++y)for(int x=0;x<7;++x){
  const unsigned widths[]={0,3,5,7,7,7,5,3};
  unsigned width=widths[y];
  assert(pixel(x,y)==(width && x>=(int)(7-width)/2 && x<(int)(7+width)/2));
 }
 assert(!count_text("RX"));
 expect_text(0,56,"00:38",1);expect_text(80,56,"BUF  80%",1);expect_text(0,40,"MIN  40%",1);
 assert(pixel(0,49) && pixel(127,54) && pixel(20,51) && !pixel(40,51));
 for(int x=0;x<128;++x){assert(!pixel(x,48));}
 artifact("record");
 view.rx_free=20;ui_set_transport_view(true,&view);tick+=50;ui_render(&model);
 expect_text(0,56,"00:38",1);expect_text(80,56,"BUF  20%",1);expect_text(0,40,"MIN  40%",1);expect_text(110,40,"LOW",1);artifact("record-low");
 view.rx_free=100;view.rx_min=100;ui_set_transport_view(true,&view);tick+=50;ui_render(&model);
 expect_text(0,56,"00:38",1);expect_text(80,56,"BUF 100%",1);expect_text(0,40,"MIN 100%",1);
 for(unsigned free=0;free<=100;free=free==0 ? 7:free==7 ? 73:free==73 ? 100:101){
  view.rx_free=free;ui_set_transport_view(true,&view);tick+=50;ui_render(&model);
  char expected[16];snprintf(expected,sizeof(expected),"BUF %3u%%",free);expect_text(80,56,expected,1);
 }
 view.record_active=false;view.status="PAUSE USER";ui_set_transport_view(true,&view);tick+=50;ui_render(&model);
 expect_text(9,0,"PAUSE USER WAV 44K",1);expect_text(0,56,"00:38",1);expect_text(0,40,"MIN 100%",1);artifact("record-paused");
 view.status="WAIT MOTOR";ui_set_transport_view(true,&view);tick+=50;ui_render(&model);expect_text(9,0,"WAIT MOTOR WAV 44K",1);
 view.status="SAVING";ui_set_transport_view(true,&view);tick+=50;ui_render(&model);expect_text(9,0,"SAVING WAV 44K",1);artifact("record-saving");
 view.status="SAVED";view.program.received_bytes=100;ui_set_transport_view(true,&view);tick+=50;ui_render(&model);
 expect_text(9,0,"SAVED WAV 44K",1);expect_stop_icon();assert(count_text("SAVED")==1 && pixel(126,51));artifact("record-saved");
 view.status="ERROR";view.warning="EDGE LOSS";ui_set_transport_view(true,&view);tick+=50;ui_render(&model);
 expect_text(9,0,"ERROR WAV 44K",1);expect_text(0,40,"EDGE LOSS",1);expect_text(0,56,"00:38",1);expect_text(80,56,"BUF 100%",1);artifact("record-error");
 view.warning=NULL;
 view.status="PLAY";view.record_active=false;view.tx_expected=true;view.total_seconds=120;
 view.tx=(raw_pulse_status_t){.fifo_used=3000,.fifo_capacity=4095,.reserve_us=82000,.time_valid=true,.running=true};
 view.program.valid|=PROGRAM_TRANSFER_MODE;view.program.transfer_loader_mode=LOADER_MODE_TC_1_3;view.progress=56;
 ui_set_transport_view(false,&view);tick+=50;ui_render(&model);
 expect_text(0,16,"OBJ TC1:3 SRC IC1:4",1);expect_text(0,24,"SIZE 100",1);
 expect_text(0,32,"LOAD 1200 EXEC 1200",1);assert(!count_text("TIME"));
 expect_text(0,56,"00:38/02:00",1);expect_text(80,56,"BUF  73%",1);
 assert(!count_text("PLAY") && pixel(0,49) && pixel(127,54) && pixel(70,51) && !pixel(80,51));
 for(unsigned page=0;page<8;++page){if(page==5)continue;unsigned nonzero=0;for(unsigned x=0;x<128;++x)nonzero|=oled_framebuffer()[page*128+x];assert(nonzero);}
 for(int x=0;x<128;++x){assert(!pixel(x,48));}
 artifact("play");
 int play_top=8,play_bottom=-1,pause_top=8,pause_bottom=-1;
 for(int y=0;y<8;++y)for(int x=0;x<7;++x)if(pixel(x,y)){if(y<play_top)play_top=y;if(y>play_bottom)play_bottom=y;}
 view.status="PAUSE USER";view.tx_expected=false;ui_set_transport_view(false,&view);tick+=50;ui_render(&model);
 for(int y=0;y<8;++y)for(int x=0;x<7;++x)if(pixel(x,y)){if(y<pause_top)pause_top=y;if(y>pause_bottom)pause_bottom=y;}
 assert(play_top==1 && play_bottom==7 && pause_top==play_top && pause_bottom==play_bottom);artifact("play-paused-aligned");
 view.status="FINISHED";view.program.format=FILE_FORMAT_MZF;view.program.record=view.program.records=1;
 view.tx.running=false;view.tx.fifo_used=0;view.tx.reserve_us=0;view.progress=100;view.seconds=120;
 ui_set_transport_view(false,&view);tick+=50;ui_render(&model);
 expect_text(9,0,"FINISHED MZF",1);expect_text(110,0,"1/1",1);expect_stop_icon();assert(count_text("FINISHED")==1);
 assert(!count_text("TIME"));expect_text(0,56,"02:00/02:00",1);expect_text(80,56,"BUF   0%",1);artifact("play-finished");
 view.status="PLAY";view.program.format=FILE_FORMAT_MZT;view.program.record=2;view.program.records=3;
 view.has_info_sidecar=true;view.tx_expected=true;view.tx.running=true;view.tx.fifo_used=3000;view.tx.reserve_us=82000;view.progress=56;
 ui_set_transport_view(false,&view);tick+=50;ui_render(&model);
 expect_text(9,0,"MZT",1);expect_text(110,0,"2I3",1);artifact("play-sidecar");
 view.tx.fifo_used=view.tx.fifo_capacity;view.tx.reserve_us=1000000000ULL;
 ui_set_transport_view(false,&view);tick+=50;ui_render(&model);
 expect_text(0,56,"02:00/02:00",1);expect_text(80,56,"BUF 100%",1);artifact("play-full-buffer");
 for(unsigned fill=0;fill<=100;fill=fill==0 ? 7:fill==7 ? 73:fill==73 ? 100:101){
  view.tx.fifo_capacity=100;view.tx.fifo_used=fill;ui_set_transport_view(false,&view);tick+=50;ui_render(&model);
  char expected[16];snprintf(expected,sizeof(expected),"BUF %3u%%",fill);expect_text(80,56,expected,1);
 }
 view.total_seconds=0;view.program.format=FILE_FORMAT_LEP;view.progress=42;view.seconds=0;
 ui_set_transport_view(false,&view);tick+=50;ui_render(&model);expect_text(0,56,"42 %",1);assert(!count_text("TIME") && !count_text("00:00") && !count_text("TX "));artifact("play-lep-percent");
 view.status="PAUSE USER";ui_set_transport_view(false,&view);tick+=50;ui_render(&model);expect_text(0,56,"42 %",1);
 view.status="FINISHED";view.progress=100;ui_set_transport_view(false,&view);tick+=50;ui_render(&model);expect_text(0,56,"100 %",1);
 view.status="UL HANDSHAKE";ui_set_transport_view(false,&view);tick+=50;ui_render(&model);expect_text(80,56,"BUF  -- ",1);
 view.status="PLAY";view.total_seconds=18000;view.seconds=7200;ui_set_transport_view(false,&view);tick+=50;ui_render(&model);expect_text(0,56,"2h00/5h00",1);
 view.status="ERROR";view.tx_expected=false;view.warning="UL TIMEOUT";
 ui_set_transport_view(false,&view);tick+=50;ui_render(&model);expect_text(0,40,"UL TIMEOUT",1);artifact("play-warning");
 ui_overlay_show("MOTOR",0,1);assert(!strcmp(ui_overlay_current((uint32_t)tick),"MOTOR"));
 tick+=1000;assert(ui_overlay_current((uint32_t)tick));ui_overlay_show("EDGE LOSS",0,3);ui_overlay_show("MOTOR",1000,1);
 assert(!strcmp(ui_overlay_current((uint32_t)tick),"EDGE LOSS"));ui_overlay_clear(false);assert(ui_overlay_current((uint32_t)tick));
 ui_render(&model);artifact("overlay");ui_overlay_clear(true);assert(!ui_overlay_current((uint32_t)tick));
 ui_overlay_show("LONG MESSAGE SECOND LINE READABLE AT 6X8",0,1);tick+=50;ui_render(&model);artifact("overlay-long");ui_overlay_clear(true);
 tick=(TickType_t)-50;ui_overlay_show("CANCELLED",100,1);tick+=70;assert(ui_overlay_current((uint32_t)tick));tick+=31;assert(!ui_overlay_current((uint32_t)tick));
 ui_model_t analysis={{"ANALYZING","","SD / TAPE PREFLIGHT","","STOP TO CANCEL","","",""}};uint8_t frame[8][128];
 for(unsigned phase=0;phase<8;++phase){ui_set_analysis_view(phase);tick+=100;ui_render(&analysis);memcpy(frame[phase],oled_framebuffer(),128);
  unsigned count=0;for(int y=0;y<9;++y)for(int x=108;x<=116;++x)count+=pixel(x,y);assert(count>=9);
  for(unsigned old=0;old<phase;++old)assert(memcmp(frame[phase]+108,frame[old]+108,9));
  char name[32];snprintf(name,sizeof(name),"analysis-%u",phase);artifact(name);
 }
 puts("PASS uniform 6x8 UI: fixed BUF at x80; current/total or percent on physical last row; LEP fallback; long durations; FINISHED once with STOP; QD filename, round RECORD versus square STOP, MIN without RX, unified RECORD layout and state/warning priority, overlays and eight animation phases");
}
int main(void){
 oled_init();
 assert(mode_seen==1 && page_count==8);
 unsigned start=calls;
 assert(!oled_set_brightness(0) && !oled_set_brightness(101) && calls==start);
 assert(oled_set_brightness(10) && oled_set_brightness(100) && calls==start+2);
 ui_init();
 ui_model_t model={{"HEADER","PATH","FILE","PLAY","DATA","TIME","RATE","FOOTER"}};
 start=calls;
 ui_render(&model);
 assert(calls==start+16);
 tick+=50;
 ui_render(&model);
 assert(calls==start+16);
 // unchanged pages cost no bus work
 model.line[3]="PAUSED";
 tick+=50;
 ui_render(&model);
 assert(calls==start+18 && last_page==3);
 model.line[4]="NEW DATA";
 ui_render(&model);
 assert(calls==start+18);
 tick+=49;
 ui_render(&model);
 assert(calls==start+18);
 tick+=1;
 ui_render(&model);
 assert(calls==start+20 && last_page==4);
 /* Failure must not advance the UI cache; the same text is retried. */
 model.line[0]="ERROR";
 tick+=50;
 fail_at=calls+1;
 start=calls;
 ui_render(&model);
 assert(calls==start+1);
 fail_at=0;
 tick+=50;
 ui_render(&model);
 assert(calls==start+3 && last_page==0);
 model.line[7]="123456789012345678901X";
 tick+=50;
 fail_at=calls+2;
 start=calls;
 ui_render(&model);
 assert(calls==start+2);
 fail_at=0;
 tick+=50;
 ui_render(&model);
 assert(calls==start+4 && last_page==7);
 model.line[7]="123456789012345678901Y";
 tick+=50;
 start=calls;
 ui_render(&model);
 assert(calls==start);
 // clipped beyond 21 chars
 /* Full dirty frame aborts on first missing-device timeout. */
 ui_init();
 fail_at=calls+1;
 start=calls;
 ui_render(&model);
 assert(calls==start+1);
 fail_at=0;
 tick+=50;
 ui_render(&model);
 assert(calls==start+17);
 graphics();
 puts("PASS actual OLED/UI: SSD1306 page mode, bounded batched packets, 20Hz dirty refresh, clipped text, no unchanged writes, missing-device timeout and command/data failure retry");
 return 0;
}
