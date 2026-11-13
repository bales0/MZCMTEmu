#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio/raw_pulse.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "soc/rmt_struct.h"
#ifdef RMT_PLAYER_TEST
#include "audio/player.h"
#include "audio/tap_stream.h"
#include "audio/wav_writer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#endif
struct fake_rmt_channel {bool allocated,enabled,running;} tx;
struct fake_rmt_encoder {bool allocated;rmt_simple_encoder_config_t config;} enc;
rmt_dev_t RMT;
static rmt_tx_event_callbacks_t callbacks;
static uint64_t wall_ticks,active_ticks;
static uint8_t pin;
#ifdef RMT_PLAYER_TEST
static uint8_t sense=1,write_pin,monitor;
static bool bus_owned,arm700;
#endif
static bool routed,source_done;
static rmt_symbol_word_t memory[128];
static size_t rd,wr,used,total_encoded;
static unsigned half,remaining,transmits,allocations,calls,fail_at;
static const void *payload;
static size_t payload_size;
static bool failed(void) {return fail_at && ++calls==fail_at;}
int64_t esp_timer_get_time(void) {return (int64_t)(wall_ticks/16);}
void test_rmt_clock_gate(bool enable) {RMT.clock=enable;}
esp_err_t gpio_reset_pin(int gpio) {assert(gpio==4);routed=false;pin=0;return ESP_OK;}
esp_err_t gpio_set_direction(int gpio,int mode) {assert((gpio==4 || gpio==7) && mode==GPIO_MODE_OUTPUT);return ESP_OK;}
esp_err_t gpio_set_level(int gpio,int level) {
#ifdef RMT_PLAYER_TEST
 if(gpio==4) {assert(!routed);pin=(uint8_t)level;if(bus_owned && arm700 && !level && sense){write_pin=0;arm700=false;}}
 else if(gpio==7) {if(bus_owned && sense!=(uint8_t)level)write_pin=level ? 0:1;sense=(uint8_t)level;}
 else {assert(gpio==41);monitor=(uint8_t)level;}
#else
 assert(gpio==4 && !routed);pin=(uint8_t)level;
#endif
 return ESP_OK;
}
#ifdef RMT_PLAYER_TEST
esp_err_t gpio_config(const gpio_config_t *config) {
 assert(config->pin_bit_mask==(1ULL<<5));bus_owned=true;
 arm700=player_sharp_details()->effective==LOADER_MODE_UL_MZ700;write_pin=arm700 ? 1:0;return ESP_OK;
}
int gpio_get_level(int gpio) {assert(gpio==5);return write_pin;}
#endif
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config,rmt_channel_handle_t *out) {
 assert(config->flags.with_dma && config->resolution_hz==16000000 && config->mem_block_symbols==128 && config->trans_queue_depth==1 && config->intr_priority==3);
 if(failed())return -1;assert(!tx.allocated);tx.allocated=true;*out=&tx;++allocations;RMT.clock=true;return ESP_OK;
}
esp_err_t rmt_del_channel(rmt_channel_handle_t ch) {assert(ch==&tx && ch->allocated && !ch->enabled);ch->allocated=false;return ESP_OK;}
esp_err_t rmt_new_simple_encoder(const rmt_simple_encoder_config_t *config,rmt_encoder_handle_t *out) {
 assert(config->min_chunk_size==1);if(failed())return -1;assert(!enc.allocated);enc.allocated=true;enc.config=*config;*out=&enc;++allocations;return ESP_OK;
}
esp_err_t rmt_del_encoder(rmt_encoder_handle_t e) {assert(e==&enc && e->allocated);e->allocated=false;return ESP_OK;}
esp_err_t rmt_tx_register_event_callbacks(rmt_channel_handle_t ch,const rmt_tx_event_callbacks_t *cb,void *context) {assert(ch==&tx && !context);if(failed())return -1;callbacks=*cb;return ESP_OK;}
esp_err_t rmt_tx_switch_gpio(rmt_channel_handle_t ch,int gpio,bool invert) {assert(ch==&tx && !ch->enabled && gpio==4 && !invert);routed=true;return ESP_OK;}
esp_err_t rmt_enable(rmt_channel_handle_t ch) {assert(ch==&tx && !ch->enabled);ch->enabled=true;RMT.clock=true;return ESP_OK;}
esp_err_t rmt_disable(rmt_channel_handle_t ch) {assert(ch==&tx && ch->enabled && RMT.clock);ch->enabled=ch->running=false;used=remaining=0;return ESP_OK;}
static void refill(void) {
 if(source_done || used>64)return;
 rmt_symbol_word_t buffer[64];bool done=false;size_t free_count=128-used;if(free_count>64)free_count=64;
 size_t n=enc.config.callback(payload,payload_size,total_encoded,free_count,buffer,&done,enc.config.arg);assert(n<=free_count && (n || done));
 for(size_t i=0;i<n;++i){memory[wr]=buffer[i];wr=(wr+1)%128;++used;}
 total_encoded+=n;source_done=done;
}
static void load_half(void) {
 while(tx.running && !remaining) {
  if(!used) {refill();if(!used)break;}
  rmt_symbol_word_t symbol=memory[rd];remaining=half ? symbol.duration1:symbol.duration0;
  if(!remaining)break;
  pin=(uint8_t)(half ? symbol.level1:symbol.level0);return;
 }
 if(tx.running && !remaining) {assert(source_done);tx.running=false;pin=0;rmt_tx_done_event_data_t done={total_encoded};callbacks.on_trans_done(&tx,&done,NULL);}
}
esp_err_t rmt_transmit(rmt_channel_handle_t ch,rmt_encoder_handle_t e,const void *data,size_t size,const rmt_transmit_config_t *config) {
 assert(ch==&tx && e==&enc && ch->enabled && !ch->running && size==1 && data && !config->loop_count && !config->flags.eot_level);
 rd=wr=used=total_encoded=0;half=remaining=0;source_done=false;payload=data;payload_size=size;++transmits;ch->running=true;
 refill();refill();load_half();return ESP_OK;
}
static void advance(uint64_t ticks) {
 if(!RMT.clock) {wall_ticks+=ticks;return;}
 while(ticks && tx.running) {
  load_half();if(!tx.running)break;unsigned n=ticks<remaining ? (unsigned)ticks:remaining;
  remaining-=n;ticks-=n;wall_ticks+=n;active_ticks+=n;
  if(!remaining) {if(half) {half=0;rd=(rd+1)%128;--used;refill();}else half=1;load_half();}
 }
 wall_ticks+=ticks;
}
#ifndef RMT_PLAYER_TEST
static void finish_interval(uint8_t level,uint32_t us) {
 uint64_t left=(uint64_t)us*16;
 while(left) {assert(tx.running && pin==level && remaining);uint64_t n=left<remaining ? left:remaining;advance(n);left-=n;raw_pulse_service();}
}
int main(void) {
 for(unsigned fault=1;fault<=3;++fault) {calls=0;fail_at=fault;raw_pulse_init();assert(!raw_pulse_start() && !tx.allocated && !enc.allocated);}
 fail_at=0;raw_pulse_init();unsigned initialized_allocations=allocations;raw_pulse_set_read_invert(false);
 raw_pulse_t events[]={{0,0,238},{1,1,469},{1,0,30000},{0,0,1000000},{1,1,UINT32_MAX},{0,0,10}};
 assert(raw_pulse_write(events,6)==6);raw_pulse_finish();assert(raw_pulse_start());unsigned session_count=transmits;
 advance(70);unsigned held=remaining;uint8_t held_pin=pin;uint64_t before=raw_pulse_get_completed_us();raw_pulse_pause();advance(5000000ULL*16);
 raw_pulse_service();assert(remaining==held && pin==held_pin && raw_pulse_get_completed_us()==before);raw_pulse_resume();advance(238*16-70);raw_pulse_service();
 uint64_t total=238;
 for(unsigned i=1;i<6;++i){finish_interval(events[i].level,events[i].duration_us);total+=events[i].duration_us;}
 raw_pulse_service();assert(!tx.running && !raw_pulse_is_running() && !raw_pulse_is_underrun() && !raw_pulse_level());
 assert(raw_pulse_get_completed_us()==total && transmits==session_count && active_ticks==total*16 && !pin);raw_pulse_stop();assert(!routed && !pin);
 raw_pulse_init();raw_pulse_set_read_invert(false);
 const uint32_t exact[]={3807,4087,7506,7786,1,65535};raw_pulse_t exact_events[6];
 for(unsigned i=0;i<6;++i)exact_events[i]=(raw_pulse_t){(uint8_t)(i&1),1,0};
 assert(raw_pulse_native_ticks16() && raw_pulse_write_ticks16(exact_events,exact,6)==6);raw_pulse_finish();assert(raw_pulse_start());
 uint64_t exact_total=0;
 for(unsigned i=0;i<6;++i) {uint32_t left=exact[i];
  while(left) {assert(tx.running && pin==(i&1));unsigned n=left<remaining ? left:remaining;advance(n);left-=n;raw_pulse_service();}
  exact_total+=exact[i];
 }
 raw_pulse_service();assert(raw_pulse_get_completed_us()==(exact_total+8)/16 && !raw_pulse_level());raw_pulse_stop();
 raw_pulse_init();raw_pulse_set_read_invert(true);const unsigned count=12000;unsigned queued=0,finished=0;uint64_t sum=0;
 while(queued<2048){raw_pulse_t event={(uint8_t)(queued&1),1,(queued%3)+77};assert(raw_pulse_write(&event,1)==1);++queued;}
 assert(raw_pulse_start());session_count=transmits;
 while(finished<count) {
  unsigned width=(finished%3)+77;finish_interval((uint8_t)((finished&1)^1),width);sum+=width;++finished;
  while(queued<count && raw_pulse_free()) {raw_pulse_t event={(uint8_t)(queued&1),1,(queued%3)+77};assert(raw_pulse_write(&event,1)==1);++queued;}
  if(queued==count)raw_pulse_finish();
 }
 raw_pulse_service();assert(!raw_pulse_is_underrun() && raw_pulse_get_completed_us()==sum && transmits==session_count);raw_pulse_stop();
 raw_pulse_init();raw_pulse_set_read_invert(false);raw_pulse_t event={1,1,100};assert(raw_pulse_write(&event,1)==1 && raw_pulse_start());finish_interval(1,100);
 raw_pulse_service();assert(raw_pulse_is_underrun() && !raw_pulse_is_running());raw_pulse_stop();
 raw_pulse_init();assert(raw_pulse_write(&event,1)==1 && raw_pulse_start());advance(20*16);raw_pulse_stop();assert(!routed && !pin && !tx.running);
 raw_pulse_init();assert(raw_pulse_write(&event,1)==1);raw_pulse_finish();assert(raw_pulse_start());finish_interval(1,100);raw_pulse_service();assert(raw_pulse_get_completed_us()==100);
 raw_pulse_stop();raw_pulse_init();
 raw_pulse_t monitor_events[]={{0,RAW_PULSE_MONITOR_MUTE,1000000},{1,RAW_PULSE_COUNT,1000000}};
 assert(!raw_pulse_monitor_active() && raw_pulse_write(monitor_events,2)==2);raw_pulse_finish();assert(raw_pulse_start());
 assert(!raw_pulse_monitor_active());advance(1000020ULL*16);raw_pulse_service();
 assert(raw_pulse_monitor_active() && raw_pulse_get_pulses_per_second()==0);
 raw_pulse_pause();assert(!raw_pulse_monitor_active());advance(5000000ULL*16);raw_pulse_resume();assert(raw_pulse_monitor_active());
 advance(999980ULL*16);raw_pulse_service();assert(!raw_pulse_monitor_active() && raw_pulse_get_pulses_per_second()==1);
 raw_pulse_stop();
 assert(allocations==initialized_allocations);
 puts("PASS real RMT DMA encoder: one continuous transaction through repeated refills, exact split long runs, physical levels/inversion, 32-bit duration carry and complete EOF");
 puts("PASS RMT source-clock pause preserves in-flight half symbol and DMA queue, STOP detaches GPIO for UL, replay, underrun and init failure cleanup; no start/stop allocation");return 0;
}

