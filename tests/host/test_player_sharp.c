/* Integrates the real player, TAP parser, browser and raw_pulse GPTimer ISR.
 * Only ESP peripheral/RTOS functions and unused MZF/WAV hardware are mocked. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "audio/player.h"
#include "app/cmt_bus_policy.h"
#include "audio/raw_pulse.h"
#include "audio/raw_out.h"
#include "audio/mzf_stream.h"
#include "file_list.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

struct fake_timer { uint64_t now, alarm;
bool running;
gptimer_event_callbacks_t cb;
} timer;
static uint8_t pin;
static int64_t fake_now;
static bool scheduling_probe;
static void (*scheduler_entry)(void *);
static jmp_buf scheduler_exit;
static TickType_t scheduler_start,scheduler_last;
static unsigned scheduler_delays;
static bool bus_owned,arm700,ul_peer_disabled,ul_ack_disabled;
int64_t esp_timer_get_time(void) {
if(bus_owned && mzf_stream_is_ultrafast_active())++fake_now;
return fake_now;
}
static uint8_t sense=1,write_pin,monitor;
esp_err_t gpio_config(const gpio_config_t *cfg) {
    assert(cfg->pin_bit_mask==(1ULL<<4) || cfg->pin_bit_mask==(1ULL<<5));
    if(cfg->pin_bit_mask==(1ULL<<5)) { bus_owned=true;
    arm700=player_sharp_details()->effective==LOADER_MODE_UL_MZ700;
    write_pin=arm700 ? 1:0;
    }
    return ESP_OK;
}
esp_err_t gpio_set_direction(int gpio,int mode) { (void)mode;
assert(gpio==4 || gpio==7);
return ESP_OK;
}
esp_err_t gpio_set_level(int gpio,int value) {
    if(gpio==4) { pin=(uint8_t)value;
    if(bus_owned && arm700 && !value && sense) {write_pin=0;
    arm700=false;
    } }
    else if(gpio==7) {
    /* MZ700 peer arms on READ first; initial SENSE release is not data. */
    if(bus_owned && !arm700 && sense!=(uint8_t)value) write_pin=(value || ul_ack_disabled) ? 0:1;
    sense=(uint8_t)value;
    }
    else { assert(gpio==41);
    monitor=(uint8_t)value;
    }
    return ESP_OK;
}
int gpio_get_level(int gpio) {
if(gpio==4)return pin;
if(gpio==7)return sense;
if(gpio==6)return 1;
assert(gpio==5);
return ul_peer_disabled ? 1:write_pin;
}
void test_reg_write(uint32_t reg, uint32_t mask) { assert(mask==(1U<<4));
pin=(reg==1);
}
esp_err_t gptimer_new_timer(const gptimer_config_t *cfg,gptimer_handle_t *out) {
    assert(cfg->resolution_hz==1000000);
    *out=&timer;
    return ESP_OK;
}
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t t,const gptimer_event_callbacks_t *cb,void *arg) {
    (void)arg;
    t->cb=*cb;
    return ESP_OK;
}
esp_err_t gptimer_enable(gptimer_handle_t t) { (void)t;
return ESP_OK;
}
esp_err_t gptimer_start(gptimer_handle_t t) { assert(!t->running);
t->running=true;
return ESP_OK;
}
esp_err_t gptimer_stop(gptimer_handle_t t) { t->running=false;
return ESP_OK;
}
esp_err_t gptimer_get_raw_count(gptimer_handle_t t,uint64_t *out) { *out=t->now;
return ESP_OK;
}
esp_err_t gptimer_set_raw_count(gptimer_handle_t t,uint64_t value) { t->now=value;
return ESP_OK;
}
esp_err_t gptimer_set_alarm_action(gptimer_handle_t t,const gptimer_alarm_config_t *a) {
    assert(a->alarm_count && !a->flags.auto_reload_on_alarm);
    t->alarm=a->alarm_count;
    return ESP_OK;
}
TickType_t xTaskGetTickCount(void) { return (TickType_t)((fake_now/1000)/portTICK_PERIOD_MS);
}
void vTaskDelay(TickType_t ticks) {
if(scheduling_probe){
 TickType_t now=xTaskGetTickCount();
 TickType_t since=(TickType_t)(now-(scheduler_delays ? scheduler_last:scheduler_start));
 assert(ticks==1 && since*portTICK_PERIOD_MS>=100 && since*portTICK_PERIOD_MS<=110 && sense==1);
 scheduler_last=now+ticks;
 if(++scheduler_delays==2)longjmp(scheduler_exit,1);
 fake_now+=(int64_t)ticks*portTICK_PERIOD_MS*1000;
}
}
int xTaskCreatePinnedToCore(void (*entry)(void *),const char *name,unsigned stack,
                           void *arg,unsigned priority,TaskHandle_t *out,int core) {
    (void)entry;
    (void)name;
    (void)stack;
    (void)arg;
    (void)priority;
    (void)out;
    (void)core;
    assert(scheduling_probe);scheduler_entry=entry;*out=&timer;
    return 1;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &timer;
}
int xSemaphoreTake(SemaphoreHandle_t s,TickType_t ticks) { (void)s;
(void)ticks;
return 1;
}
int xSemaphoreGive(SemaphoreHandle_t s) { (void)s;
return 1;
}
void raw_out_init(int sr) { (void)sr;
assert(!"TAP must not use sample output");
}
bool raw_out_start(void) { assert(!"TAP sample start");
return false;
}
void raw_out_stop(void) {}
void raw_out_pause(void) { assert(!"TAP sample pause");
}
void raw_out_resume(void) { assert(!"TAP sample resume");
}
size_t raw_out_write(const uint8_t *p,size_t n) { (void)p;
(void)n;
return 0;
}
void raw_out_finish(void) {}
size_t raw_out_free(void) { return 0;
}
size_t raw_out_level(void) { return 0;
}
bool raw_out_is_underrun(void) { return false;
}
void raw_out_clear_underrun(void) {}
void raw_out_set_sample_output_enabled(bool value) { assert(!value);
}

