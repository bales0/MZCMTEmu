/* Host integration test: production recorder + writers + edge playback parser.
 * Only hardware capture and FreeRTOS time are replaced. No production .c includes.
 * Timing facts: SD2CMT2-Reborn 600a788, formats/mz_tape_profiles.cpp (16 MHz).
 */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "audio/recorder.h"
#include "record/record_autoname.h"
#include "formats/sidecar.h"
#include "audio/edge_capture.h"
#include "audio/edge_stream.h"
#include "freertos/task.h"

static TickType_t ticks = 1;
static edge_capture_event_t pending;
static bool available, running, paused, overrun;
TickType_t xTaskGetTickCount(void) { return ticks; }
void edge_capture_init(uint32_t unit) { assert(unit == 1); available = overrun = false; }
void edge_capture_start(void) { running = true; paused = false; }
void edge_capture_stop(void) { running = false; }
void edge_capture_pause(void) { paused = true; }
void edge_capture_resume(void) { assert(running && paused); paused = false; }
bool edge_capture_is_running(void) { return running; }
bool edge_capture_is_overrun(void) { return overrun; }
void edge_capture_clear_overrun(void) { overrun = false; }
size_t edge_capture_level(void) { return available ? 1 : 0; }
size_t edge_capture_free(void) { return 8191 - edge_capture_level(); }
size_t edge_capture_read(edge_capture_event_t *out, size_t count) {
    if (!available || !count) return 0;
    *out = pending; available = false; return 1;
}
static void feed(uint8_t level, uint32_t us) {
    assert(running && !paused && !available);
    pending = (edge_capture_event_t){level, !level, us}; available = true;
    recorder_service();
    assert(recorder_get_state() == RECORDER_STATE_RECORDING);
}
static void begin(recorder_format_t fmt) {
    recorder_init(); recorder_set_format(fmt); recorder_handle_rec(".", true);
    assert(recorder_get_state() == RECORDER_STATE_RECORDING);
    assert(recorder_get_format()==fmt);
}
static void finish(char *path) {
    snprintf(path, 256, "%s", recorder_get_path()); recorder_handle_stop();
    assert(recorder_get_state() == RECORDER_STATE_FINISHED && !running);
}
static void dispose(const char *path) {
    recorder_handle_stop(); assert(recorder_get_state() == RECORDER_STATE_IDLE);
    assert(remove(path) == 0);
    if(file_format_detect_from_name(path)==FILE_FORMAT_MZF) {
        char metadata[256];snprintf(metadata,sizeof(metadata),"%s",path);memcpy(metadata+strlen(metadata)-3,"MFI",3);
        assert(!remove(metadata));
    }
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void edge_generation(recorder_format_t fmt, const uint32_t *units,
                            const uint32_t *physical_us, size_t count) {
    char path[256]; uint32_t unit = fmt == RECORDER_FORMAT_LEP ? 50 : 16;
    begin(fmt);
    for (size_t i=0; i<count; ++i) feed(i%2, physical_us ? physical_us[i] : units[i]*unit);
    finish(path);
    /* Independent on-disk oracle: signed remainder followed by 127-unit zeros. */
    FILE *f = fopen(path,"rb"); assert(f);
    for (size_t i=0; i<count; ++i) {
        uint32_t n = units[i], zeros = (n-1)/127;
        uint8_t tail = (uint8_t)((n-1)%127+1);
        int expected = i%2 ? tail : (uint8_t)(-(int)tail);
        assert(fgetc(f) == expected);
        while (zeros--) assert(fgetc(f) == 0);
    }
    assert(fgetc(f) == EOF && !ferror(f)); fclose(f);
    /* Check playback consumes the same physical levels, including extensions. */
    assert(edge_stream_open(path,NULL));
    uint8_t level; uint32_t us; bool pulse;
    for (int i=0; i<10; ++i) {
        assert(edge_stream_next_pulse(&level,&us,&pulse));
        assert(level==0 && us==30000 && !pulse);
    }
    for (size_t i=0; i<count; ++i) {
        uint64_t elapsed=0, expected=(uint64_t)units[i]*unit;
        while (elapsed<expected) {
            assert(edge_stream_next_pulse(&level,&us,&pulse));
            assert((level!=0)==(i%2!=0)); elapsed+=us;
        }
        assert(elapsed==expected);
    }
    uint32_t tail=0;
    while (edge_stream_next_pulse(&level,&us,&pulse)) { assert(!level && !pulse); tail+=us; }
    assert(tail==500000 && edge_stream_is_eof() && !edge_stream_has_error());
    edge_stream_close(); dispose(path);
}
static void test_edges(void) {
    /* Distinct high/low widths for every static reference profile. These are
       input facts, not a claim that those playback profiles are ported. */
    static const uint16_t profile_ticks[][4] = {
        {3807,4087,7506,7786},{1818,2228,3753,4164},{1407,1994,2815,3577},
        {3840,4224,7424,7904},{1280,1280,2560,2560},{1232,1877,2522,2874},
        {1407,1994,2815,3577},{1818,2228,3753,4164},{1687,1673,3360,3360},
        {2269,2255,4524,4524},{1232,1877,2522,2874},{3753,4222,7506,7917},
        {4015,4000,8015,8015}
    };
    /* Expected quantized widths manually fixed for the edge-history regression. */
    const uint32_t lep_us[]={1,24,74,74,126*50,127*50,128*50,254*50,255*50,1000000};
    const uint32_t lep_n[] ={1,1,1,1,126,127,128,254,255,20000};
    const uint32_t l16_us[]={1,7,23,23,126*16,127*16,128*16,254*16,255*16,1000000};
    const uint32_t l16_n[] ={1,1,1,1,126,127,128,254,255,62500};
    edge_generation(RECORDER_FORMAT_LEP,lep_n,lep_us,10);
    edge_generation(RECORDER_FORMAT_L16,l16_n,l16_us,10);
    const uint32_t max_us[]={UINT32_MAX, UINT32_MAX};
    const uint32_t max_lep[]={85899346,85899346}, max_l16[]={268435456,268435456};
    edge_generation(RECORDER_FORMAT_LEP,max_lep,max_us,2);
    edge_generation(RECORDER_FORMAT_L16,max_l16,max_us,2);
    for (int format=RECORDER_FORMAT_LEP; format<=RECORDER_FORMAT_L16; ++format) {
        uint32_t us[56], n[56], unit=format==RECORDER_FORMAT_LEP ? 50 : 16;
        size_t k=0;
        for (size_t i=0; i<13; ++i) for (int j=0;j<4;++j) {
            us[k]=(profile_ticks[i][j]+8U)/16U;
            /* Fixed mathematical oracle at the capture's 1 us resolution. */
            n[k]=(us[k]+unit/2)/unit; ++k;
        }
        us[k]=unit/2-1; n[k++]=1; us[k]=unit/2; n[k++]=1;
        us[k]=unit+unit/2-1; n[k++]=1; us[k]=unit+unit/2; n[k++]=2;
        edge_generation((recorder_format_t)format,n,us,k);
        for (int generation=0;generation<8;++generation)
            edge_generation((recorder_format_t)format,n,NULL,k);
    }
    puts("PASS LEP/L16 independent widths, half units, 127 extensions, 1 s idle, 8 generations");
}
static void test_wav(uint32_t rate) {
    char path[256]; uint8_t header[44];
    static uint8_t expected[200000];
    static const uint32_t lengths[]={1,37,113,250};
    uint64_t elapsed=0; size_t written=0;
    begin(RECORDER_FORMAT_WAV);
    /* Select rate before a new session; begin() resets to the default. */
    recorder_handle_abort(); recorder_handle_stop();
    recorder_set_sample_rate(rate); recorder_handle_rec(".",true);
    for (size_t i=0;i<10000;++i) {
        elapsed+=lengths[i%4];
        size_t end=(size_t)((elapsed*rate+500000)/1000000);
        assert(end<sizeof(expected));
        while (written<end) expected[written++]= i%2 ? 235 : 20;
        feed(i%2,lengths[i%4]);
    }
    finish(path); FILE *f=fopen(path,"rb"); assert(f);
    assert(fread(header,1,44,f)==44);
    assert(!memcmp(header,"RIFF",4) && !memcmp(header+8,"WAVEfmt ",8));
    assert(le32(header+24)==rate && header[22]==1 && header[34]==8);
    assert(le32(header+40)==written && le32(header+4)==written+36+(written&1));
    for (size_t i=0;i<written;++i) assert(fgetc(f)==expected[i]);
    if(written&1)assert(fgetc(f)==0);
    assert(fgetc(f)==EOF && !ferror(f)); fclose(f); dispose(path);
    printf("PASS WAV %lu Hz absolute phase: %lu samples, RIFF lengths, physical levels\n",
           (unsigned long)rate,(unsigned long)written);
}
static void test_states(void) {
    char path[256]; begin(RECORDER_FORMAT_L16); feed(0,200);
    recorder_handle_play(true); assert(recorder_get_state()==RECORDER_STATE_PAUSED_USER);
    recorder_service_motor(false); recorder_service_motor(true);
    assert(recorder_get_state()==RECORDER_STATE_PAUSED_USER);
    recorder_handle_play(true); assert(running); feed(1,200); finish(path); dispose(path);
    begin(RECORDER_FORMAT_LEP); snprintf(path,sizeof(path),"%s",recorder_get_path());
    feed(0,200); recorder_handle_abort(); assert(recorder_get_state()==RECORDER_STATE_DELETED);
    assert(fopen(path,"rb")==NULL); recorder_handle_stop();
    begin(RECORDER_FORMAT_LEP); snprintf(path,sizeof(path),"%s",recorder_get_path());
    overrun=true; recorder_service(); assert(recorder_get_state()==RECORDER_STATE_ERROR);
    assert(!running && fopen(path,"rb")==NULL); recorder_handle_stop();
    recorder_init(); recorder_set_format(RECORDER_FORMAT_LEP); recorder_set_auto_signal(true);
    recorder_handle_rec(".",false); assert(recorder_get_state()==RECORDER_STATE_WAIT_MOTOR);
    recorder_service_motor(true); assert(recorder_is_waiting_signal());
    feed(1,100); assert(recorder_is_waiting_signal());
    feed(0,100); assert(!recorder_is_waiting_signal() && recorder_has_valid_signal());
    ticks+=5000; recorder_service(); assert(recorder_get_state()==RECORDER_STATE_FINISHED);
    snprintf(path,sizeof(path),"%s",recorder_get_path()); dispose(path);
    puts("PASS user pause priority, motor wait, cancel deletes, overrun errors, AUTO activity timeout");
}
static void mzf_low(unsigned us) {
 assert(running && !paused && !available);
 pending=(edge_capture_event_t){0,1,us};available=true;recorder_service();
 assert(recorder_get_state()==RECORDER_STATE_RECORDING || recorder_get_state()==RECORDER_STATE_FINISHED);
}
static void mzf_block(const uint8_t *bytes,size_t n,bool header) {
 for(unsigned i=0;i<(header ? 11000U:5500U);++i)mzf_low(238);
 for(unsigned i=0;i<(header ? 40U:20U);++i)mzf_low(469);
 for(unsigned i=0;i<(header ? 40U:20U);++i)mzf_low(238);
 mzf_low(469);mzf_low(469);
 uint16_t sum=0;for(size_t i=0;i<n;++i)for(unsigned b=0;b<8;++b)if(bytes[i]&(1U<<b))++sum;
 for(size_t i=0;i<n+2;++i) {
  uint8_t value=i<n ? bytes[i]:i==n ? (uint8_t)(sum>>8):(uint8_t)sum;
  for(unsigned b=0;b<8;++b)mzf_low(value&(0x80U>>b) ? 469:238);
  mzf_low(469);
 }
}
static void test_mzf(void) {
 uint8_t header[128]={0},payload[]={0,128,1,255};char path[256];
 header[0]=1;memcpy(header+1,"LIVE TEST\r",10);header[18]=4;header[21]=header[23]=0x12;
 recorder_init();recorder_set_format(RECORDER_FORMAT_MZF);recorder_handle_rec(".",false);
 assert(recorder_get_state()==RECORDER_STATE_WAIT_MOTOR);recorder_service_motor(true);
 snprintf(path,sizeof(path),"%s",recorder_get_path());mzf_block(header,128,true);
 recorder_handle_play(true);assert(recorder_get_state()==RECORDER_STATE_PAUSED_USER);
 recorder_service_motor(false);recorder_service_motor(true);assert(recorder_get_state()==RECORDER_STATE_PAUSED_USER);
 recorder_handle_play(true);mzf_block(payload,4,false);
 assert(recorder_get_state()==RECORDER_STATE_FINISHED && !running);
 loader_mode_t mode;assert(sidecar_read(path,1,&mode) && mode==LOADER_MODE_NORMAL_1_1 && !recorder_metadata_warning());
 FILE *f=fopen(path,"rb");assert(f);for(unsigned i=0;i<128;++i)assert(fgetc(f)==header[i]);
 for(unsigned i=0;i<4;++i)assert(fgetc(f)==payload[i]);assert(fgetc(f)==EOF);fclose(f);dispose(path);
 begin(RECORDER_FORMAT_MZF);snprintf(path,sizeof(path),"%s",recorder_get_path());mzf_block(header,128,true);
 recorder_handle_stop();assert(recorder_get_state()==RECORDER_STATE_ERROR && !running && !fopen(path,"rb"));recorder_handle_stop();
 begin(RECORDER_FORMAT_MZF);snprintf(path,sizeof(path),"%s",recorder_get_path());recorder_handle_abort();
 assert(recorder_get_state()==RECORDER_STATE_DELETED && !fopen(path,"rb"));recorder_handle_stop();
 puts("PASS real recorder MZF motor wait, user-pause priority, automatic checksum completion, byte-exact file, incomplete STOP and abort cleanup");
}
static void test_live_autoname(void) {
 uint8_t header[128]={0},payload[]={0,128,1,255};char path[256];
 header[0]=1;memcpy(header+1,"LIVE TEST\r",10);header[18]=4;header[21]=header[23]=0x12;
 for(unsigned format=0;format<4;++format) {
  recorder_init();recorder_set_format((recorder_format_t)format);recorder_set_autoname(true);recorder_set_sample_rate(22050);recorder_handle_rec(".",true);
  mzf_block(header,128,true);mzf_block(payload,4,false);
  if(recorder_get_state()==RECORDER_STATE_RECORDING)recorder_handle_stop();
  assert(recorder_get_state()==RECORDER_STATE_FINISHED && !recorder_metadata_warning());
  assert(record_autoname_has_name() && !strcmp(record_autoname_get_name(),"LIVE TEST"));
  snprintf(path,sizeof(path),"%s",recorder_get_path());assert(strstr(path,"LIVE TEST."));dispose(path);
 }
 puts("PASS real recorder AutoName for WAV 22kHz, LEP, L16 and MZF: validated live header, closed-file rename, MFI under final name");
}
static void test_media_and_sequence(void) {
 char path[256];begin(RECORDER_FORMAT_LEP);snprintf(path,sizeof(path),"%s",recorder_get_path());feed(0,200);
 recorder_media_removed();assert(recorder_get_state()==RECORDER_STATE_ERROR && !running && !fopen(path,"rb"));recorder_handle_stop();
 FILE *f=fopen("REC9999.WAV","wb");assert(f && fputs("preserve",f)>=0 && !fclose(f));
 recorder_set_format(RECORDER_FORMAT_WAV);recorder_handle_rec(".",true);assert(recorder_get_state()==RECORDER_STATE_ERROR);
 f=fopen("REC9999.WAV","rb");assert(f && fgetc(f)=='p');fclose(f);assert(!remove("REC9999.WAV"));recorder_handle_stop();
 puts("PASS media removal aborts recording with error and closed/deleted provisional file; exhausted REC9999 sequence cannot overwrite existing recording");
}
static bool defer_job;static void (*deferred_job)(void *);static void *deferred_context;
bool cmt_storage_execute_async(void (*fn)(void *),void *context) {
 if(defer_job) {assert(!deferred_job);deferred_job=fn;deferred_context=context;}
 else fn(context);return true;
}
static void pump_job(void) {assert(deferred_job);void (*fn)(void *)=deferred_job;deferred_job=NULL;fn(deferred_context);recorder_service();}
static void test_async_finalization(void) {
 char path[256];
 for(unsigned cancel=0;cancel<3;++cancel) {
  begin(RECORDER_FORMAT_L16);feed(0,200);feed(1,200);
  snprintf(path,sizeof(path),"%s",recorder_get_path());defer_job=true;
  recorder_handle_stop();assert(recorder_get_state()==RECORDER_STATE_FINALIZING && paused);
  recorder_service();recorder_handle_play(true);recorder_service_motor(true);recorder_handle_stop();
  assert(recorder_get_state()==RECORDER_STATE_FINALIZING && !strcmp(path,recorder_get_path()));
  if(cancel==1)recorder_handle_abort();
  if(cancel==2) {recorder_media_removed();recorder_handle_abort();}
  pump_job();defer_job=false;assert(!running);
  if(!cancel){assert(recorder_get_state()==RECORDER_STATE_FINISHED);dispose(path);}
  else {assert(recorder_get_state()==(cancel==1 ? RECORDER_STATE_DELETED:RECORDER_STATE_ERROR));assert(!fopen(path,"rb"));recorder_handle_stop();}
 }
 puts("PASS deferred recorder finalization: frozen public state/path, nonblocking service, STOP/PLAY/MOTOR isolation, cancel and removal cannot report SAVED");
}
int main(void) { test_async_finalization(); test_edges(); test_wav(44100); test_wav(22050); test_states(); test_mzf(); test_live_autoname(); test_media_and_sequence(); return 0; }
