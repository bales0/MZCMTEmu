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
static void test_seek_navigation(void){
 const unsigned pins[]={10,11};
 const btn_event_t short_event[]={BTN_NEXT,BTN_PREV};
 const btn_event_t long_event[]={BTN_NEXT_LONG,BTN_PREV_LONG};
 const btn_event_t repeat_event[]={BTN_NEXT_REPEAT,BTN_PREV_REPEAT};
 const btn_event_t release_event[]={BTN_NEXT_RELEASE,BTN_PREV_RELEASE};
 for(unsigned i=0;i<2;++i){
  reset();buttons_set_nav_mode(BUTTONS_NAV_SEEK);
  assert(down(pins[i])==BTN_NONE);assert(poll(400)==BTN_NONE);
  assert(up(pins[i])==short_event[i]);assert(!buttons_last_event_was_repeat());
  assert(poll(1000)==BTN_NONE);
  assert(down(pins[i])==BTN_NONE);assert(poll(699)==BTN_NONE);
  assert(poll(1)==long_event[i]);assert(!buttons_last_event_was_repeat());
  /* Idempotent mode selection must not reset a held seek gesture. */
  buttons_set_nav_mode(BUTTONS_NAV_SEEK);
  assert(poll(119)==BTN_NONE);assert(poll(1)==repeat_event[i]);
  assert(buttons_last_event_was_repeat());
  assert(poll(120)==repeat_event[i]);
  assert(up(pins[i])==release_event[i]);assert(!buttons_last_event_was_repeat());
  assert(poll(1000)==BTN_NONE);

  /* Releasing exactly at the long threshold still yields only a short tap
   * if no debounced long event has been published. */
  assert(down(pins[i])==BTN_NONE);assert(poll(670)==BTN_NONE);
  assert(up(pins[i])==short_event[i]);assert(poll(1000)==BTN_NONE);

  /* STOP suppresses both an uncommitted tap and a running long gesture. */
  for(unsigned held=0;held<2;++held){
   assert(down(pins[i])==BTN_NONE);
   if(held)assert(poll(700)==long_event[i]);
   assert(down(9)==BTN_NONE);assert(up(9)==BTN_STOP);
   assert(poll(1000)==BTN_NONE);assert(up(pins[i])==BTN_NONE);
   assert(down(pins[i])==BTN_NONE);assert(up(pins[i])==short_event[i]);
  }
  /* Both screen-change directions disarm a debounced held key. */
  assert(down(pins[i])==BTN_NONE);assert(poll(700)==long_event[i]);
  buttons_set_nav_mode(BUTTONS_NAV_NORMAL);
  assert(poll(1000)==BTN_NONE);assert(up(pins[i])==BTN_NONE);
  assert(down(pins[i])==short_event[i]);
  buttons_set_nav_mode(BUTTONS_NAV_SEEK);
  assert(poll(1000)==BTN_NONE);assert(up(pins[i])==BTN_NONE);
  assert(down(pins[i])==BTN_NONE);assert(up(pins[i])==short_event[i]);

  /* A raw press not yet polled/debounced cannot leak across mode changes. */
  levels[pins[i]]=0;buttons_set_nav_mode(BUTTONS_NAV_NORMAL);
  assert(poll(1000)==BTN_NONE);assert(up(pins[i])==BTN_NONE);
  levels[pins[i]]=0;assert(poll(0)==BTN_NONE);
  buttons_set_nav_mode(BUTTONS_NAV_SEEK);
  assert(poll(1000)==BTN_NONE);assert(up(pins[i])==BTN_NONE);
  assert(down(pins[i])==BTN_NONE);assert(up(pins[i])==short_event[i]);

  reset();now=(TickType_t)-50;buttons_set_nav_mode(BUTTONS_NAV_SEEK);
  assert(down(pins[i])==BTN_NONE);assert(poll(700)==long_event[i]);
  assert(poll(120)==repeat_event[i]);assert(up(pins[i])==release_event[i]);
 }
 /* Seek mode does not consume or change PLAY/REC/menu/STOP events. */
 for(unsigned i=0;i<2;++i){
  unsigned pin=i ? 42:8;
  reset();buttons_set_nav_mode(BUTTONS_NAV_SEEK);
  assert(down(pin)==BTN_NONE);assert(up(pin)==(i ? BTN_REC:BTN_PLAY));
  assert(down(pin)==BTN_NONE);assert(poll(700)==(i ? BTN_REC_LONG:BTN_PLAY_LONG));
  assert(up(pin)==BTN_NONE);
 }
 reset();buttons_set_nav_mode(BUTTONS_NAV_SEEK);
 assert(down(9)==BTN_NONE);assert(poll(800)==BTN_STOP_LONG);assert(up(9)==BTN_NONE);
 /* Ending a hold wins over the opposite short release in the same poll. */
 reset();buttons_set_nav_mode(BUTTONS_NAV_SEEK);
 assert(down(11)==BTN_NONE);assert(poll(700)==BTN_PREV_LONG);
 assert(down(10)==BTN_NONE);
 levels[10]=levels[11]=1;assert(poll(0)==BTN_NONE);
 assert(poll(30)==BTN_PREV_RELEASE);assert(poll(1000)==BTN_NONE);
 /* It also wins over a repeat due on the opposite held key. */
 assert(down(10)==BTN_NONE);assert(poll(700)==BTN_NEXT_LONG);
 assert(down(11)==BTN_NONE);assert(poll(700)==BTN_NEXT_REPEAT);
 assert(poll(0)==BTN_PREV_LONG);
 levels[11]=1;assert(poll(0)==BTN_NONE);
 assert(poll(120)==BTN_PREV_RELEASE);
 assert(poll(0)==BTN_NEXT_REPEAT);
 assert(up(10)==BTN_NEXT_RELEASE);
 /* Initialization always restores the normal browser contract. */
 reset();assert(down(10)==BTN_NEXT);assert(poll(400)==BTN_NEXT);
}
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
 test_seek_navigation();
 puts("PASS normal navigation unchanged; opt-in seek short/long/repeat/release, mode-switch suppression, STOP priority, PLAY/REC menus and tick wrap");
 return 0;
}