static void fixture(const char *name,bool two) {
    FILE *f=fopen(name,"wb");
    assert(f);
    for(unsigned i=0;i<(two ? 2U:1U);++i) {
        uint8_t h[128]={1},data[]={0,128,1,255};
        memcpy(h+1,i ? "SECOND\r":"FIRST\r",i ? 7:6);
        h[0x12]=4;
        h[0x15]=h[0x17]=0x12;
        assert(fwrite(h,1,128,f)==128 && fwrite(data,1,4,f)==4);
    }
    assert(!fclose(f));
}
static uint64_t drained_records;
static void drain(void) {
    drained_records=0;
    unsigned budget=600000;
    uint16_t previous_record=0;
    uint8_t previous=0;
    bool ul_pause_checked=false;
    while(player_get_state()==PLAYER_STATE_PLAYING) {
        assert(budget--);
        if(player_sharp_details()->record<64)drained_records|=1ULL<<player_sharp_details()->record;
        /* Exercise the same automatic policy through every native region.
         * Also simulate a stale policy attempting both levels during UL. */
        bool ul=player_is_ultrafast_active();
        if(ul)assert(player_get_total_seconds()==0);
        uint8_t before=sense;
        cmt_bus_outputs_t out=cmt_bus_outputs(false,ul,false,player_get_state(),RECORDER_STATE_IDLE,false);
        assert(player_set_sense_level(out.sense_high)==!ul);
        if(ul) { assert(!player_set_sense_level(!before) && sense==before); }
        else assert(sense==0);
        if(player_is_ultrafast_active() && !ul_pause_checked) {
            player_pause_motor();
            assert(player_get_state()==PLAYER_STATE_PLAYING);
            uint8_t held_pin=pin,held_sense=sense;
            player_pause_user();
            fake_now+=5000000;
            player_service();
            assert(player_get_state()==PLAYER_STATE_PAUSED && pin==held_pin && sense==held_sense);
            player_resume_user();
            ul_pause_checked=true;
        }
        if(raw_pulse_is_running()) {
            timer.now=timer.alarm;
            fake_now+=(int64_t)timer.alarm;
            gptimer_alarm_event_data_t e={timer.now,timer.alarm};
            timer.cb.on_alarm(&timer,&e,NULL);
        } else fake_now+=10000;
        if(previous_record!=player_sharp_details()->record) { previous=0;
        previous_record=player_sharp_details()->record;
        }
        uint8_t progress=player_get_progress_percent();
        /* UL switches from prelude to byte progress; only native progress is monotonic. */
        if(!player_sharp_details()->ultrafast) { assert(progress>=previous);
        previous=progress;
        }
        player_service();
    }
    if(player_sharp_details()->format==FILE_FORMAT_MZQ)assert(player_get_state()==PLAYER_STATE_READY);
    else assert(player_get_state()==PLAYER_STATE_EOF && player_get_progress_percent()==100);
    assert(!pin && !timer.running);
}
static void all_modes(void) {
    fixture("all.mzf",false);
    player_init();
    raw_pulse_set_read_invert(false);
    for(int mode=0;mode<17;++mode) {
        assert(player_set_loader_mode((loader_mode_t)mode));
        bus_owned=false;
        sense=1;
        sharp_stream_info_t reference;
        assert(sharp_stream_open("all.mzf",1,(loader_mode_t)mode,&reference));
        uint64_t ticks=reference.total_ticks;sharp_stream_close();
        player_start("all.mzf");
        assert(player_get_state()==PLAYER_STATE_PLAYING);
        program_metadata_t metadata;
        player_metadata(&metadata);
        assert(metadata.declared_size==4 && metadata.load==0x1200 && metadata.exec==0x1200 && (metadata.valid&PROGRAM_TRANSFER_MODE));
        assert(player_sharp_details()->total_ticks>0 && !player_sharp_details()->duration_deferred);
        if(mode==LOADER_MODE_IC_1_4) {
            player_pause_motor();
            assert(player_get_state()==PLAYER_STATE_PAUSED);
            cmt_bus_outputs_t out=cmt_bus_outputs(false,false,false,player_get_state(),RECORDER_STATE_IDLE,false);
            assert(player_set_sense_level(out.sense_high) && sense==0);
            player_resume_motor();
            assert(player_get_state()==PLAYER_STATE_PLAYING && sense==0);
        }
        player_pause_user();
        assert(player_get_state()==PLAYER_STATE_PAUSED);
        player_resume_user();
        drain();
        assert(player_get_position_seconds()==ticks/16000000ULL);
        if(mode>=2 && mode<=4) assert(sense==1 && monitor==0);
    }
    assert(!remove("all.mzf"));
    puts("PASS automatic SENSE LOW across all 17 native profiles and IC 1:4 motor pause; stale policy cannot overwrite UL; safe final bus");
}
static void selector(void) {
    fixture("multi.mzt",true);
    FILE *f=fopen("multi.MTI","wb");
    assert(f);
    fputs("RECORD=1\nTYPE=NORMAL\nSPEED=1:2\nRECORD=2\nTYPE=TC\nSPEED=1:1\n",f);
    assert(!fclose(f));
    assert(player_set_loader_mode(LOADER_MODE_AUTO));
    assert(player_prepare_record("multi.mzt",1));
    assert(player_get_state()==PLAYER_STATE_READY && player_sharp_details()->effective==LOADER_MODE_NORMAL_1_2);
    assert(player_set_record_mode(LOADER_MODE_NORMAL_1_4));
    assert(player_prepare_record("multi.mzt",2));
    assert(player_sharp_details()->effective==LOADER_MODE_TC_1_1 && player_sharp_details()->sidecar);
    assert(player_prepare_record("multi.mzt",1));
    assert(player_sharp_details()->effective==LOADER_MODE_NORMAL_1_4);
    assert(player_prepare_record("multi.mzt",2));
    player_start("multi.mzt");
    assert(!strcmp(player_sharp_details()->title,"SECOND"));
    drain();
    assert(player_prepare_record("multi.mzt",1));
    player_start("multi.mzt");
    assert(!strcmp(player_sharp_details()->title,"FIRST"));
    drain();
    program_metadata_t next;
    player_metadata(&next);
    assert(next.record==2 && !strcmp(next.title,"SECOND"));
    assert(player_sharp_details()->record==2 && player_sharp_details()->effective==LOADER_MODE_TC_1_1);
    assert(player_prepare_record("multi.mzt",1));
    player_start("multi.mzt");
    player_stop();
    assert(!pin && !timer.running);
    player_stop();
    assert(!remove("multi.mzt") && !remove("multi.MTI"));
    puts("PASS MZT selector and per-record profiles survive navigation; AUTO MTI is isolated, chosen record only, STOP/replay");
}
static void mzt_continuation(void) {
 fixture("sequence.mzt",true);
 const loader_mode_t pairs[][2]={{LOADER_MODE_NORMAL_1_1,LOADER_MODE_NORMAL_1_2},
  {LOADER_MODE_IC_1_4,LOADER_MODE_TC_1_3},{LOADER_MODE_UL,LOADER_MODE_NORMAL_1_1},
  {LOADER_MODE_UL,LOADER_MODE_UL_MZ800},{LOADER_MODE_NORMAL_1_1,LOADER_MODE_UL_MZ700}};
 for(unsigned i=0;i<sizeof(pairs)/sizeof(pairs[0]);++i) {
  bus_owned=false;
  sense=1;
  assert(player_prepare_record("sequence.mzt",1));
  assert(player_set_record_mode(pairs[i][0]));
  assert(player_prepare_record("sequence.mzt",2));
  assert(player_set_record_mode(pairs[i][1]));
  assert(player_prepare_record("sequence.mzt",1));
  player_start("sequence.mzt");
  drain();
  bool stop_ul=pairs[i][0]==LOADER_MODE_UL && pairs[i][1]==LOADER_MODE_UL_MZ800;
  assert(player_sharp_details()->record==(stop_ul ? 1:2));
  assert(player_sharp_details()->effective==(stop_ul ? pairs[i][0]:pairs[i][1]));
 }
 assert(!remove("sequence.mzt"));
 puts("PASS MZT continuous native/turbo and mixed UL records; each record keeps its profile; UL-to-UL stops per reference");
}

