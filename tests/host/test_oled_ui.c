#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "drivers/oled.h"
#include "drivers/i2c.h"
#include "ui/ui.h"
#include "freertos/task.h"
static TickType_t tick;
static unsigned calls,fail_at,mode_seen,page_count;
static uint8_t previous,last_page;
TickType_t xTaskGetTickCount(void){return tick;}
void vTaskDelay(TickType_t delay){tick+=delay;}
esp_err_t i2c_write_to_device(uint8_t address,const uint8_t *bytes,size_t n,uint32_t timeout){
 assert(address==0x3C && timeout==10 && bytes && n);++calls;
 if(fail_at==calls)return -1;
 if(n==2 && bytes[0]==0){if(previous==0x20){assert(bytes[1]==2);++mode_seen;}previous=bytes[1];}
 else if(n==4){assert(bytes[0]==0 && (bytes[1]&0xF8)==0xB0 && bytes[2]==0 && bytes[3]==0x10);last_page=bytes[1]&7;++page_count;}
 else if(n==129){assert(bytes[0]==0x40);}
 else if(n==3){assert(bytes[0]==0 && bytes[1]==0x81);}
 else assert(!"Unexpected OLED packet");
 return ESP_OK;
}
int main(void){
 oled_init();assert(mode_seen==1 && page_count==8);
 unsigned start=calls;assert(!oled_set_brightness(0) && !oled_set_brightness(101) && calls==start);
 assert(oled_set_brightness(10) && oled_set_brightness(100) && calls==start+2);
 ui_init();ui_model_t model={{"HEADER","PATH","FILE","PLAY","DATA","TIME","RATE","FOOTER"}};
 start=calls;ui_render(&model);assert(calls==start+16);
 tick+=50;ui_render(&model);assert(calls==start+16); // unchanged pages cost no bus work
 model.line[3]="PAUSED";ui_render(&model);assert(calls==start+18 && last_page==3);
 model.line[4]="NEW DATA";ui_render(&model);assert(calls==start+18);
 tick+=49;ui_render(&model);assert(calls==start+18);tick+=1;ui_render(&model);assert(calls==start+20 && last_page==4);
 /* Failure must not advance the UI cache; the same text is retried. */
 model.line[0]="ERROR";tick+=50;fail_at=calls+1;start=calls;ui_render(&model);assert(calls==start+1);
 fail_at=0;tick+=50;ui_render(&model);assert(calls==start+3 && last_page==0);
 model.line[7]="123456789012345678901X";tick+=50;fail_at=calls+2;start=calls;ui_render(&model);assert(calls==start+2);
 fail_at=0;tick+=50;ui_render(&model);assert(calls==start+4 && last_page==7);
 model.line[7]="123456789012345678901Y";tick+=50;start=calls;ui_render(&model);assert(calls==start); // clipped beyond 21 chars
 /* Full dirty frame aborts on first missing-device timeout. */
 ui_init();fail_at=calls+1;start=calls;ui_render(&model);assert(calls==start+1);fail_at=0;
 tick+=50;ui_render(&model);assert(calls==start+17);
 puts("PASS actual OLED/UI: SSD1306 page mode, bounded batched packets, 20Hz dirty refresh, clipped text, no unchanged writes, missing-device timeout and command/data failure retry");return 0;
}
