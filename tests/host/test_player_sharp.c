/* Integrates the real player, TAP parser, browser and raw_pulse GPTimer ISR.
 * Only ESP peripheral/RTOS functions and unused MZF/WAV hardware are mocked. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio/player.h"
#include "audio/raw_pulse.h"
#include "audio/raw_out.h"
#include "audio/mzf_stream.h"
#include "file_list.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

struct fake_timer { uint64_t now, alarm; bool running; gptimer_event_callbacks_t cb; } timer;
static uint8_t pin;
static int64_t fake_now;
int64_t esp_timer_get_time(void) { return fake_now; }
static uint8_t sense=1,write_pin,monitor;
static bool bus_owned,arm700;
esp_err_t gpio_config(const gpio_config_t *cfg) {
    assert(cfg->pin_bit_mask==(1ULL<<4) || cfg->pin_bit_mask==(1ULL<<5));
    if(cfg->pin_bit_mask==(1ULL<<5)) { bus_owned=true; arm700=player_sharp_details()->effective==LOADER_MODE_UL_MZ700; write_pin=arm700 ? 1:0; }
    return ESP_OK;
}
esp_err_t gpio_set_direction(int gpio,int mode) { (void)mode;assert(gpio==4 || gpio==7);return ESP_OK; }
esp_err_t gpio_set_level(int gpio,int value) {
    if(gpio==4) { pin=(uint8_t)value; if(bus_owned && arm700 && !value && sense) {write_pin=0;arm700=false;} }
    else if(gpio==7) { if(bus_owned && sense!=(uint8_t)value) write_pin=value ? 0:1; sense=(uint8_t)value; }
    else { assert(gpio==41); monitor=(uint8_t)value; }
    return ESP_OK;
}
int gpio_get_level(int gpio) { assert(gpio==5);return write_pin; }
void test_reg_write(uint32_t reg, uint32_t mask) { assert(mask==(1U<<4)); pin=(reg==1); }
esp_err_t gptimer_new_timer(const gptimer_config_t *cfg,gptimer_handle_t *out) {
    assert(cfg->resolution_hz==1000000); *out=&timer; return ESP_OK;
}
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t t,const gptimer_event_callbacks_t *cb,void *arg) {
    (void)arg; t->cb=*cb; return ESP_OK;
}
esp_err_t gptimer_enable(gptimer_handle_t t) { (void)t; return ESP_OK; }
esp_err_t gptimer_start(gptimer_handle_t t) { assert(!t->running); t->running=true; return ESP_OK; }
esp_err_t gptimer_stop(gptimer_handle_t t) { t->running=false; return ESP_OK; }
esp_err_t gptimer_get_raw_count(gptimer_handle_t t,uint64_t *out) { *out=t->now; return ESP_OK; }
esp_err_t gptimer_set_raw_count(gptimer_handle_t t,uint64_t value) { t->now=value; return ESP_OK; }
esp_err_t gptimer_set_alarm_action(gptimer_handle_t t,const gptimer_alarm_config_t *a) {
    assert(a->alarm_count && !a->flags.auto_reload_on_alarm); t->alarm=a->alarm_count; return ESP_OK;
}
TickType_t xTaskGetTickCount(void) { return 1; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
int xTaskCreatePinnedToCore(void (*entry)(void *),const char *name,unsigned stack,
                           void *arg,unsigned priority,TaskHandle_t *out,int core) {
    (void)entry; (void)name; (void)stack; (void)arg; (void)priority; (void)out; (void)core;
    assert(!"Host test must explicitly service the player"); return 0;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &timer; }
int xSemaphoreTake(SemaphoreHandle_t s,TickType_t ticks) { (void)s; (void)ticks; return 1; }
int xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return 1; }
void raw_out_init(int sr) { (void)sr; assert(!"TAP must not use sample output"); }
bool raw_out_start(void) { assert(!"TAP sample start"); return false; }
void raw_out_stop(void) {}
void raw_out_pause(void) { assert(!"TAP sample pause"); }
void raw_out_resume(void) { assert(!"TAP sample resume"); }
size_t raw_out_write(const uint8_t *p,size_t n) { (void)p; (void)n; return 0; }
void raw_out_finish(void) {}
size_t raw_out_free(void) { return 0; }
size_t raw_out_level(void) { return 0; }
bool raw_out_is_underrun(void) { return false; }
void raw_out_clear_underrun(void) {}
void raw_out_set_sample_output_enabled(bool value) { assert(!value); }

static void fixture(const char *name,bool two) {
    FILE *f=fopen(name,"wb");assert(f);
    for(unsigned i=0;i<(two ? 2U:1U);++i) {
        uint8_t h[128]={1},data[]={0,128,1,255};memcpy(h+1,i ? "SECOND\r":"FIRST\r",i ? 7:6);
        h[0x12]=4;h[0x15]=h[0x17]=0x12;
        assert(fwrite(h,1,128,f)==128 && fwrite(data,1,4,f)==4);
    }
    assert(!fclose(f));
}
static void drain(void) {
    unsigned budget=600000; uint16_t previous_record=0;uint8_t previous=0; bool ul_pause_checked=false;
    while(player_get_state()==PLAYER_STATE_PLAYING) {
        assert(budget--);
        if(player_is_ultrafast_active() && !ul_pause_checked) {
            player_pause_motor(); assert(player_get_state()==PLAYER_STATE_PLAYING);
            uint8_t held_pin=pin,held_sense=sense;
            player_pause_user();fake_now+=5000000;player_service();
            assert(player_get_state()==PLAYER_STATE_PAUSED && pin==held_pin && sense==held_sense);
            player_resume_user();ul_pause_checked=true;
        }
        if(raw_pulse_is_running()) {
            timer.now=timer.alarm;fake_now+=(int64_t)timer.alarm;
            gptimer_alarm_event_data_t e={timer.now,timer.alarm};timer.cb.on_alarm(&timer,&e,NULL);
        } else fake_now+=10000;
        if(previous_record!=player_sharp_details()->record) { previous=0;previous_record=player_sharp_details()->record; }
        uint8_t progress=player_get_progress_percent();
        /* UL switches from prelude to byte progress; only native progress is monotonic. */
        if(!player_sharp_details()->ultrafast) { assert(progress>=previous);previous=progress; }
        player_service();
    }
    assert(player_get_state()==PLAYER_STATE_EOF && player_get_progress_percent()==100 && !pin && !timer.running);
}
static void all_modes(void) {
    fixture("all.mzf",false);
    player_init();raw_pulse_set_read_invert(false);
    for(int mode=0;mode<17;++mode) {
        assert(player_set_loader_mode((loader_mode_t)mode));
        bus_owned=false; sense=1;
        player_start("all.mzf"); assert(player_get_state()==PLAYER_STATE_PLAYING);
        uint64_t ticks=player_sharp_details()->total_ticks;
        player_pause_user();assert(player_get_state()==PLAYER_STATE_PAUSED);player_resume_user();
        drain();
        assert(player_get_position_seconds()==ticks/16000000ULL);
        if(mode>=2 && mode<=4) assert(sense==1 && monitor==0);
    }
    assert(!remove("all.mzf"));
    puts("PASS real Sharp/player/GPTimer integration for all 17 modes, block boundaries and simulated UL peer; safe final bus");
}
static void selector(void) {
    fixture("multi.mzt",true);
    FILE *f=fopen("multi.MTI","wb");assert(f);
    fputs("RECORD=1\nTYPE=NORMAL\nSPEED=1:2\nRECORD=2\nTYPE=TC\nSPEED=1:1\n",f);assert(!fclose(f));
    assert(player_set_loader_mode(LOADER_MODE_AUTO));
    assert(player_prepare_record("multi.mzt",1));
    assert(player_get_state()==PLAYER_STATE_READY && player_sharp_details()->effective==LOADER_MODE_NORMAL_1_2);
    assert(player_set_record_mode(LOADER_MODE_NORMAL_1_4));
    assert(player_prepare_record("multi.mzt",2));
    assert(player_sharp_details()->effective==LOADER_MODE_TC_1_1 && player_sharp_details()->sidecar);
    assert(player_prepare_record("multi.mzt",1));assert(player_sharp_details()->effective==LOADER_MODE_NORMAL_1_4);
    assert(player_prepare_record("multi.mzt",2));
    player_start("multi.mzt");assert(!strcmp(player_sharp_details()->title,"SECOND"));drain();
    assert(player_prepare_record("multi.mzt",1));player_start("multi.mzt");assert(!strcmp(player_sharp_details()->title,"FIRST"));
    drain();assert(player_sharp_details()->record==2 && player_sharp_details()->effective==LOADER_MODE_TC_1_1);
    assert(player_prepare_record("multi.mzt",1));player_start("multi.mzt");
    player_stop();assert(!pin && !timer.running);player_stop();
    assert(!remove("multi.mzt") && !remove("multi.MTI"));
    puts("PASS MZT selector and per-record profiles survive navigation; AUTO MTI is isolated, chosen record only, STOP/replay");
}
static void mzt_continuation(void) {
 fixture("sequence.mzt",true);
 const loader_mode_t pairs[][2]={{LOADER_MODE_NORMAL_1_1,LOADER_MODE_NORMAL_1_2},
  {LOADER_MODE_IC_1_4,LOADER_MODE_TC_1_3},{LOADER_MODE_UL,LOADER_MODE_NORMAL_1_1},
  {LOADER_MODE_UL,LOADER_MODE_UL_MZ800},{LOADER_MODE_NORMAL_1_1,LOADER_MODE_UL_MZ700}};
 for(unsigned i=0;i<sizeof(pairs)/sizeof(pairs[0]);++i) {
  bus_owned=false;sense=1;
  assert(player_prepare_record("sequence.mzt",1));assert(player_set_record_mode(pairs[i][0]));
  assert(player_prepare_record("sequence.mzt",2));assert(player_set_record_mode(pairs[i][1]));
  assert(player_prepare_record("sequence.mzt",1));player_start("sequence.mzt");drain();
  bool stop_ul=pairs[i][0]==LOADER_MODE_UL && pairs[i][1]==LOADER_MODE_UL_MZ800;
  assert(player_sharp_details()->record==(stop_ul ? 1:2));
  assert(player_sharp_details()->effective==(stop_ul ? pairs[i][0]:pairs[i][1]));
 }
 assert(!remove("sequence.mzt"));
 puts("PASS MZT continuous native/turbo and mixed UL records; each record keeps its profile; UL-to-UL stops per reference");
}
int main(void) { all_modes();selector();mzt_continuation();return 0; }