static unsigned analysis_calls;
static bool count_analysis(void *unused){(void)unused;++analysis_calls;return false;}
static void qd_selector_session(void){
 FILE *f=fopen("session.mzq","wb");assert(f);
 const uint8_t count[]={0,0x16,0x16,0xa5,4,'C','R','C'};
 const uint8_t frame_h[]={0,0x16,0x16,0xa5,0,64,0};
 const uint8_t frame_d[]={0,0x16,0x16,0xa5,1,4,0};
 const uint8_t data[]={0,128,1,255};
 assert(fwrite(count,1,sizeof(count),f)==sizeof(count));
 for(unsigned i=0;i<2;++i){
  uint8_t h[64]={1};memcpy(h+1,i ? "SECOND":"FIRST",i ? 6:5);h[20]=4;h[23]=h[25]=0x12;
  assert(fwrite(frame_h,1,sizeof(frame_h),f)==sizeof(frame_h) && fwrite(h,1,64,f)==64 && fwrite("CRC",1,3,f)==3);
  assert(fwrite(frame_d,1,sizeof(frame_d),f)==sizeof(frame_d) && fwrite(data,1,4,f)==4 && fwrite("CRC",1,3,f)==3);
 }
 assert(!fclose(f));
 player_stop();assert(player_set_loader_mode(LOADER_MODE_NORMAL_1_1));
 analysis_calls=0;sharp_stream_set_analysis_control(count_analysis,NULL);
 assert(player_prepare_record("session.mzq",1));
 assert(analysis_calls>0 && !player_sharp_details()->source_reused && player_sharp_details()->duration_deferred);
 unsigned after_first=analysis_calls;
 assert(player_get_position_seconds()==0 && player_get_progress_percent()==0 && player_get_total_seconds()==0);
 assert(player_prepare_record("session.mzq",2));
 assert(player_sharp_details()->record==2 && !strcmp(player_sharp_details()->title,"SECOND") && player_sharp_details()->source_reused && player_sharp_details()->duration_deferred);
 unsigned after_second=analysis_calls;assert(after_second==after_first); /* First-ever visit to second record performs no scan. */
 for(unsigned i=0;i<6;++i){assert(player_prepare_record("session.mzq",i%2 ? 2:1));assert(player_sharp_details()->source_reused && player_sharp_details()->duration_deferred);}
 assert(analysis_calls==after_second); /* No rescan or duration dry-run on revisit. */
 assert(player_sharp_details()->total_ticks==0);
 player_start(player_get_current_path()); /* Selected path may alias the player buffer. */
 assert(player_get_state()==PLAYER_STATE_PLAYING && player_sharp_details()->record==2);
 assert(analysis_calls>after_second && !player_sharp_details()->duration_deferred); /* Full duration is deferred until PLAY, as in reference. */
 uint64_t duration=player_sharp_details()->total_ticks;
 unsigned after_play=analysis_calls;
 drain();assert(player_get_position_seconds()==0 && duration>0 && drained_records==(1ULL<<2));
 assert(player_prepare_record("session.mzq",1));assert(analysis_calls==after_play); /* EOF selector retains index. */
 player_start(player_get_current_path());after_play=analysis_calls;drain();
 assert(drained_records==((1ULL<<1)|(1ULL<<2)) && analysis_calls==after_play);
 player_stop();assert(player_prepare_record("session.mzq",1) && analysis_calls>after_play);
 player_stop();sharp_stream_set_analysis_control(NULL,NULL);assert(!remove("session.mzq"));
 puts("PASS QD selector retains validated directory/durations, first visits and revisits avoid analysis; EOF/auto-continuation reuse the index; STOP invalidates cache; selected-path alias safe");
}


