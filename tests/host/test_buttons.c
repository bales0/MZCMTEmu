#include <assert.h>
#include <stdio.h>
#include "drivers/buttons.h"
#include "driver/gpio.h"
#include "freertos/task.h"
static TickType_t now;
static int levels[48];
TickType_t xTaskGetTickCount(void){return now;}
esp_err_t gpio_config(const gpio_config_t *cfg){assert(cfg->pin_bit_mask==((1ULL<<8)|(1ULL<<9)|(1ULL<<10)|(1ULL<<11)|(1ULL<<42)));return ESP_OK;}
int gpio_get_level(int pin){return levels[pin];}
static btn_event_t poll(unsigned ms){now+=ms;return buttons_poll();}
static void reset(void){for(int i=0;i<48;++i)levels[i]=1;now=0;buttons_init();}
static btn_event_t down(unsigned pin){levels[pin]=0;assert(poll(0)==BTN_NONE);return poll(30);}
static btn_event_t up(unsigned pin){levels[pin]=1;assert(poll(0)==BTN_NONE);return poll(30);}
int main(void){
 reset();
 levels[8]=0;assert(poll(0)==BTN_NONE);levels[8]=1;assert(poll(10)==BTN_NONE);
 levels[8]=0;assert(poll(10)==BTN_NONE);assert(poll(29)==BTN_NONE);assert(poll(1)==BTN_NONE);
 assert(poll(100)==BTN_NONE);assert(up(8)==BTN_PLAY);assert(poll(1000)==BTN_NONE);
 const unsigned dual[]={8,42};
 for(unsigned i=0;i<2;++i){
  reset();assert(down(dual[i])==BTN_NONE);assert(poll(699)==BTN_NONE);
  assert(poll(1)==(i ? BTN_REC_LONG:BTN_PLAY_LONG));assert(poll(1000)==BTN_NONE);
  assert(up(dual[i])==BTN_NONE);assert(poll(1000)==BTN_NONE);
  assert(down(dual[i])==BTN_NONE);assert(poll(100)==BTN_NONE);
  assert(up(dual[i])==(i ? BTN_REC:BTN_PLAY));assert(poll(1000)==BTN_NONE);
 }
 reset();assert(down(10)==BTN_NEXT);assert(!buttons_last_event_was_repeat());
 assert(poll(399)==BTN_NONE);assert(poll(1)==BTN_NEXT && buttons_last_event_was_repeat());
 assert(poll(119)==BTN_NONE);assert(poll(1)==BTN_NEXT);buttons_delay_repeat(BTN_NEXT,700);
 assert(poll(699)==BTN_NONE);assert(poll(1)==BTN_NEXT);
 /* STOP owns the chord; releasing held REC/PLAY after STOP cannot start a transport. */
 levels[9]=levels[8]=levels[42]=0;assert(poll(0)==BTN_NONE);assert(poll(30)==BTN_NONE);
 assert(poll(799)==BTN_NONE);assert(poll(1)==BTN_STOP_LONG);assert(poll(1000)==BTN_NONE);
 assert(up(9)==BTN_NONE);assert(up(8)==BTN_NONE);assert(up(42)==BTN_NONE);
 assert(poll(1000)==BTN_NONE);assert(up(10)==BTN_NONE);assert(down(10)==BTN_NEXT);
 reset();assert(down(9)==BTN_NONE);assert(poll(100)==BTN_NONE);assert(up(9)==BTN_STOP);
 reset();levels[8]=levels[9]=levels[42]=0;buttons_init();assert(poll(2000)==BTN_NONE);
 levels[8]=levels[9]=levels[42]=1;assert(poll(0)==BTN_NONE);assert(poll(30)==BTN_NONE);
 reset();now=(TickType_t)-50;assert(down(11)==BTN_PREV);assert(poll(400)==BTN_PREV && buttons_last_event_was_repeat());
 reset();now=(TickType_t)-50;assert(down(42)==BTN_NONE);assert(poll(700)==BTN_REC_LONG);assert(up(42)==BTN_NONE);
 puts("PASS GPIO pinmap, debounce, short PLAY/REC only on release; 700ms independent long menus, no transport on hold/release, startup suppression, STOP chord priority, repeat and tick wrap");
 return 0;
}