#else
TickType_t xTaskGetTickCount(void) {return 1;}
void vTaskDelay(TickType_t ticks) {(void)ticks;}
int xTaskCreatePinnedToCore(void (*entry)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out,int core) {
 (void)entry;(void)name;(void)stack;(void)arg;(void)priority;(void)out;(void)core;assert(!"Host services player explicitly");return 0;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {return &tx;}
int xSemaphoreTake(SemaphoreHandle_t mutex,TickType_t ticks) {(void)mutex;(void)ticks;return 1;}
int xSemaphoreGive(SemaphoreHandle_t mutex) {(void)mutex;return 1;}
static void fixture(const char *path) {
 FILE *f=fopen(path,"wb");assert(f);uint8_t header[128]={1},data[]={0,128,1,255};memcpy(header+1,"DMA TEST\r",9);header[18]=4;header[21]=header[23]=0x12;
 assert(fwrite(header,1,128,f)==128 && fwrite(data,1,4,f)==4 && !fclose(f));
}
static void drain_player(void) {
 unsigned budget=250000;bool paused_once=false;
 while(player_get_state()==PLAYER_STATE_PLAYING) {
  assert(budget--);
  if(!paused_once) {
   unsigned held=remaining;uint8_t level=pin;player_pause_user();advance(5000000ULL*16);
   player_resume_motor();assert(player_get_state()==PLAYER_STATE_PAUSED && remaining==held && pin==level);
   player_resume_user();paused_once=true;
  }
  if(tx.running && RMT.clock)advance(remaining);
  else wall_ticks+=10000ULL*16;
  player_service();
 }
 assert(player_get_state()==PLAYER_STATE_EOF && !tx.running && !pin && !routed && player_get_progress_percent()==100);
}
int main(void) {
 player_init();fixture("dma.mzf");
 for(unsigned mode=0;mode<LOADER_MODE_COUNT;++mode) {
  assert(player_set_loader_mode((loader_mode_t)mode));bus_owned=false;sense=1;
  uint64_t before=active_ticks;player_start("dma.mzf");assert(player_get_state()==PLAYER_STATE_PLAYING);
  uint64_t expected=player_sharp_details()->total_ticks;
  drain_player();assert(active_ticks-before==expected);
  if(mode>=2 && mode<=4)assert(sense==1 && !monitor);
 }
 assert(!remove("dma.mzf"));
 const uint8_t tap[]={2,0,0,0xA5,4,0,255,128,0,255};FILE *f=fopen("dma.tap","wb");assert(f && fwrite(tap,1,sizeof(tap),f)==sizeof(tap) && !fclose(f));
 for(unsigned speed=0;speed<4;++speed) {
  tap_stream_info_t info;assert(tap_stream_open("dma.tap",(tap_speed_t)speed,&info));tap_stream_close();
  assert(player_set_tap_speed((tap_speed_t)speed));bus_owned=false;
  uint64_t before=active_ticks;player_start("dma.tap");drain_player();assert(active_ticks-before==info.total_ticks);
 }
 assert(!remove("dma.tap"));
 uint8_t samples[6001];for(unsigned i=0;i<sizeof(samples);++i)samples[i]=(i/11)%2 ? 235:20;
 assert(wav_writer_open("dma.wav",44100,8,1) && wav_writer_write(samples,sizeof(samples))==sizeof(samples) && wav_writer_finalize());
 uint64_t before=active_ticks;player_start("dma.wav");drain_player();assert(active_ticks-before==((uint64_t)sizeof(samples)*16000000+22050)/44100);assert(!remove("dma.wav"));
 const uint8_t edge_data[]={0xF6,10,0,0xF6};f=fopen("dma.l16","wb");assert(f && fwrite(edge_data,1,sizeof(edge_data),f)==sizeof(edge_data) && !fclose(f));
 player_start("dma.l16");drain_player();assert(!remove("dma.l16"));
 puts("PASS actual player/RMT DMA integration for all 17 Sharp modes including simulated UL peer, all four TAP speeds, WAV and L16; exact active duration, user-pause precedence and safe GPIO handoff");return 0;
}
#endif