static void fast_mzf_start(void){
 const char *path="fast.mzf";const unsigned payload_size=16384;
 FILE *f=fopen(path,"wb");assert(f);uint8_t h[128]={1};h[19]=payload_size>>8;h[21]=h[23]=0x12;
 assert(fwrite(h,1,128,f)==128);for(unsigned i=0;i<payload_size;++i)assert(fputc((uint8_t)i,f)!=EOF);assert(!fclose(f));
 player_stop();assert(player_set_loader_mode(LOADER_MODE_NORMAL_1_1));analysis_calls=0;sharp_stream_set_analysis_control(count_analysis,NULL);
 player_start(path);assert(player_get_state()==PLAYER_STATE_PLAYING && analysis_calls>0 && analysis_calls<=35 && !player_get_progress_percent());
 assert(!player_sharp_details()->duration_deferred && player_sharp_details()->total_ticks>0);
 assert(player_get_total_seconds()>0);
 assert(mzf_stream_transfer_bytes()==payload_size);
 unsigned budget=500000;bool checked_pause=false,checked_data=false;
 while(player_get_state()==PLAYER_STATE_PLAYING){
  assert(budget--);
  if(raw_pulse_is_running()){timer.now=timer.alarm;fake_now+=(int64_t)timer.alarm;
   gptimer_alarm_event_data_t e={timer.now,timer.alarm};timer.cb.on_alarm(&timer,&e,NULL);
  }else fake_now+=10000;
  player_service();
  if(!raw_pulse_is_running() || player_get_state()!=PLAYER_STATE_PLAYING)continue;
  uint32_t done=raw_pulse_completed_source_bytes();uint8_t progress=player_get_progress_percent();
  /* Header and future prefetched payload cannot advance the data bar. */
  if(!done)assert(!progress);
  if(done>100 && done<payload_size/2){
   assert(progress==(uint64_t)done*100/payload_size);checked_data=true;
   if(!checked_pause){player_pause_user();uint8_t held=player_get_progress_percent();uint32_t bytes=raw_pulse_completed_source_bytes();
    fake_now+=5000000;player_service();assert(player_get_progress_percent()==held && raw_pulse_completed_source_bytes()==bytes);
    player_resume_user();checked_pause=true;
   }
  }
 }
 assert(player_get_state()==PLAYER_STATE_EOF && player_get_progress_percent()==100 && checked_data && checked_pause && analysis_calls>0 && analysis_calls<=35);
 player_stop();sharp_stream_set_analysis_control(NULL,NULL);assert(!remove(path));
 /* Truncated declared data is rejected before output despite fast start. */
 f=fopen("fast-bad.mzf","wb");assert(f && fwrite(h,1,128,f)==128 && !fclose(f));
 player_start("fast-bad.mzf");assert(player_get_state()==PLAYER_STATE_ERROR && !raw_pulse_is_running());
 player_stop();assert(!remove("fast-bad.mzf"));
 puts("PASS fast MZF: bounded byte duration scan, queued/header pulses excluded from data progress, exact completed bytes, pause freeze, EOF100 and truncated-file refusal");
}

