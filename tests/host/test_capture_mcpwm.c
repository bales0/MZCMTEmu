#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio/edge_capture.h"
#include "driver/mcpwm_cap.h"
#include "driver/gpio.h"
struct fake_cap_timer {bool allocated,enabled,running;} timer;
struct fake_cap_channel {bool allocated,enabled;uint32_t latched;int gpio;} channels[2];
static mcpwm_capture_event_callbacks_t callback;
static uint64_t clock_ticks;
static uint8_t pin;
static int calls,fail_at,allocations;
static bool failed(void) {return fail_at && ++calls==fail_at;}
int64_t esp_timer_get_time(void) {return (int64_t)(clock_ticks/80);}
esp_err_t gpio_config(const gpio_config_t *config) {assert(config->pin_bit_mask==(1ULL<<5) && config->intr_type==GPIO_INTR_DISABLE);return failed() ? -1:ESP_OK;}
int gpio_get_level(int gpio) {assert(gpio==5);return pin;}
esp_err_t mcpwm_new_capture_timer(const mcpwm_capture_timer_config_t *config,mcpwm_cap_timer_handle_t *out) {
 assert(config->group_id==0);if(failed())return -1;assert(!timer.allocated);timer.allocated=true;*out=&timer;++allocations;return ESP_OK;
}
esp_err_t mcpwm_del_capture_timer(mcpwm_cap_timer_handle_t t) {assert(t==&timer && t->allocated && !t->enabled);t->allocated=false;return ESP_OK;}
esp_err_t mcpwm_capture_timer_get_resolution(mcpwm_cap_timer_handle_t t,uint32_t *out) {assert(t==&timer);if(failed())return -1;*out=80000000;return ESP_OK;}
esp_err_t mcpwm_capture_timer_enable(mcpwm_cap_timer_handle_t t) {if(failed())return -1;assert(t->allocated && !t->enabled);t->enabled=true;return ESP_OK;}
esp_err_t mcpwm_capture_timer_disable(mcpwm_cap_timer_handle_t t) {assert(t->enabled && !t->running);t->enabled=false;return ESP_OK;}
esp_err_t mcpwm_capture_timer_start(mcpwm_cap_timer_handle_t t) {if(failed())return -1;assert(t->enabled);t->running=true;return ESP_OK;}
esp_err_t mcpwm_capture_timer_stop(mcpwm_cap_timer_handle_t t) {assert(t->running);t->running=false;return ESP_OK;}
esp_err_t mcpwm_new_capture_channel(mcpwm_cap_timer_handle_t t,const mcpwm_capture_channel_config_t *config,mcpwm_cap_channel_handle_t *out) {
 assert(t==&timer && config->prescale==1);if(failed())return -1;
 unsigned index=config->gpio_num<0 ? 1:0;assert(!channels[index].allocated);
 assert(index || (config->flags.pos_edge && config->flags.neg_edge && config->intr_priority==3));
 assert(!index || (!config->flags.pos_edge && !config->flags.neg_edge));
 channels[index].allocated=true;channels[index].gpio=config->gpio_num;*out=&channels[index];++allocations;return ESP_OK;
}
esp_err_t mcpwm_del_capture_channel(mcpwm_cap_channel_handle_t ch) {assert(ch->allocated && !ch->enabled);ch->allocated=false;return ESP_OK;}
esp_err_t mcpwm_capture_channel_enable(mcpwm_cap_channel_handle_t ch) {if(failed())return -1;assert(ch->allocated && !ch->enabled);ch->enabled=true;return ESP_OK;}
esp_err_t mcpwm_capture_channel_disable(mcpwm_cap_channel_handle_t ch) {assert(ch->enabled);ch->enabled=false;return ESP_OK;}
esp_err_t mcpwm_capture_channel_register_event_callbacks(mcpwm_cap_channel_handle_t ch,const mcpwm_capture_event_callbacks_t *cb,void *arg) {
 assert(ch==&channels[0] && !ch->enabled && !arg);if(failed())return -1;callback=*cb;return ESP_OK;
}
esp_err_t mcpwm_capture_channel_trigger_soft_catch(mcpwm_cap_channel_handle_t ch) {assert(ch==&channels[1] && ch->enabled);ch->latched=(uint32_t)clock_ticks;return ESP_OK;}
esp_err_t mcpwm_capture_get_latched_value(mcpwm_cap_channel_handle_t ch,uint32_t *out) {assert(ch->allocated);*out=ch->latched;return ESP_OK;}
static void advance(uint64_t ticks) {clock_ticks+=ticks;}
static void edge(uint64_t target,uint8_t level,unsigned delay,bool deliver) {
 assert(target>=clock_ticks && channels[0].enabled);clock_ticks=target;pin=level;channels[0].latched=(uint32_t)target;
 if(deliver) {advance((uint64_t)delay*80);mcpwm_capture_event_data_t event={(uint32_t)target,level ? MCPWM_CAP_EDGE_POS:MCPWM_CAP_EDGE_NEG};assert(!callback.on_cap(&channels[0],&event,NULL));}
}
static void expect(uint8_t level,uint32_t units,bool rising) {edge_capture_event_t e;assert(edge_capture_read(&e,1)==1);assert(e.level==level && e.units==units && e.rising==(unsigned)rising);}
int main(void) {
 for(int fault=1;fault<=9;++fault) {
  calls=0;fail_at=fault;edge_capture_init(1);assert(edge_capture_is_overrun() && !edge_capture_is_running());
  assert(!timer.allocated && !channels[0].allocated && !channels[1].allocated);
 }
 fail_at=0;clock_ticks=0;pin=0;edge_capture_init(1);int initialized_allocations=allocations;edge_capture_start();assert(edge_capture_is_running());
 edge(100*80,1,20,true);expect(0,100,true);edge(250*80,0,60,true);expect(1,150,false);
 edge(400*80,1,30,true);expect(0,150,true);edge_capture_pause();expect(1,30,false);
 uint32_t old=channels[0].latched;advance(1000000ULL*80);pin=0;edge_capture_resume();uint64_t base=clock_ticks;
 mcpwm_capture_event_data_t stale={old,MCPWM_CAP_EDGE_POS};callback.on_cap(&channels[0],&stale,NULL);assert(!edge_capture_level() && !edge_capture_is_overrun());
 edge(base+90*80,1,0,true);expect(0,90,true);edge_capture_stop();
 clock_ticks=UINT32_MAX-1000ULL;pin=0;edge_capture_init(1);edge_capture_start();base=clock_ticks;
 edge(base+20*80,1,0,true);expect(0,20,true);
 uint64_t long_ticks=(2ULL<<32)+1234ULL*80;edge(clock_ticks+long_ticks,0,20,true);expect(1,(uint32_t)((long_ticks+40)/80),false);edge_capture_stop();
 pin=0;edge_capture_init(1);edge_capture_start();base=clock_ticks;
 edge(base+100*80,1,0,true);expect(0,100,true);edge(base+160*80,0,0,false);advance(20*80);edge_capture_pause();expect(1,60,false);expect(0,20,false);
 assert(!edge_capture_level());advance(130000ULL*80);pin=1;edge_capture_resume();base=clock_ticks;edge(base+90*80,0,0,true);expect(1,90,false);edge_capture_stop();
 edge_capture_init(50);edge_capture_start();base=clock_ticks;pin=0;
 edge(base+74*80,1,0,true);expect(0,1,true);edge(base+148*80,0,0,true);expect(1,1,false);edge_capture_stop();
 edge_capture_init(1);edge_capture_start();for(unsigned i=0;i<8192;++i)edge(clock_ticks+100*80,(uint8_t)!pin,0,true);
 assert(edge_capture_is_overrun() && !edge_capture_is_running() && edge_capture_level()==8191);edge_capture_stop();
 edge_capture_init(1);edge_capture_start();edge(clock_ticks+100*80,pin,0,true);assert(edge_capture_is_overrun());edge_capture_stop();
 assert(allocations==initialized_allocations);
 puts("PASS actual MCPWM capture driver: hardware latch ignores ISR delay, 32-bit wrap and multi-wrap idle, stale IRQ, pending-edge pause tail, resume clock and polarity");
 puts("PASS MCPWM independent quantization, FIFO overflow and missed-polarity error, all initialization failures clean resources; no allocation on start/stop");return 0;
}
