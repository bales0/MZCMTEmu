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

struct fake_timer { uint64_t now, alarm;
bool running;
gptimer_event_callbacks_t cb;
} timer;
static uint8_t pin;
static int64_t fake_now;
int64_t esp_timer_get_time(void) { return fake_now;
}
esp_err_t gpio_set_level(int gpio,int value) { assert(gpio==7 && (value==0 || value==1)); return ESP_OK; }
static bool inverted;
static uint64_t expected_ticks, expected_us;
static size_t emitted;
static const uint16_t timing[4][5]={{9798,2321,2979,7749,3874},
    {7621,1805,2317,6027,3013},{6532,1547,1986,5166,2583},{5487,1300,1668,4340,2170}};
esp_err_t gpio_config(const gpio_config_t *cfg) { assert(cfg->pin_bit_mask==(1ULL<<4));
return ESP_OK;
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
TickType_t xTaskGetTickCount(void) { return 1;
}
void vTaskDelay(TickType_t ticks) { (void)ticks;
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
    assert(!"Host test must explicitly service the player");
    return 0;
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
/* These alternative source/driver paths must never start during TAP. */
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
bool mzf_stream_open(const char *p,mzf_stream_info_t *i) { (void)p;
(void)i;
assert(!"TAP MZF open");
return false;
}
void mzf_stream_close(void) {}
void mzf_stream_pause_ul(bool pause) { (void)pause;
}
static sharp_stream_info_t fake_sharp;
const sharp_stream_info_t *mzf_stream_details(void) { return &fake_sharp;
}
bool mzf_stream_at_boundary(void) { return false;
}
bool mzf_stream_advance(void) { return false;
}
uint32_t mzf_stream_boundary_delay_us(bool seen) { (void)seen;
return 0;
}
bool mzf_stream_set_loader_mode(loader_mode_t m) { (void)m;
return true;
}
loader_mode_t mzf_stream_get_loader_mode(void) { return LOADER_MODE_NORMAL_1_1;
}
void mzf_stream_clear_selection(void) {}
bool mzf_stream_select_mode(const char *p,uint16_t r,loader_mode_t m) { (void)p;
(void)r;
(void)m;
return false;
}
bool mzf_stream_get_ultrafast_enabled(void) { return false;
}
void mzf_stream_set_ultrafast_enabled(bool value) { assert(!value);
}
bool mzf_stream_is_ultrafast_ready(void) { return false;
}
bool mzf_stream_is_ultrafast_active(void) { return false;
}
bool mzf_stream_begin_ultrafast(void) { return false;
}
void mzf_stream_service_ultrafast(void) {}
bool mzf_stream_is_eof(void) { return false;
}
bool mzf_stream_has_error(void) { return false;
}
bool mzf_stream_next_pulse(uint8_t *l,uint32_t *us) { (void)l;
(void)us;
return false;
}
uint32_t mzf_stream_get_samples_emitted(void) { return 0;
}
uint32_t mzf_stream_get_ultrafast_total_bytes(void) { return 0;
}
uint32_t mzf_stream_get_ultrafast_sent_bytes(void) { return 0;
}
uint32_t mzf_stream_get_baud_rate(void) { return 0;
}

static void fire(void) {
    assert(timer.running && raw_pulse_is_running());
    timer.now=timer.alarm;
    fake_now+=(int64_t)timer.alarm;
    gptimer_alarm_event_data_t event={timer.now,timer.alarm};
    assert(!timer.cb.on_alarm(&timer,&event,NULL));
}
static void expect(uint8_t level,uint32_t ticks) {
    assert(player_get_state()==PLAYER_STATE_PLAYING);
    /* Entire source is queued before the final physical interval completes.
       MOTOR must pause it, not interpret source EOF as physical EOF. */
    if (emitted==11386) {
        player_pause_motor();
        assert(player_get_state()==PLAYER_STATE_PAUSED);
        assert(player_get_pause_reason()==PLAYER_PAUSE_MOTOR);
        player_service();
        assert(player_get_progress_percent()<100);
        player_resume_motor();
        assert(player_get_state()==PLAYER_STATE_PLAYING);
    }
    expected_ticks+=ticks;
    uint64_t end=(expected_ticks+8)/16;
    assert(pin==(uint8_t)(level^inverted));
    assert(timer.alarm==end-expected_us);
    assert(raw_pulse_get_completed_us()==expected_us);
    assert(player_get_progress_percent()<100);
    fire();
    expected_us=end;
    ++emitted;
    assert(raw_pulse_get_completed_us()==expected_us);
    player_service();
}
static void expect_block(const uint8_t *bytes,size_t len,int speed,bool last) {
    const uint16_t *t=timing[speed];
    for (unsigned i=0;i<(bytes[0] ? 1610U : 4032U);++i) { expect(0,t[0]);
    expect(1,t[0]);
    }
    expect(0,t[1]);
    expect(1,t[2]);
    for (size_t i=0;i<len;++i) for (int bit=7;bit>=0;--bit) {
        uint32_t duration=(bytes[i]&(1U<<bit)) ? t[3] : t[4];
        expect(0,duration);
        expect(1,duration);
    }
    expect(0,t[0]);
    if (!last) expect(0,bytes[0] ? 16000000 : 5600000);
}
static void make_fixture(void) {
    const uint8_t data[]={2,0,0,0xA5,4,0,255,0x80,0,255};
    FILE *f=fopen("session.tap","wb");
    assert(f);
    assert(fwrite(data,1,sizeof(data),f)==sizeof(data) && !fclose(f));
}
static void test_play(void) {
    make_fixture();
    assert(file_list_init("."));
    assert(file_list_count()==1);
    assert(file_list_get_type()==FILE_ITEM_TAPE);
    assert(strstr(file_list_get_path(),"session.tap"));
    const uint8_t header[]={0,0xA5},data[]={255,0x80,0,255};
    for (int speed=0;speed<4;++speed) {
        player_init();
        assert(player_set_tap_speed((tap_speed_t)speed));
        assert(!player_set_tap_speed(TAP_SPEED_COUNT));
        inverted=(speed%2)!=0;
        raw_pulse_set_read_invert(inverted);
        player_start("session.tap");
        assert(player_is_tap() && player_is_playing() && player_get_sample_rate()==0);
        assert(player_get_baud_rate()==tap_stream_speed_baud((tap_speed_t)speed));
        assert(player_get_progress_percent()==0 && player_get_position_seconds()==0);
        player_pause_user();
        player_resume_motor();
        assert(player_get_state()==PLAYER_STATE_PAUSED && !timer.running);
        player_resume_user();
        /* A new requested speed must not change the active stream/baud. */
        assert(player_set_tap_speed((tap_speed_t)((speed+1)%4)));
        assert(player_get_baud_rate()==tap_stream_speed_baud((tap_speed_t)speed));
        expected_ticks=expected_us=0;
        emitted=0;
        expect_block(header,2,speed,false);
        expect_block(data,4,speed,true);
        assert(emitted==11387 && player_get_state()==PLAYER_STATE_EOF);
        assert(player_get_progress_percent()==100 && !pin && !timer.running);
        assert(player_get_position_seconds()==expected_us/1000000);
        assert(player_get_total_seconds()==expected_ticks/16000000);
    }
    assert(!remove("session.tap"));
    puts("PASS real browser/player/GPTimer TAP integration: all speeds, inverted output, absolute edge times and physical progress");
    puts("PASS queued EOF MOTOR pause, user pause priority, active speed isolation, final terminator before FINISHED");
}
static void test_stop_underrun(void) {
    make_fixture();
    player_set_tap_speed(TAP_SPEED_1400);
    player_start("session.tap");
    player_pause_user();
    player_start("session.tap");
    assert(player_get_state()==PLAYER_STATE_PLAYING);
    player_stop_for(PLAYER_STOP_S1_EXTERNAL);
    assert(player_get_state()==PLAYER_STATE_STOPPED && !pin && !timer.running);
    assert(!strcmp(player_state_name(player_get_state()),"STOPPED") && !strcmp(player_stop_cause_name(),"S1_EXTERNAL"));
    assert(raw_pulse_level()==0);
    player_start("session.tap");
    assert(player_is_playing() && !strcmp(player_stop_cause_name(),"NONE"));
    /* Do not service the producer: the real driver must report FIFO underrun. */
    unsigned budget=5000;
    while (raw_pulse_is_running()) { assert(budget--);
    fire();
    }
    assert(raw_pulse_is_underrun());
    player_service();
    assert(player_get_state()==PLAYER_STATE_UNDERRUN && !pin && !timer.running);
    assert(!remove("session.tap"));
    FILE *f=fopen("bad.tap","wb");
    assert(f);
    fputc(0,f);
    fclose(f);
    player_start("bad.tap");
    assert(player_get_state()==PLAYER_STATE_ERROR && !timer.running);
    player_stop();
    assert(!remove("bad.tap"));
    puts("PASS STOP/replay while paused, actual FIFO underrun, malformed TAP error and idle output");
}
static void test_driver_clock(void) {
    raw_pulse_set_read_invert(false);
    raw_pulse_init();
    raw_pulse_t pulses[]={{1,0,UINT32_MAX},{0,0,10}};
    assert(raw_pulse_write(pulses,2)==2);
    raw_pulse_finish();
    assert(raw_pulse_start());
    fire();
    assert(raw_pulse_get_completed_us()==UINT32_MAX);
    fire();
    assert(raw_pulse_get_completed_us()==(uint64_t)UINT32_MAX+10);
    raw_pulse_stop();
    raw_pulse_init();
    assert(!raw_pulse_get_completed_us());
    raw_pulse_t pulse={1,1,100};
    assert(raw_pulse_write(&pulse,1)==1);
    raw_pulse_finish();
    assert(raw_pulse_start());
    timer.now=37;
    raw_pulse_pause();
    assert(!pin && !timer.running && !raw_pulse_get_completed_us());
    raw_pulse_resume();
    assert(timer.alarm==63 && pin);
    fire();
    assert(raw_pulse_get_completed_us()==100);
    raw_pulse_stop();
    puts("PASS real driver completed clock: 32-bit carry, session reset and partial-interval pause/resume");
}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));
}
static void test_wav_pulses(void) {
 static uint8_t samples[120000],levels[120000];
 uint8_t state=0;
 const uint8_t values[]={0,63,64,65,127,128,191,192,193,255};
 for(size_t i=0;i<sizeof(samples);++i) {
  samples[i]=i<40000 ? 0:i<80000 ? 255:values[i%10];
  if(samples[i]>=192)state=1;
  else if(samples[i]<=64)state=0;
  levels[i]=state;
 }
 const uint32_t rates[]={44100,22050,48000,11025};
 for(unsigned r=0;r<4;++r) {
  uint32_t rate=rates[r];
  uint8_t header[44]={0};
  memcpy(header,"RIFF",4);
  put32(header+4,sizeof(samples)+36);
  memcpy(header+8,"WAVEfmt ",8);
  put32(header+16,16);
  header[20]=header[22]=1;
  put32(header+24,rate);
  put32(header+28,rate);
  header[32]=1;
  header[34]=8;
  memcpy(header+36,"data",4);
  put32(header+40,sizeof(samples));
  FILE *f=fopen("session.wav","wb");
  assert(f && fwrite(header,1,44,f)==44 && fwrite(samples,1,sizeof(samples),f)==sizeof(samples) && !fclose(f));
  player_init();
  inverted=(r%2)!=0;
  raw_pulse_set_read_invert(inverted);
  player_start("session.wav");
  assert(player_get_state()==PLAYER_STATE_PLAYING && player_get_sample_rate()==rate);
  player_pause_user();
  player_pause_motor();
  player_resume_motor();
  assert(player_get_state()==PLAYER_STATE_PAUSED);
  player_resume_user();
  uint64_t elapsed=0;
  size_t index=0;
  unsigned budget=130000;
  while(player_get_state()==PLAYER_STATE_PLAYING) {
   assert(budget-- && timer.running && index<sizeof(samples));
   uint64_t end=elapsed+timer.alarm;
   assert(pin==(uint8_t)(levels[index]^inverted));
   size_t first=index;
   while(index<sizeof(samples) && (((uint64_t)(index+1)*1000000+rate/2)/rate)<=end) {
    assert(levels[index]==levels[first]);
    ++index;
   }
   assert(index>first && index-first<=256 && end==((uint64_t)index*1000000+rate/2)/rate);
   assert(player_get_progress_percent()<100);
   fire();
   elapsed=end;
   assert(raw_pulse_get_completed_us()==elapsed);
   player_service();
  }
  assert(index==sizeof(samples) && elapsed==((uint64_t)sizeof(samples)*1000000+rate/2)/rate);
  assert(player_get_state()==PLAYER_STATE_EOF && player_get_progress_percent()==100 && !pin && !timer.running);
  assert(player_get_position_seconds()==elapsed/1000000);
  assert(!remove("session.wav"));
 }
 puts("PASS WAV shared pulse transport: four sample rates, every physical interval and hysteresis boundary, 256-sample silence splits, inversion, pauses and exact EOF clock");
}
int main(void) { test_wav_pulses();
test_play();
test_stop_underrun();
test_driver_clock();
return 0;
}

bool mzf_stream_continue_record(const char *path,loader_mode_t mode,mzf_stream_info_t *info) { (void)path;
(void)mode;
(void)info;
return false;
}

bool mzf_stream_next_ticks16(uint8_t *level,uint32_t *ticks) {(void)level;
(void)ticks;
return false;
}

/* TAP tests mock the unused Sharp source, including its cancellation status. */
bool sharp_stream_analysis_cancelled(void){return false;}

const char *mzf_stream_error(void){return "";}

bool mzf_stream_last_pulse_source_byte(void){return false;}
uint32_t mzf_stream_transfer_bytes(void){return 0;}