static void qd_stop_reuse(void){
 const char *path="stop.mzq";FILE *f=fopen(path,"wb");assert(f);
 const uint8_t count[]={0,0x16,0x16,0xa5,6,'C','R','C'},fh[]={0,0x16,0x16,0xa5,0,64,0},fd[]={0,0x16,0x16,0xa5,1,0,1};
 uint8_t data[256];for(unsigned i=0;i<sizeof(data);++i)data[i]=(uint8_t)i;
 assert(fwrite(count,1,sizeof(count),f)==sizeof(count));
 for(unsigned i=0;i<3;++i){uint8_t h[64]={1};h[1]=(uint8_t)('A'+i);h[21]=1;h[23]=h[25]=0x12;
  assert(fwrite(fh,1,sizeof(fh),f)==sizeof(fh) && fwrite(h,1,64,f)==64 && fwrite("CRC",1,3,f)==3);
  assert(fwrite(fd,1,sizeof(fd),f)==sizeof(fd) && fwrite(data,1,sizeof(data),f)==sizeof(data) && fwrite("CRC",1,3,f)==3);
 }
 assert(!fclose(f));
 const loader_mode_t modes[]={LOADER_MODE_NORMAL_1_1,LOADER_MODE_NORMAL_1_1,LOADER_MODE_UL_MZ800,LOADER_MODE_UL_MZ700};
 player_stop();assert(player_set_loader_mode(LOADER_MODE_NORMAL_1_1));analysis_calls=0;
 sharp_stream_set_analysis_control(count_analysis,NULL);
 for(unsigned pass=0;pass<4;++pass){
  bus_owned=false;sense=1;
  assert(player_prepare_record(path,2) && player_set_record_mode(modes[pass]));
  player_start(player_get_current_path());unsigned after_start=analysis_calls;
  if(pass>=2){unsigned budget=100000;
   while(!mzf_stream_get_ultrafast_sent_bytes()){
    assert(budget-- && player_get_state()==PLAYER_STATE_PLAYING);
    if(raw_pulse_is_running()){timer.now=timer.alarm;fake_now+=(int64_t)timer.alarm;
     gptimer_alarm_event_data_t e={timer.now,timer.alarm};timer.cb.on_alarm(&timer,&e,NULL);
    }else fake_now+=10000;
    player_service();
   }
  }
  if(pass>=2)assert(player_is_ultrafast_active());
  if(pass==1 || pass==3)player_pause_user();
  assert(player_stop_to_selector());
  assert(player_get_state()==PLAYER_STATE_READY && !raw_pulse_is_running() && !player_is_ultrafast_active());
  assert(!pin && sense==1 && analysis_calls==after_start && player_sharp_details()->record==2);
  assert(player_sharp_details()->source_reused && player_sharp_details()->effective==modes[pass]);
  assert(!strcmp(player_stop_cause_name(),"BUTTON_STOP"));
  assert(player_stop_to_selector() && analysis_calls==after_start); /* EOF/STOP race already reached READY. */
  assert(player_prepare_record(path,1) && player_prepare_record(path,2) && analysis_calls==after_start);
  player_start(player_get_current_path());assert(analysis_calls==after_start);
  assert(player_stop_to_selector() && analysis_calls==after_start);
  /* Leaving the selector/media removal must still discard the source. */
  player_stop_for(pass&1 ? PLAYER_STOP_SD_REMOVED:PLAYER_STOP_BUTTON);
  assert(!player_stop_to_selector());
  assert(player_prepare_record(path,2) && analysis_calls>after_start && !player_sharp_details()->source_reused);
 }
 player_stop();sharp_stream_set_analysis_control(NULL,NULL);assert(!remove(path));
 puts("PASS QD PLAY/STOP/replay and paused/active UL STOP retain index/profile without analysis; READ/SENSE released; selector exit and media removal invalidate");
}

