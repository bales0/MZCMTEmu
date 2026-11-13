#include "ui/ui.h"
#include "ui/view.h"
#include "drivers/oled.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
static bool initialized,refresh_started;
static TickType_t last_refresh;
void ui_init(void){ui_view_reset();
oled_force_refresh();
initialized=true;
refresh_started=false;
}
void ui_render(const ui_model_t *model){
 if(!model)return;
 if(!initialized)ui_init();
 TickType_t now=xTaskGetTickCount();
 if(refresh_started && (TickType_t)(now-last_refresh)<pdMS_TO_TICKS(50))return;
 uint8_t page[128];
 for(unsigned i=0;i<8;++i){oled_canvas_begin((uint8_t)i,page);
 ui_view_draw(model,(uint32_t)(now*portTICK_PERIOD_MS));
 oled_canvas_end();
 }
 uint8_t dirty=oled_dirty_pages();
 if(dirty)oled_update_pages(dirty);
 last_refresh=now;
 refresh_started=true;
}
