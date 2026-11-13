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
static void graphics(void){
 ui_model_t model={{"/RECORDINGS","","","","","","",""}};
 const char *names[]={"FIRST.MZF","SELECTED_LONG_FILENAME.MZT","DIR","FOURTH.TAP","FIFTH.WAV","LAST.L16"};
 ui_set_browser_view(names,1,4,1,10000);
 tick+=50;
 ui_render(&model);
 assert(pixel(124,17) && !pixel(124,9));
 artifact("browser");
 ui_transport_view_t view={0};
 view.status="REC";
 view.filename="DIZZY 5";
 view.program.format=FILE_FORMAT_WAV;
 view.program.protocol=PROGRAM_SHARP;
 view.program.confidence=PROGRAM_HEADER_VALID;view.sample_rate=44100;
 view.program.valid=PROGRAM_TYPE|PROGRAM_SIZE|PROGRAM_TITLE|PROGRAM_LOAD|PROGRAM_EXEC|PROGRAM_RECORDED_MODE;
 strcpy(view.program.title,"DIZZY 5");
 view.program.file_type=1;
 view.program.declared_size=100;
 view.program.received_bytes=20;
 view.program.load=view.program.exec=0x1200;
 view.program.recorded_loader_mode=LOADER_MODE_IC_1_4;
 view.rx_free=80;
 view.rx_min=40;
 view.record_active=true;
 view.session=1;
 view.seconds=38;
 ui_set_transport_view(true,&view);
 tick+=50;
 ui_render(&model);
 assert(!pixel(40,44) && pixel(40,59));
 artifact("record");
 view.rx_free=20;
 ui_set_transport_view(true,&view);
 tick+=50;
 ui_render(&model);
 assert(!pixel(40,59));
 view.rx_free=80;
 ui_set_transport_view(true,&view);
 tick+=50;
 ui_render(&model);
 assert(!pixel(40,59));
 view.status="PLAY";
 view.record_active=false;
 view.tx_expected=true;
 view.tx=(raw_pulse_status_t){.fifo_used=3000,.fifo_capacity=4095,.reserve_us=82000,.time_valid=true,.running=true};
 view.program.valid|=PROGRAM_TRANSFER_MODE;
 view.program.transfer_loader_mode=LOADER_MODE_TC_1_3;
 view.progress=56;
 ui_set_transport_view(false,&view);
 tick+=50;
 ui_render(&model);
 artifact("play");
 ui_overlay_show("MOTOR",0,1);
 assert(!strcmp(ui_overlay_current((uint32_t)tick),"MOTOR"));
 tick+=1000;
 assert(ui_overlay_current((uint32_t)tick));
 ui_overlay_show("EDGE LOSS",0,3);
 ui_overlay_show("MOTOR",1000,1);
 assert(!strcmp(ui_overlay_current((uint32_t)tick),"EDGE LOSS"));
 ui_overlay_clear(false);
 assert(ui_overlay_current((uint32_t)tick));
 ui_render(&model);
 artifact("overlay");
 ui_overlay_clear(true);
 assert(!ui_overlay_current((uint32_t)tick));
 tick=(TickType_t)-50;
 ui_overlay_show("CANCELLED",100,1);
 tick+=70;
 assert(ui_overlay_current((uint32_t)tick));
 tick+=31;
 assert(!ui_overlay_current((uint32_t)tick));
 puts("PASS pixel browser inverse selection, separate program/RX/TX bars, immediate RX drop, overlay priority/latch/expiry and wrap; framebuffer previews exported");
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