static void qd_auto_sequence(void){
 FILE *f=fopen("auto.mzq","wb");assert(f);
 const uint8_t count[]={0,0x16,0x16,0xa5,6,'C','R','C'};
 const uint8_t frame_h[]={0,0x16,0x16,0xa5,0,64,0},frame_d[]={0,0x16,0x16,0xa5,1,4,0};
 const char *titles[]={"FIRST","SECOND","THIRD"};const uint8_t data[]={0,128,1,255};
 assert(fwrite(count,1,sizeof(count),f)==sizeof(count));
 for(unsigned i=0;i<3;++i){uint8_t h[64]={1};memcpy(h+1,titles[i],strlen(titles[i]));h[20]=4;h[23]=h[25]=0x12;
  assert(fwrite(frame_h,1,sizeof(frame_h),f)==sizeof(frame_h) && fwrite(h,1,64,f)==64 && fwrite("CRC",1,3,f)==3);
  assert(fwrite(frame_d,1,sizeof(frame_d),f)==sizeof(frame_d) && fwrite(data,1,4,f)==4 && fwrite("CRC",1,3,f)==3);
 }
 assert(!fclose(f));
 qd_stop_reuse();
 const loader_mode_t profiles[][3]={{LOADER_MODE_NORMAL_1_1,LOADER_MODE_IC_1_4,LOADER_MODE_TC_1_3},
  {LOADER_MODE_UL_MZ800,LOADER_MODE_NORMAL_1_1,LOADER_MODE_UL_MZ700},
  {LOADER_MODE_UL,LOADER_MODE_UL_MZ800,LOADER_MODE_NORMAL_1_1}};
 for(unsigned pass=0;pass<3;++pass){
  player_stop();assert(player_set_loader_mode(LOADER_MODE_NORMAL_1_1));bus_owned=false;sense=1;
  analysis_calls=0;sharp_stream_set_analysis_control(count_analysis,NULL);
  for(unsigned i=0;i<3;++i){assert(player_prepare_record("auto.mzq",i+1));assert(player_set_record_mode(profiles[pass][i]));}
  assert(player_prepare_record("auto.mzq",1));player_start(player_get_current_path());unsigned after_start=analysis_calls;
  drain();assert(drained_records==14 && analysis_calls==after_start);
  assert(player_get_state()==PLAYER_STATE_READY && player_sharp_details()->record==3 && !strcmp(player_sharp_details()->title,"THIRD"));
  assert(player_sharp_details()->effective==profiles[pass][2] && player_sharp_details()->source_reused);
  program_metadata_t m;player_metadata(&m);assert(m.record==3 && m.records==3);
  assert(player_prepare_record("auto.mzq",2) && analysis_calls==after_start);
 }
 player_stop();sharp_stream_set_analysis_control(NULL,NULL);assert(!remove("auto.mzq"));
 puts("PASS QD auto-continuation: three records, individual NORMAL/IC/TC/UL profiles including UL-to-UL, no analysis between records or on final selector, no wrap");
}

