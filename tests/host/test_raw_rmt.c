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
#include "audio/mzf_stream.h"
#include "audio/wav_writer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#endif
struct fake_rmt_channel {bool allocated,enabled,running;
} tx;
struct fake_rmt_encoder {bool allocated;
rmt_simple_encoder_config_t config;
} enc;
rmt_dev_t RMT;
static rmt_tx_event_callbacks_t callbacks;
static uint64_t wall_ticks,active_ticks;
static uint8_t pin;
#ifdef RMT_PLAYER_TEST
static uint8_t sense=1,write_pin,monitor;
static bool bus_owned,arm700;
static bool verify_ul_payload;
static uint32_t ul_received;
static uint8_t ul_bit_count,ul_bits;
static uint32_t ul_read_writes,ul_last_clock_read,ul_pad_reads;
static uint64_t peer_ack_at,ul_low_at,ul_longest_low;
static uint8_t ul_expected_byte(uint32_t i) { return (uint8_t)((i*73U+(i>>8)*19U)^0xA5U); }
#endif
static bool routed,source_done;
static rmt_symbol_word_t memory[128];
static size_t rd,wr,used,total_encoded;
static unsigned half,remaining,transmits,allocations,calls,fail_at;
static const void *payload;
static size_t payload_size;
static bool failed(void) {return fail_at && ++calls==fail_at;
}
int64_t esp_timer_get_time(void) {
#ifdef RMT_PLAYER_TEST
 if(bus_owned && mzf_stream_is_ultrafast_active())wall_ticks+=16; /* Poll/CPU time. */
#endif
return (int64_t)(wall_ticks/16);
}
void test_rmt_clock_gate(bool enable) {RMT.clock=enable;
}
esp_err_t gpio_reset_pin(int gpio) {assert(gpio==4);
routed=false;
pin=0;
return ESP_OK;
}
esp_err_t gpio_set_direction(int gpio,int mode) {assert((gpio==4 || gpio==7) && (mode==GPIO_MODE_OUTPUT || mode==GPIO_MODE_INPUT_OUTPUT));
return ESP_OK;
}
esp_err_t gpio_set_level(int gpio,int level) {
#ifdef RMT_PLAYER_TEST
 if(gpio==4) {assert(!routed);
 pin=(uint8_t)level;
 ++ul_read_writes;
 if(bus_owned && arm700 && !level && sense){write_pin=0;
 arm700=false;
 }}
 else if(gpio==7) {
 if(verify_ul_payload && bus_owned && !sharp_stream_has_error() && sense!=(uint8_t)level) {
  assert(ul_read_writes>ul_last_clock_read); /* READ written before each SENSE edge. */
  ul_last_clock_read=ul_read_writes;
  if(!level)ul_low_at=wall_ticks;
  else if(wall_ticks-ul_low_at>ul_longest_low)ul_longest_low=wall_ticks-ul_low_at;
  ul_bits=(uint8_t)((ul_bits<<1)|pin);
  if(++ul_bit_count==8) {
   uint8_t decoded=(uint8_t)~((ul_bits>>2)|(ul_bits<<6));
   assert(decoded==ul_expected_byte(ul_received));
   ++ul_received;ul_bit_count=0;ul_bits=0;
  }
 }
 if(bus_owned && !arm700 && sense!=(uint8_t)level){
  if(verify_ul_payload && !level){
   write_pin=0;peer_ack_at=wall_ticks+((ul_received==0 && ul_bit_count==1) ? 8600U:20U)*16;
  }
  else {write_pin=level ? 0:1;peer_ack_at=0;}
 }
 sense=(uint8_t)level;
 }
 else {assert(gpio==41);
 monitor=(uint8_t)level;
 }
#else
 assert(gpio==4 && !routed);
 pin=(uint8_t)level;
#endif
 return ESP_OK;
}
#ifdef RMT_PLAYER_TEST
esp_err_t gpio_config(const gpio_config_t *config) {
 assert(config->pin_bit_mask==(1ULL<<5));
 bus_owned=true;
 arm700=player_sharp_details()->effective==LOADER_MODE_UL_MZ700;
 write_pin=arm700 ? 1:0;
 return ESP_OK;
}
int gpio_get_level(int gpio) {
if(verify_ul_payload && gpio!=5)++ul_pad_reads;
if(gpio==4)return pin;
if(gpio==7)return sense;
if(gpio==6)return verify_ul_payload ? (!sense && !peer_ack_at):1;
assert(gpio==5);
if(peer_ack_at && wall_ticks>=peer_ack_at){write_pin=1;peer_ack_at=0;}
return write_pin;
}
#endif
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config,rmt_channel_handle_t *out) {
 assert(config->gpio_num==4); /* ESP-IDF rejects -1 even for detached idle. */
 assert(config->flags.with_dma && config->resolution_hz==16000000 && config->mem_block_symbols==128 && config->trans_queue_depth==1 && config->intr_priority==3);
 if(failed())return -1;
 assert(!tx.allocated);
 tx.allocated=true;
 routed=true;
 *out=&tx;
 ++allocations;
 RMT.clock=true;
 return ESP_OK;
}
esp_err_t rmt_del_channel(rmt_channel_handle_t ch) {assert(ch==&tx && ch->allocated && !ch->enabled);
ch->allocated=false;
return ESP_OK;
}
esp_err_t rmt_new_simple_encoder(const rmt_simple_encoder_config_t *config,rmt_encoder_handle_t *out) {
 assert(config->min_chunk_size==1);
 if(failed())return -1;
 assert(!enc.allocated);
 enc.allocated=true;
 enc.config=*config;
 *out=&enc;
 ++allocations;
 return ESP_OK;
}
esp_err_t rmt_del_encoder(rmt_encoder_handle_t e) {assert(e==&enc && e->allocated);
e->allocated=false;
return ESP_OK;
}
esp_err_t rmt_tx_register_event_callbacks(rmt_channel_handle_t ch,const rmt_tx_event_callbacks_t *cb,void *context) {assert(ch==&tx && !context);
if(failed())return -1;
callbacks=*cb;
return ESP_OK;
}
esp_err_t rmt_tx_switch_gpio(rmt_channel_handle_t ch,int gpio,bool invert) {assert(ch==&tx && !ch->enabled && gpio==4 && !invert);
routed=true;
return ESP_OK;
}
esp_err_t rmt_enable(rmt_channel_handle_t ch) {assert(ch==&tx && !ch->enabled);
ch->enabled=true;
RMT.clock=true;
return ESP_OK;
}
esp_err_t rmt_disable(rmt_channel_handle_t ch) {assert(ch==&tx && ch->enabled && RMT.clock);
ch->enabled=ch->running=false;
used=remaining=0;
return ESP_OK;
}
static void refill(void) {
 if(source_done || used>64)return;
 rmt_symbol_word_t buffer[64];
 bool done=false;
 size_t free_count=128-used;
 if(free_count>64)free_count=64;
 size_t n=enc.config.callback(payload,payload_size,total_encoded,free_count,buffer,&done,enc.config.arg);
 assert(n<=free_count && (n || done));
 for(size_t i=0;i<n;++i){memory[wr]=buffer[i];
 wr=(wr+1)%128;
 ++used;
 }
 total_encoded+=n;
 source_done=done;
}
static void load_half(void) {
 while(tx.running && !remaining) {
  if(!used) {refill();
  if(!used)break;
  }
  rmt_symbol_word_t symbol=memory[rd];
  remaining=half ? symbol.duration1:symbol.duration0;
  if(!remaining)break;
  pin=(uint8_t)(half ? symbol.level1:symbol.level0);
  return;
 }
 if(tx.running && !remaining) {assert(source_done);
 tx.running=false;
 pin=0;
 rmt_tx_done_event_data_t done={total_encoded};
 callbacks.on_trans_done(&tx,&done,NULL);
 }
}
esp_err_t rmt_transmit(rmt_channel_handle_t ch,rmt_encoder_handle_t e,const void *data,size_t size,const rmt_transmit_config_t *config) {
 assert(ch==&tx && e==&enc && ch->enabled && !ch->running && size==1 && data && !config->loop_count && !config->flags.eot_level);
 rd=wr=used=total_encoded=0;
 half=remaining=0;
 source_done=false;
 payload=data;
 payload_size=size;
 ++transmits;
 ch->running=true;
 refill();
 refill();
 load_half();
 return ESP_OK;
}
static void advance(uint64_t ticks) {
 if(!RMT.clock) {wall_ticks+=ticks;
 return;
 }
 while(ticks && tx.running) {
  load_half();
  if(!tx.running)break;
  unsigned n=ticks<remaining ? (unsigned)ticks:remaining;
  remaining-=n;
  ticks-=n;
  wall_ticks+=n;
  active_ticks+=n;
  if(!remaining) {if(half) {half=0;
  rd=(rd+1)%128;
  --used;
  refill();
  }
else half=1;
  load_half();
  }
 }
 wall_ticks+=ticks;
}
#ifndef RMT_PLAYER_TEST
static void finish_interval(uint8_t level,uint32_t us) {
 uint64_t left=(uint64_t)us*16;
 while(left) {assert(tx.running && pin==level && remaining);
 uint64_t n=left<remaining ? left:remaining;
 advance(n);
 left-=n;
 raw_pulse_service();
 }
}
int main(void) {
 for(unsigned fault=1;fault<=3;++fault) {calls=0;
 fail_at=fault;
 raw_pulse_init();
 assert(!raw_pulse_start() && !tx.allocated && !enc.allocated);
 const char *expected_error=fault==1 ? "RMT CHANNEL":fault==2 ? "RMT ENCODER":"RMT CALLBACK";
 assert(raw_pulse_error() && !strcmp(raw_pulse_error(),expected_error));
 }
 fail_at=0;
 raw_pulse_init();
 assert(!raw_pulse_error());
 unsigned initialized_allocations=allocations;
 raw_pulse_set_read_invert(false);
 raw_pulse_t events[]={{0,0,238},{1,1,469},{1,0,30000},{0,0,1000000},{1,1,UINT32_MAX},{0,0,10}};
 assert(raw_pulse_write(events,6)==6);
 raw_pulse_finish();
 assert(raw_pulse_start());
 unsigned session_count=transmits;
 raw_pulse_status_t status;
 raw_pulse_status(&status);
 assert(status.time_valid && status.reserve_us>UINT32_MAX);
 advance(70);
 unsigned held=remaining;
 uint8_t held_pin=pin;
 uint64_t before=raw_pulse_get_completed_us();
 raw_pulse_pause();
 advance(5000000ULL*16);
 raw_pulse_service();
 raw_pulse_status(&status);
 uint64_t paused_reserve=status.reserve_us;
 advance(1000000ULL*16);
 raw_pulse_status(&status);
 assert(status.reserve_us==paused_reserve && status.paused);
 assert(remaining==held && pin==held_pin && raw_pulse_get_completed_us()==before);
 raw_pulse_resume();
 advance(238*16-70);
 raw_pulse_service();
 uint64_t total=238;
 for(unsigned i=1;i<6;++i){finish_interval(events[i].level,events[i].duration_us);
 total+=events[i].duration_us;
 }
 raw_pulse_service();
 assert(!tx.running && !raw_pulse_is_running() && !raw_pulse_is_underrun() && !raw_pulse_level());
 assert(raw_pulse_get_completed_us()==total && transmits==session_count && active_ticks==total*16 && !pin);
 raw_pulse_stop();
 assert(!routed && !pin);
 raw_pulse_init();
 raw_pulse_set_read_invert(false);
 const uint32_t exact[]={3807,4087,7506,7786,1,65535};
 raw_pulse_t exact_events[6];
 for(unsigned i=0;i<6;++i)exact_events[i]=(raw_pulse_t){(uint8_t)(i&1),1,0};
 assert(raw_pulse_native_ticks16() && raw_pulse_write_ticks16(exact_events,exact,6)==6);
 raw_pulse_finish();
 assert(raw_pulse_start());
 uint64_t exact_total=0;
 for(unsigned i=0;i<6;++i) {uint32_t left=exact[i];
  while(left) {assert(tx.running && pin==(i&1));
  unsigned n=left<remaining ? left:remaining;
  advance(n);
  left-=n;
  raw_pulse_service();
  }
  exact_total+=exact[i];
 }
 raw_pulse_service();
 assert(raw_pulse_get_completed_us()==(exact_total+8)/16 && !raw_pulse_level());
 raw_pulse_stop();
 raw_pulse_init();
 raw_pulse_set_read_invert(true);
 const unsigned count=12000;
 unsigned queued=0,finished=0;
 uint64_t sum=0;
 while(queued<2048){raw_pulse_t event={(uint8_t)(queued&1),1,(queued%3)+77};
 assert(raw_pulse_write(&event,1)==1);
 ++queued;
 }
 assert(raw_pulse_start());
 session_count=transmits;
 while(finished<count) {
  unsigned width=(finished%3)+77;
  finish_interval((uint8_t)((finished&1)^1),width);
  sum+=width;
  ++finished;
  while(queued<count && raw_pulse_free()) {raw_pulse_t event={(uint8_t)(queued&1),1,(queued%3)+77};
  assert(raw_pulse_write(&event,1)==1);
  ++queued;
  }
  if(queued==count)raw_pulse_finish();
 }
 raw_pulse_service();
 assert(!raw_pulse_is_underrun() && raw_pulse_get_completed_us()==sum && transmits==session_count);
 raw_pulse_stop();
 raw_pulse_init();
 raw_pulse_set_read_invert(false);
 raw_pulse_t event={1,1,100};
 assert(raw_pulse_write(&event,1)==1 && raw_pulse_start());
 finish_interval(1,100);
 raw_pulse_service();
 assert(raw_pulse_is_underrun() && !raw_pulse_is_running());
 raw_pulse_stop();
 raw_pulse_init();
 assert(raw_pulse_write(&event,1)==1 && raw_pulse_start());
 advance(20*16);
 raw_pulse_stop();
 assert(!routed && !pin && !tx.running);
 raw_pulse_init();
 assert(raw_pulse_write(&event,1)==1);
 raw_pulse_finish();
 assert(raw_pulse_start());
 finish_interval(1,100);
 raw_pulse_service();
 assert(raw_pulse_get_completed_us()==100);
 raw_pulse_stop();
 raw_pulse_init();
 raw_pulse_t monitor_events[]={{0,RAW_PULSE_MONITOR_MUTE,1000000},{1,RAW_PULSE_COUNT,1000000}};
 assert(!raw_pulse_monitor_active() && raw_pulse_write(monitor_events,2)==2);
 raw_pulse_finish();
 assert(raw_pulse_start());
 assert(!raw_pulse_monitor_active());
 advance(1000020ULL*16);
 raw_pulse_service();
 assert(raw_pulse_monitor_active() && raw_pulse_get_pulses_per_second()==0);
 raw_pulse_pause();
 assert(!raw_pulse_monitor_active());
 advance(5000000ULL*16);
 raw_pulse_resume();
 assert(raw_pulse_monitor_active());
 advance(999980ULL*16);
 raw_pulse_service();
 assert(!raw_pulse_monitor_active() && raw_pulse_get_pulses_per_second()==1);
 raw_pulse_stop();
 raw_pulse_init();
 /* Reproduce stopped player with a full 4095-entry unsent FIFO. */
 raw_pulse_t stopped_pending_event={1,RAW_PULSE_COUNT,5000};
 for(unsigned i=0;i<4095;++i)assert(raw_pulse_write(&stopped_pending_event,1)==1);
 assert(raw_pulse_start());
 raw_pulse_status(&status);assert(status.fifo_used==4095 && status.reserve_us>20000000);
 raw_pulse_debug_t diagnostic;raw_pulse_debug(&diagnostic);
 assert(diagnostic.rmt && diagnostic.encoder_callbacks>0 && diagnostic.encoded_us>0 && diagnostic.completed_transactions==0);
 raw_pulse_stop();raw_pulse_status(&status);
 assert(!status.running && !status.paused && status.fifo_used==0 && status.reserve_us==0);
 assert(raw_pulse_free()==4095 && !raw_pulse_start() && !pin && !routed);
 raw_pulse_init();
 raw_pulse_t long_event={1,RAW_PULSE_COUNT|RAW_PULSE_SOURCE_BYTE,1000000};
 assert(raw_pulse_write(&long_event,1)==1);
 raw_pulse_finish();
 assert(raw_pulse_start());
 raw_pulse_status(&status);
 assert(status.fifo_used==1 && status.reserve_us>=999990 && !raw_pulse_completed_source_bytes());
 advance(750000ULL*16);
 raw_pulse_service();
 raw_pulse_status(&status);
 assert(status.reserve_us>=250000 && status.reserve_us<=250020 && !raw_pulse_completed_source_bytes());
 advance(250000ULL*16);
 raw_pulse_service();
 assert(raw_pulse_completed_source_bytes()==1);
 raw_pulse_stop();
 assert(allocations==initialized_allocations);
 puts("PASS real RMT DMA encoder: one continuous transaction through repeated refills, exact split long runs, physical levels/inversion, 32-bit duration carry and complete EOF");
 puts("PASS RMT source-clock pause preserves in-flight half symbol and DMA queue, STOP detaches GPIO for UL, replay, underrun and init failure cleanup; no start/stop allocation");
 return 0;
}

