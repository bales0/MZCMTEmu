#include "storage/fs.h"
#include "audio/edge_stream.h"
#include "formats/preflight_metadata.h"
#if CMT_EDGE_DIAGNOSTICS
#ifdef ESP_PLATFORM
#include "esp_timer.h"
static uint64_t scan_now(void){return (uint64_t)esp_timer_get_time();}
#else
#include <time.h>
static uint64_t scan_now(void){struct timespec t;timespec_get(&t,TIME_UTC);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
#endif
#define SCAN_TIME(name) uint64_t name=scan_now()
#define SCAN_ADD(field,name) analysis.field+=scan_now()-(name)
#else
#define SCAN_TIME(name) ((void)0)
#define SCAN_ADD(field,name) ((void)0)
#endif

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define EDGE_LEP_SAMPLE_RATE 20000U
#define EDGE_L16_SAMPLE_RATE 62500U
#define EDGE_ZERO_UNITS 127U
#define EDGE_IDLE_PREROLL_US 300000U
#define EDGE_IDLE_TAIL_US 500000U
#define EDGE_IDLE_CHUNK_US 30000U

static cmt_file_t *s_fp = NULL;
static bool s_is_l16 = false;
static bool s_error = false;
static bool s_eof = false;
static uint8_t s_current_level = 0;
static uint32_t s_slot_remaining = 0;
static bool s_slot_is_pulse = false;
static uint32_t s_total_bytes = 0;
static uint32_t s_consumed_bytes = 0;
static uint32_t s_preroll_remaining_us = 0;
static uint32_t s_tail_remaining_us = 0;
static bool s_tail_started = false;
static bool s_monitor_muted;
static edge_scan_control_fn scan_control;
static void *scan_context;
static bool scan_cancelled;
void edge_stream_set_scan_control(edge_scan_control_fn control,void *context){scan_control=control;scan_context=context;}
bool edge_stream_analysis_cancelled(void){return scan_cancelled;}

static edge_analysis_t analysis;
static uint8_t scan_block[1024]; /* Exclusive foreground preflight, never ISR/stack. */
static struct {bool valid;char path[1024];struct stat identity;uint32_t epoch,fingerprint,version;bool l16;edge_analysis_t result;} session_cache;
void edge_stream_analysis_snapshot(edge_analysis_t *out){if(out)*out=analysis;}
void edge_stream_analysis_note_ui(uint64_t us,bool yielding){
#if CMT_EDGE_DIAGNOSTICS
 if(yielding)analysis.yield_us+=us;else analysis.ui_us+=us;
#else
 (void)us;(void)yielding;
#endif
}
void edge_stream_invalidate_cache(void){cmt_storage_invalidate();}
static bool same_identity(const struct stat *a,const struct stat *b){return a->st_size==b->st_size && a->st_mtime==b->st_mtime && a->st_ctime==b->st_ctime && a->st_dev==b->st_dev && a->st_ino==b->st_ino;}
/* Sampled fingerprint strengthens path/size/mtime/epoch; not a full integrity hash.
 * External writers must invalidate the media epoch (remove/remount); all firmware
 * writes invalidate it through the FS owner. */
static uint32_t fingerprint_join(const uint32_t partial[3]){
 uint32_t hash=2166136261U;
 for(unsigned part=0;part<3;++part)for(unsigned byte=0;byte<4;++byte)hash=(hash^((partial[part]>>(byte*8))&255U))*16777619U;
 return hash;
}
static void fingerprint_update(uint32_t partial[3],uint32_t begin,const uint8_t *bytes,size_t length){
 uint32_t n=s_total_bytes<512 ? s_total_bytes:512,offsets[3]={0,(s_total_bytes-n)/2,s_total_bytes-n};
 for(unsigned part=0;part<3;++part){
  uint64_t left=begin>offsets[part] ? begin:offsets[part],right=(uint64_t)begin+length;
  if(right>(uint64_t)offsets[part]+n)right=(uint64_t)offsets[part]+n;
  for(uint64_t pos=left;pos<right;++pos)partial[part]=(partial[part]^bytes[pos-begin])*16777619U;
 }
}
static bool source_fingerprint(uint32_t *out){
 uint32_t n=s_total_bytes<512 ? s_total_bytes:512,partial[3]={2166136261U,2166136261U,2166136261U};
 uint32_t offsets[3]={0,(s_total_bytes-n)/2,s_total_bytes-n};
 for(unsigned part=0;part<3;++part){
  if(cmt_fseek(s_fp,(long)offsets[part],SEEK_SET) || cmt_fread(scan_block,1,n,s_fp)!=n)return false;
  for(uint32_t j=0;j<n;++j)partial[part]=(partial[part]^scan_block[j])*16777619U;
 }
 if(cmt_fseek(s_fp,0,SEEK_SET))return false;
 *out=fingerprint_join(partial);return true;
}
static edge_scan_action_t scan_poll(uint32_t done){
 analysis.scanned_bytes=done;
 SCAN_TIME(started);
 edge_scan_action_t action=scan_control ? scan_control(done,s_total_bytes,scan_context):EDGE_SCAN_CONTINUE;
 SCAN_ADD(callback_us,started);
 return action;
}
static bool scan_finish_action(edge_scan_action_t action){
 if(action==EDGE_SCAN_CANCEL){scan_cancelled=true;analysis.state=EDGE_ANALYSIS_CANCELLED;analysis.cache_status=EDGE_CACHE_INVALID;program_metadata_reset(&analysis.program,s_is_l16 ? FILE_FORMAT_L16:FILE_FORMAT_LEP);session_cache.valid=false;return false;}
 analysis.state=EDGE_ANALYSIS_SKIPPED;analysis.cache_status=EDGE_CACHE_INVALID;analysis.total_us=0;analysis.cache_hit=false;session_cache.valid=false;
 return cmt_fseek(s_fp,0,SEEK_SET)==0;
}
static bool program_is_header(const program_metadata_t *p){return (p->confidence==PROGRAM_HEADER_VALID || p->confidence==PROGRAM_PAYLOAD_VALID) && (p->valid&PROGRAM_TITLE);}
static void scan_collect(uint64_t elapsed,bool *first){
 program_metadata_t current;preflight_tape_metadata_snapshot(&current);
 if(program_is_header(&current)){
  uint32_t number=current.record ? current.record:1;
  if(number>analysis.programs)analysis.programs=number;
  if(!*first){analysis.program=current;analysis.discovered_us=EDGE_IDLE_PREROLL_US+elapsed;
   (void)preflight_record_autoname_get_detected_aux_profile(&analysis.aux_profile);*first=true;}
  else if(analysis.program.confidence!=PROGRAM_PAYLOAD_VALID && current.record==analysis.program.record && !strcmp(current.title,analysis.program.title) && current.load==analysis.program.load && current.declared_size==analysis.program.declared_size)analysis.program=current;
 }else if(!*first)analysis.program=current;
 else if(analysis.program.confidence!=PROGRAM_PAYLOAD_VALID && current.confidence==PROGRAM_INVALID && current.record==analysis.program.record && !strcmp(current.title,analysis.program.title) && current.load==analysis.program.load && current.declared_size==analysis.program.declared_size){analysis.program.confidence=PROGRAM_INVALID;analysis.program.valid=0;}
}
static bool scan_duration(const char *path,uint64_t *duration){
 *duration=0;memset(&analysis,0,sizeof(analysis));analysis.total_bytes=s_total_bytes;analysis.state=EDGE_ANALYSIS_RUNNING;
 program_metadata_reset(&analysis.program,s_is_l16 ? FILE_FORMAT_L16:FILE_FORMAT_LEP);
 edge_scan_action_t action=scan_poll(0);if(action!=EDGE_SCAN_CONTINUE)return scan_finish_action(action);
 struct stat identity,after;uint32_t epoch=cmt_storage_epoch(),fingerprint=0;
 if(cmt_stat(path,&identity) || identity.st_size!=(int64_t)s_total_bytes)goto failed;
 analysis.cache_status=session_cache.valid ? EDGE_CACHE_INVALID:EDGE_CACHE_MISS;
 if(session_cache.valid && session_cache.version==EDGE_ANALYSIS_VERSION && session_cache.epoch==epoch && session_cache.l16==s_is_l16 &&
    !strcmp(session_cache.path,path) && same_identity(&identity,&session_cache.identity) ){
  if(!source_fingerprint(&fingerprint))goto failed;
  if(fingerprint==session_cache.fingerprint){
  if(epoch!=cmt_storage_epoch() || cmt_stat(path,&after) || !same_identity(&identity,&after))goto failed;
  uint64_t initial_callback=analysis.callback_us,initial_ui=analysis.ui_us,initial_yield=analysis.yield_us;
  analysis=session_cache.result;analysis.cache_hit=true;analysis.cache_status=EDGE_CACHE_HIT;analysis.read_us=analysis.decode_us=0;
  analysis.callback_us=initial_callback;analysis.ui_us=initial_ui;analysis.yield_us=initial_yield;
  action=scan_poll(s_total_bytes);if(action!=EDGE_SCAN_CONTINUE)return scan_finish_action(action);
  if(epoch!=cmt_storage_epoch())goto failed;
  *duration=analysis.total_us;return true;
  }
 }
 preflight_tape_metadata_begin(s_is_l16 ? FILE_FORMAT_L16:FILE_FORMAT_LEP);
 uint32_t done=0,partial[3]={2166136261U,2166136261U,2166136261U};uint64_t units=0,run=0;uint8_t level=0;bool have_first=false;
 while(done<s_total_bytes){
  size_t n=s_total_bytes-done;if(n>sizeof(scan_block))n=sizeof(scan_block);
  SCAN_TIME(read_started);
  if(cmt_fread(scan_block,1,n,s_fp)!=n)goto failed;
  SCAN_ADD(read_us,read_started);
  SCAN_TIME(decode_started);
  fingerprint_update(partial,done,scan_block,n);
  for(size_t j=0;j<n;++j){
   int value=(int8_t)scan_block[j];uint32_t ticks=value==0 ? EDGE_ZERO_UNITS:value<0 ? (unsigned)-value:(unsigned)value;
   uint8_t next=value ? value>0:level;
   if(next!=level){
    if(run)preflight_tape_metadata_feed(level,run>UINT32_MAX ? UINT32_MAX:(uint32_t)run,false);
    run=0;level=next;
   }
   units+=ticks;run+=(uint64_t)ticks*(s_is_l16 ? 16:50);
  }
  scan_collect(units*(s_is_l16 ? 16:50),&have_first);
  SCAN_ADD(decode_us,decode_started);
  done+=(uint32_t)n;action=scan_poll(done);
  if(action!=EDGE_SCAN_CONTINUE)return scan_finish_action(action);
 }
 if(run)preflight_tape_metadata_feed(level,run>UINT32_MAX ? UINT32_MAX:(uint32_t)run,false);
 preflight_tape_metadata_end();scan_collect(units*(s_is_l16 ? 16:50),&have_first);
 if(cmt_fseek(s_fp,0,SEEK_SET) || cmt_stat(path,&after) || !same_identity(&identity,&after) || epoch!=cmt_storage_epoch())goto failed;
 fingerprint=fingerprint_join(partial);
 analysis.total_us=units*(s_is_l16 ? 16:50)+EDGE_IDLE_PREROLL_US+EDGE_IDLE_TAIL_US;
 analysis.state=EDGE_ANALYSIS_COMPLETE;
 /* Decoder record counter is a recognized-program lower bound, not a seek index. */
 analysis.programs_exact=false;
 if(strlen(path)<sizeof(session_cache.path)){
  strcpy(session_cache.path,path);session_cache.identity=identity;session_cache.epoch=epoch;session_cache.fingerprint=fingerprint;
  session_cache.l16=s_is_l16;session_cache.version=EDGE_ANALYSIS_VERSION;session_cache.result=analysis;session_cache.valid=true;
 }
 *duration=analysis.total_us;return true;
failed:
 analysis.state=EDGE_ANALYSIS_ERROR;analysis.cache_status=EDGE_CACHE_INVALID;analysis.total_us=0;session_cache.valid=false;
 program_metadata_reset(&analysis.program,s_is_l16 ? FILE_FORMAT_L16:FILE_FORMAT_LEP);return false;
}

static bool detect_l16(const char *path, bool *is_l16)
{
    const char *ext;

    if (path == NULL || is_l16 == NULL) {
        return false;
    }

    ext = strrchr(path, '.');
    if (ext == NULL) {
        return false;
    }

    if (strcasecmp(ext, ".l16") == 0) {
        *is_l16 = true;
        return true;
    }

    if (strcasecmp(ext, ".lep") == 0) {
        *is_l16 = false;
        return true;
    }

    return false;
}

bool edge_stream_is_edge_path(const char *path)
{
    bool is_l16;
    return detect_l16(path, &is_l16);
}

static bool get_file_size(cmt_file_t *fp, uint32_t *out)
{
    long current;
    long end;

    if (fp == NULL || out == NULL) {
        return false;
    }

    current = cmt_ftell(fp);
    if (current < 0) {
        return false;
    }

    if (cmt_fseek(fp, 0, SEEK_END) != 0) {
        return false;
    }

    end = cmt_ftell(fp);
    if (end < 0) {
        return false;
    }

    if (cmt_fseek(fp, current, SEEK_SET) != 0) {
        return false;
    }

    if((uint64_t)end>UINT32_MAX)return false;
    *out = (uint32_t)end;
    return true;
}

static bool edge_stream_open_owned(const char *path, edge_stream_info_t *info)
{
    bool is_l16;
    uint32_t sample_rate;

    edge_stream_close();
    scan_cancelled=false;
    memset(&analysis,0,sizeof(analysis));analysis.state=EDGE_ANALYSIS_ERROR;
    if(info)memset(info,0,sizeof(*info));

    if (!detect_l16(path, &is_l16)) {
        return false;
    }

    s_fp = cmt_fopen(path, "rb");
    if (s_fp == NULL) {
        return false;
    }

    if (!get_file_size(s_fp, &s_total_bytes) || s_total_bytes == 0) {
        edge_stream_close();
        return false;
    }

    s_is_l16 = is_l16;
    s_error = false;
    s_eof = false;
    s_current_level = 0;
    s_slot_remaining = 0;
    s_slot_is_pulse = false;
    s_consumed_bytes = 0;
    s_preroll_remaining_us = EDGE_IDLE_PREROLL_US;
    s_tail_remaining_us = 0;
    s_tail_started = false;
    sample_rate = s_is_l16 ? EDGE_L16_SAMPLE_RATE : EDGE_LEP_SAMPLE_RATE;

    uint64_t duration;
    if(!scan_duration(path,&duration)){edge_stream_close();return false;}
    if (info != NULL) {
        info->analysis=analysis;
        info->total_us=duration;
        info->sample_rate = sample_rate;
        uint64_t samples=duration/(s_is_l16 ? 16U:50U);
        info->total_samples=samples>UINT32_MAX ? UINT32_MAX:(uint32_t)samples;
        info->total_seconds=(uint32_t)(duration/1000000);
        info->pulses_per_second = 0;
    }

    return true;
}

void edge_stream_close(void)
{
    if (s_fp != NULL) {
        cmt_fclose(s_fp);
        s_fp = NULL;
    }

    s_is_l16 = false;
    s_error = false;
    s_eof = false;
    s_current_level = 0;
    s_slot_remaining = 0;
    s_slot_is_pulse = false;
    s_total_bytes = 0;
    s_consumed_bytes = 0;
    s_preroll_remaining_us = 0;
    s_tail_remaining_us = 0;
    s_tail_started = false;
}

static bool edge_stream_should_count_pulse(void)
{
    return s_slot_is_pulse && (s_current_level != 0);
}

static bool load_next_slot(void)
{
    int c;
    int8_t slot;
    uint32_t raw_units;

    if (s_fp == NULL || s_eof || s_error) {
        return false;
    }

    c = cmt_fgetc(s_fp);
    if (c == EOF) {
        if (cmt_ferror(s_fp)) {
            s_error = true;
        } else {
            s_eof = true;
        }
        return false;
    }
    s_consumed_bytes++;

    slot = (int8_t)(uint8_t)c;
    if (slot == 0) {
        raw_units = EDGE_ZERO_UNITS;
        s_slot_is_pulse = false;
    } else if (slot < 0) {
        s_current_level = 0;
        raw_units = (slot == INT8_MIN) ? 128U : (uint8_t)(-slot);
        s_slot_is_pulse = true;
    } else {
        s_current_level = 0xFFU;
        raw_units = (uint8_t)slot;
        s_slot_is_pulse = true;
    }

    if (raw_units == 0 ||
        (raw_units > EDGE_ZERO_UNITS && slot != INT8_MIN)) {
        s_error = true;
        return false;
    }

    s_slot_remaining = raw_units;
    return true;
}

size_t edge_stream_read(uint8_t *dst, size_t max_len)
{
    size_t written = 0;

    if (s_fp == NULL || dst == NULL || max_len == 0 || s_error) {
        return 0;
    }

    while (written < max_len) {
        if (s_slot_remaining == 0 && !load_next_slot()) {
            break;
        }

        while (s_slot_remaining > 0 && written < max_len) {
            dst[written++] = s_current_level;
            s_slot_remaining--;
        }
    }

    return written;
}

bool edge_stream_is_eof(void)
{
    return (s_fp != NULL && !s_error && s_eof &&
            s_slot_remaining == 0 &&
            s_preroll_remaining_us == 0 &&
            s_tail_remaining_us == 0);
}

bool edge_stream_has_error(void)
{
    return s_error;
}

uint32_t edge_stream_get_total_bytes(void)
{
    return s_total_bytes;
}

uint32_t edge_stream_get_consumed_bytes(void)
{
    return s_consumed_bytes;
}

bool edge_stream_next_pulse(uint8_t *level, uint32_t *duration_us, bool *count_pulse)
{
    uint32_t unit_us;

    if (level == NULL || duration_us == NULL || count_pulse == NULL ||
        s_fp == NULL || s_error) {
        return false;
    }

    s_monitor_muted=false;
    if (s_preroll_remaining_us > 0) {
        s_monitor_muted=true;
        uint32_t chunk = (s_preroll_remaining_us > EDGE_IDLE_CHUNK_US) ?
            EDGE_IDLE_CHUNK_US : s_preroll_remaining_us;

        s_preroll_remaining_us -= chunk;
        *level = 0;
        *duration_us = chunk;
        *count_pulse = false;
        return true;
    }

    if (!load_next_slot()) {
        if (!s_error && s_eof && !s_tail_started) {
            s_tail_started = true;
            s_tail_remaining_us = EDGE_IDLE_TAIL_US;
        }

        if (s_tail_remaining_us > 0) {
            s_monitor_muted=true;
            uint32_t chunk = (s_tail_remaining_us > EDGE_IDLE_CHUNK_US) ?
                EDGE_IDLE_CHUNK_US : s_tail_remaining_us;

            s_tail_remaining_us -= chunk;
            *level = 0;
            *duration_us = chunk;
            *count_pulse = false;
            return true;
        }

        return false;
    }

    unit_us = s_is_l16 ? 16U : 50U;
    *level = s_current_level;
    *duration_us = (uint32_t)s_slot_remaining * unit_us;
    *count_pulse = edge_stream_should_count_pulse();
    s_slot_remaining = 0;
    s_slot_is_pulse = false;
    return true;
}

bool edge_stream_monitor_muted(void) {return s_monitor_muted;}

/* The preflight workspace is bounded and exclusive; recursive/concurrent opens
 * fail before touching the live source or either decoder context. */
static bool scan_owner;
bool edge_stream_open(const char *path,edge_stream_info_t *info){
 if(__atomic_exchange_n(&scan_owner,true,__ATOMIC_ACQUIRE))return false;
 bool result=edge_stream_open_owned(path,info);
 __atomic_store_n(&scan_owner,false,__ATOMIC_RELEASE);return result;
}