static void ul_timeout_preserved(void){
 fixture("ul-no-peer.mzf",false);
 player_stop();assert(player_set_loader_mode(LOADER_MODE_UL_MZ800));
 ul_peer_disabled=true;bus_owned=false;sense=1;
 player_start("ul-no-peer.mzf");
 assert(player_get_state()==PLAYER_STATE_PLAYING);
 unsigned budget=100000;
 while(player_get_state()==PLAYER_STATE_PLAYING){
  assert(budget--);
  if(raw_pulse_is_running()){
   timer.now=timer.alarm;fake_now+=(int64_t)timer.alarm;
   gptimer_alarm_event_data_t event={timer.now,timer.alarm};timer.cb.on_alarm(&timer,&event,NULL);
  }else fake_now+=10000;
  player_service();
 }
 assert(player_get_state()==PLAYER_STATE_ERROR && !strcmp(player_error_message(),"UL TIMEOUT"));
 assert(!raw_pulse_is_running() && !pin && sense==1 && !monitor);
 assert(!mzf_stream_error()[0]); /* Stream closed/reset; published player reason remains valid. */
 assert(!strcmp(player_error_message(),"UL TIMEOUT"));
 ul_peer_disabled=false;player_stop();assert(!remove("ul-no-peer.mzf"));
 puts("PASS no UL peer: timeout reason survives stream close, safe READ/SENSE/monitor outputs, not generic SOURCE/TX error");
}