#else
TickType_t xTaskGetTickCount(void) {return 1;
}
void vTaskDelay(TickType_t ticks) {(void)ticks;
}
int xTaskCreatePinnedToCore(void (*entry)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out,int core) {
 (void)entry;
 (void)name;
 (void)stack;
 (void)arg;
 (void)priority;
 (void)out;
 (void)core;
 assert(!"Host services player explicitly");
 return 0;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {return &tx;
}
int xSemaphoreTake(SemaphoreHandle_t mutex,TickType_t ticks) {(void)mutex;
(void)ticks;
return 1;
}
int xSemaphoreGive(SemaphoreHandle_t mutex) {(void)mutex;
return 1;
}
static void fixture(const char *path) {
 FILE *f=fopen(path,"wb");
 assert(f);
 uint8_t header[128]={1},data[]={0,128,1,255};
 memcpy(header+1,"DMA TEST\r",9);
 header[18]=4;
 header[21]=header[23]=0x12;
 assert(fwrite(header,1,128,f)==128 && fwrite(data,1,4,f)==4 && !fclose(f));
}
static void large_ul800_fixture(void) {
 FILE *f=fopen("large-ul800.mzf","wb");assert(f);
 uint8_t header[128]={1};memcpy(header+1,"UL800 44033\r",12);
 header[18]=(uint8_t)44033;header[19]=(uint8_t)(44033>>8);
 header[21]=header[23]=0x1e;
 assert(fwrite(header,1,128,f)==128);
 for(uint32_t i=0;i<44033;++i)assert(fputc(ul_expected_byte(i),f)!=EOF);
 assert(!fclose(f));
}
static uint64_t played_records,record_ticks;
static const loader_mode_t *expected_qd_modes;
static void drain_player(void) {
 played_records=record_ticks=0;
 unsigned budget=250000;
 bool paused_once=false,paused_data_once=false;
 while(player_get_state()==PLAYER_STATE_PLAYING) {
  assert(budget--);
  unsigned record=player_sharp_details()->record;
  if(expected_qd_modes && record>=1 && record<=3)assert(player_sharp_details()->effective==expected_qd_modes[record-1]);
  if(record<64 && !(played_records&(1ULL<<record))){played_records|=1ULL<<record;record_ticks+=player_sharp_details()->total_ticks;}
  if(!paused_once) {
   unsigned held=remaining;
   uint8_t level=pin;
   player_pause_user();
   advance(5000000ULL*16);
   player_resume_motor();
   assert(player_get_state()==PLAYER_STATE_PAUSED && remaining==held && pin==level);
   player_resume_user();
   paused_once=true;
  }
  if(tx.running && RMT.clock)advance(remaining);
  else wall_ticks+=(player_is_ultrafast_active() ? 1000ULL:10000ULL)*16;
  player_service();
  if(verify_ul_payload && bus_owned){
   if(sense!=1 || ul_bit_count!=0)fprintf(stderr,"RETURN sense=%u bits=%u received=%u state=%u active=%u\n",sense,ul_bit_count,ul_received,player_get_state(),mzf_stream_is_ultrafast_active());
   assert(sense==1 && ul_bit_count==0);
   if(!paused_data_once && ul_received && player_get_state()==PLAYER_STATE_PLAYING){
    uint32_t held=ul_received;
    player_pause_user();wall_ticks+=5000000ULL*16;player_service();
    assert(player_get_state()==PLAYER_STATE_PAUSED && ul_received==held && sense==1);
    player_resume_user();paused_data_once=true;
   }
  } /* Whole-byte return. */
 }
 if(player_sharp_details()->format==FILE_FORMAT_MZQ)assert(player_get_state()==PLAYER_STATE_READY);
 else assert(player_get_state()==PLAYER_STATE_EOF && player_get_progress_percent()==100);
 assert(!tx.running && !pin && !routed);
}
static unsigned qd_scan_calls;
static bool qd_count_scan(void *unused){(void)unused;++qd_scan_calls;return false;}
static void qd_rmt_sequence(void){
 FILE *f=fopen("rmt.qd","wb");assert(f);
 const uint8_t count[]={0,0x16,0x16,0xa5,6,'C','R','C'},fh[]={0,0x16,0x16,0xa5,0,64,0},fd[]={0,0x16,0x16,0xa5,1,0,1};
 uint8_t data[256];for(unsigned i=0;i<sizeof(data);++i)data[i]=(uint8_t)i;
 assert(fwrite(count,1,sizeof(count),f)==sizeof(count));
 for(unsigned i=0;i<3;++i){uint8_t h[64]={1};h[1]=(uint8_t)('A'+i);h[21]=1;h[23]=h[25]=0x12;
  assert(fwrite(fh,1,sizeof(fh),f)==sizeof(fh) && fwrite(h,1,64,f)==64 && fwrite("CRC",1,3,f)==3);
  assert(fwrite(fd,1,sizeof(fd),f)==sizeof(fd) && fwrite(data,1,sizeof(data),f)==sizeof(data) && fwrite("CRC",1,3,f)==3);
 }
 assert(!fclose(f));
 const loader_mode_t modes[][3]={{LOADER_MODE_NORMAL_1_1,LOADER_MODE_IC_1_4,LOADER_MODE_TC_1_3},
  {LOADER_MODE_UL,LOADER_MODE_UL_MZ800,LOADER_MODE_UL_MZ700},
  {LOADER_MODE_AUTO,LOADER_MODE_AUTO,LOADER_MODE_AUTO}};
 for(unsigned pass=0;pass<3;++pass){
  if(pass==2){FILE *profile=fopen("rmt.qd.QDI","wb");assert(profile);
   assert(fputs("RECORD=1\nTYPE=NORMAL\nSPEED=1:1\nRECORD=2\nTYPE=IC\nSPEED=1:4\nRECORD=3\nTYPE=TC\nSPEED=1:3\n",profile)>=0 && !fclose(profile));
  }
  expected_qd_modes=pass==2 ? modes[0]:NULL;
  player_stop();assert(player_set_loader_mode(LOADER_MODE_NORMAL_1_1));bus_owned=false;sense=1;
  for(unsigned i=0;i<3;++i){assert(player_prepare_record("rmt.qd",i+1));assert(player_set_record_mode(modes[pass][i]));}
  assert(player_prepare_record("rmt.qd",1));
  qd_scan_calls=0;sharp_stream_set_analysis_control(qd_count_scan,NULL);
  player_start(player_get_current_path());unsigned scans=qd_scan_calls;
  if(pass==1){unsigned budget=100000;
   while(!mzf_stream_get_ultrafast_sent_bytes()){
    assert(budget-- && player_get_state()==PLAYER_STATE_PLAYING);
    if(tx.running && RMT.clock)advance(remaining);else wall_ticks+=1000ULL*16;
    player_service();
   }
   assert(player_is_ultrafast_active());
  }else player_pause_user();
  assert(player_stop_to_selector() && player_get_state()==PLAYER_STATE_READY);
  assert(!routed && !pin && !tx.running && sense==1 && !player_is_ultrafast_active() && qd_scan_calls==scans);
  assert(player_sharp_details()->record==1);
  player_start(player_get_current_path());assert(qd_scan_calls==scans);
  uint64_t before=active_ticks;drain_player();
  assert(qd_scan_calls==scans);sharp_stream_set_analysis_control(NULL,NULL);
  assert(played_records==14 && active_ticks-before==record_ticks);
  assert(player_sharp_details()->record==3 && !strcmp(player_sharp_details()->title,"C") && player_get_state()==PLAYER_STATE_READY);
 }
 expected_qd_modes=NULL;player_stop();assert(!remove("rmt.qd") && !remove("rmt.qd.QDI"));
 puts("PASS actual RMT QD continuation: three native/turbo and three UL records plus QDI AUTO per-record profiles, exact waveform duration, pause preserved, final selector without wrap");
}
static void fast_mzf_rmt_progress(void){
 const char *path="fast-rmt.mzf";FILE *f=fopen(path,"wb");assert(f);
 uint8_t h[128]={1};h[19]=4;h[21]=h[23]=0x12;
 assert(fwrite(h,1,128,f)==128);for(unsigned i=0;i<1280;++i)assert(fputc((uint8_t)i,f)!=EOF);assert(!fclose(f));
 player_stop();assert(player_set_loader_mode(LOADER_MODE_NORMAL_1_1));
 qd_scan_calls=0;sharp_stream_set_analysis_control(qd_count_scan,NULL);
 player_start(path);unsigned first=transmits;assert(!qd_scan_calls && mzf_stream_transfer_bytes()==1280 && !player_get_progress_percent());
 unsigned budget=100000;bool payload=false,extra=false,paused=false;
 while(player_get_state()==PLAYER_STATE_PLAYING){
  assert(budget--);
  unsigned stage=transmits-first;uint32_t bytes=raw_pulse_completed_source_bytes();
  assert(stage<=2);
  uint32_t done=stage==0 ? 0:stage==1 ? bytes:1024+bytes;
  unsigned percent=done*100/1280;if(percent>=100)percent=99;
  assert(player_get_progress_percent()==percent);
  if(stage==1 && bytes>100 && !paused){
   player_pause_user();uint8_t held=player_get_progress_percent();advance(5000000ULL*16);player_service();
   assert(player_get_state()==PLAYER_STATE_PAUSED && player_get_progress_percent()==held && raw_pulse_completed_source_bytes()==bytes);
   player_resume_user();paused=true;
  }
  if(stage==1 && bytes>100)payload=true;
  if(stage==2 && bytes>100)extra=true;
  if(tx.running && RMT.clock)advance(remaining);else wall_ticks+=10000ULL*16;
  player_service();
 }
 assert(player_get_state()==PLAYER_STATE_EOF && player_get_progress_percent()==100 && payload && extra && paused && !qd_scan_calls);
 player_start(path);first=transmits;budget=100000;
 while(transmits-first<2 || raw_pulse_completed_source_bytes()<20){
  assert(budget-- && player_get_state()==PLAYER_STATE_PLAYING);
  if(tx.running && RMT.clock)advance(remaining);else wall_ticks+=10000ULL*16;
  player_service();
 }
 uint8_t held=player_get_progress_percent();player_stop();advance(1000000ULL*16);player_service();
 assert(player_get_progress_percent()==held && !qd_scan_calls && !tx.running && !pin);
 sharp_stream_set_analysis_control(NULL,NULL);assert(!remove(path));
 f=fopen("empty.mzf","wb");h[19]=0;assert(f && fwrite(h,1,128,f)==128 && !fclose(f));
 player_start("empty.mzf");assert(!mzf_stream_transfer_bytes() && !player_get_progress_percent());drain_player();
 assert(player_get_progress_percent()==100);player_stop();assert(!remove("empty.mzf"));
 puts("PASS actual RMT fast MZF: no analysis, completed payload/append bytes across RMT resets, pause/STOP freeze, EOF100 and zero-length file");
}
/* A sparse WAV prefill must outlast RMT's initial 128-symbol lookahead. */
static void wav_sparse_prefill(void) {
 const unsigned rates[]={44100,48000,96000};
 uint8_t block[256];
 for(unsigned r=0;r<sizeof(rates)/sizeof(rates[0]);++r)for(unsigned signal=0;signal<2;++signal){
  unsigned rate=rates[r],length=rate*3+137,written=0;
  assert(wav_writer_open("sparse.wav",rate,8,1));
  while(written<length){unsigned n=length-written;if(n>sizeof(block))n=sizeof(block);
   for(unsigned i=0;i<n;++i){unsigned sample=written+i;block[i]=signal && sample>=rate*2 ? ((sample/11)%2 ? 235:20):128;}
   assert(wav_writer_write(block,n)==n);written+=n;
  }
  assert(wav_writer_finalize());
  uint64_t before=active_ticks;
  player_start("sparse.wav");
  assert(player_get_state()==PLAYER_STATE_PLAYING);
  /* This file is longer than the bounded prefill: encoding must remain open. */
  assert(!source_done);
  unsigned budget=1000;
  while(player_get_state()==PLAYER_STATE_PLAYING){
   assert(budget--);advance(10000ULL*16);player_service();
  }
  assert(player_get_state()==PLAYER_STATE_EOF && player_get_progress_percent()==100);
  assert(!raw_pulse_is_underrun() && active_ticks-before==((uint64_t)length*16000000+rate/2)/rate);
  assert(!remove("sparse.wav"));
 }
 puts("PASS sparse WAV 44.1/48/96kHz: silence and two-second silent intro, open RMT prefill, 10ms service, exact physical duration and EOF100");
}
int main(void) {
 setvbuf(stdout,NULL,_IONBF,0);
 player_init();
 qd_rmt_sequence();
 fast_mzf_rmt_progress();
 fixture("dma.mzf");
 for(unsigned mode=0;mode<LOADER_MODE_COUNT;++mode) {
  assert(player_set_loader_mode((loader_mode_t)mode));
  bus_owned=false;
  sense=1;
  uint64_t before=active_ticks;
  sharp_stream_info_t reference;assert(sharp_stream_open("dma.mzf",1,(loader_mode_t)mode,&reference));
  uint64_t expected=reference.total_ticks;sharp_stream_close();
  qd_scan_calls=0;sharp_stream_set_analysis_control(qd_count_scan,NULL);
  player_start("dma.mzf");
  assert(!qd_scan_calls);
  assert(player_get_state()==PLAYER_STATE_PLAYING && player_sharp_details()->duration_deferred && !player_sharp_details()->total_ticks);
  drain_player();
  assert(active_ticks-before==expected && !qd_scan_calls);sharp_stream_set_analysis_control(NULL,NULL);
  if(mode>=2 && mode<=4)assert(sense==1 && !monitor);
 }
 puts("PASS MZF all 17 modes: zero preflight analysis callbacks, exact output durations unchanged against measured codec");
 assert(!remove("dma.mzf"));
 large_ul800_fixture();
 for(unsigned mode=LOADER_MODE_UL;mode<=LOADER_MODE_UL_MZ700;++mode){
  assert(player_set_loader_mode((loader_mode_t)mode));
  bus_owned=false;sense=1;ul_received=ul_bit_count=ul_bits=0;
  peer_ack_at=ul_low_at=ul_longest_low=0;
  ul_read_writes=ul_last_clock_read=ul_pad_reads=0;
  verify_ul_payload=true;
  player_start("large-ul800.mzf");
  drain_player();
  assert(ul_received==44033 && ul_bit_count==0 && sense==1);
  assert(ul_longest_low>=8600U*16 && ul_longest_low<500000U*16);
#if !CMT_UL_TRACE
  assert(ul_pad_reads==0);
#endif
  verify_ul_payload=false;
 }
 assert(!remove("large-ul800.mzf"));
 puts("PASS UL/UL800/UL700 each44033 bytes: independent decode, reference edge order, late first ACK, no diagnostic GPIO reads, whole-byte returns and data pause");
 const uint8_t tap[]={2,0,0,0xA5,4,0,255,128,0,255};
 FILE *f=fopen("dma.tap","wb");
 assert(f && fwrite(tap,1,sizeof(tap),f)==sizeof(tap) && !fclose(f));
 for(unsigned speed=0;speed<4;++speed) {
  tap_stream_info_t info;
  assert(tap_stream_open("dma.tap",(tap_speed_t)speed,&info));
  tap_stream_close();
  assert(player_set_tap_speed((tap_speed_t)speed));
  bus_owned=false;
  uint64_t before=active_ticks;
  player_start("dma.tap");
  drain_player();
  assert(active_ticks-before==info.total_ticks);
 }
 assert(!remove("dma.tap"));
 wav_sparse_prefill();
 uint8_t samples[6001];
 for(unsigned i=0;i<sizeof(samples);++i)samples[i]=(i/11)%2 ? 235:20;
 assert(wav_writer_open("dma.wav",44100,8,1) && wav_writer_write(samples,sizeof(samples))==sizeof(samples) && wav_writer_finalize());
 uint64_t before=active_ticks;
 player_start("dma.wav");
 drain_player();
 assert(active_ticks-before==((uint64_t)sizeof(samples)*16000000+22050)/44100);
 assert(!remove("dma.wav"));
 const uint8_t edge_data[]={0xF6,10,0,0xF6};
 f=fopen("dma.l16","wb");
 assert(f && fwrite(edge_data,1,sizeof(edge_data),f)==sizeof(edge_data) && !fclose(f));
 player_start("dma.l16");
 assert(player_get_total_seconds()==0);
 drain_player();
 assert(!remove("dma.l16"));
 f=fopen("dma.lep","wb");
 assert(f && fwrite(edge_data,1,sizeof(edge_data),f)==sizeof(edge_data) && !fclose(f));
 player_start("dma.lep");
 assert(player_get_state()==PLAYER_STATE_PLAYING && player_get_total_seconds()==0);
 drain_player();
 assert(player_get_state()==PLAYER_STATE_EOF && player_get_progress_percent()==100);
 assert(!remove("dma.lep"));
 puts("PASS actual player/RMT DMA integration for all 17 Sharp modes including simulated UL peer, all four TAP speeds, WAV, L16 and LEP with unknown duration/EOF100; exact active duration, user-pause precedence and safe GPIO handoff");
 return 0;
}
#endif