static void ul_missing_ack_timeout(void){
 fixture("ul-no-ack.mzf",false);
 player_stop();assert(player_set_loader_mode(LOADER_MODE_UL_MZ800));
 ul_ack_disabled=true;bus_owned=false;sense=1;
 player_start("ul-no-ack.mzf");
 unsigned budget=100000;
 while(player_get_state()==PLAYER_STATE_PLAYING){
  assert(budget--);
  if(raw_pulse_is_running()){
   timer.now=timer.alarm;fake_now+=(int64_t)timer.alarm;
   gptimer_alarm_event_data_t event={timer.now,timer.alarm};timer.cb.on_alarm(&timer,&event,NULL);
  }else fake_now+=10000;
  player_service();
 }
 assert(player_get_state()==PLAYER_STATE_ERROR && !strcmp(player_error_message(),"UL TIMEOUT"));
 assert(sense==1 && !pin && !monitor && !raw_pulse_is_running());
 assert(!mzf_stream_error()[0]); /* Adapter closed, immutable published reason survives. */
 ul_ack_disabled=false;player_stop();assert(!remove("ul-no-ack.mzf"));
 puts("PASS missing data ACK: reference 500ms timeout, safe SENSE HIGH/read/monitor and immutable error after close");
}
static void ul_task_schedule(void){
 FILE *f=fopen("ul-schedule.mzf","wb");assert(f);
 uint8_t header[128]={1};header[19]=0x80;header[21]=header[23]=0x12;
 assert(fwrite(header,1,128,f)==128);
 for(unsigned i=0;i<32768;++i)assert(fputc((uint8_t)i,f)!=EOF);
 assert(!fclose(f));player_stop();assert(player_set_loader_mode(LOADER_MODE_UL_MZ800));
 bus_owned=false;sense=1;player_start("ul-schedule.mzf");
 unsigned budget=100000;
 while(!mzf_stream_get_ultrafast_sent_bytes()){
  assert(budget-- && player_get_state()==PLAYER_STATE_PLAYING);
  if(raw_pulse_is_running()){
   timer.now=timer.alarm;fake_now+=(int64_t)timer.alarm;
   gptimer_alarm_event_data_t event={timer.now,timer.alarm};timer.cb.on_alarm(&timer,&event,NULL);
  }else fake_now+=10000;
  player_service();
 }
 assert(sense==1 && player_is_ultrafast_active());
 scheduling_probe=true;scheduler_delays=0;scheduler_start=xTaskGetTickCount();
 player_task_start();assert(scheduler_entry);
 if(setjmp(scheduler_exit)==0)scheduler_entry(NULL);
 scheduling_probe=false;
 assert(scheduler_delays==2 && sense==1 && player_get_state()==PLAYER_STATE_PLAYING);
 player_stop();assert(!remove("ul-schedule.mzf"));
 puts("PASS real UL task loop: no per-batch sleep, periodic 1-tick idle after100-110ms of work, only at SENSE HIGH; STOP cleanup");
}
int main(void) { setvbuf(stdout,NULL,_IONBF,0); all_modes();
fast_mzf_start();
qd_selector_session();
qd_auto_sequence();
selector();
mzt_continuation();
ul_timeout_preserved();
ul_missing_ack_timeout();
ul_task_schedule();
return 0;
}
