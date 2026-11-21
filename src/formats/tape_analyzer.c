/* SPDX-License-Identifier: GPL-3.0-only */
#include "formats/tape_analyzer.h"
#include "formats/preflight_metadata.h"
#include "audio/wav_pulse_stream.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <limits.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define POOL_ALLOC(n) heap_caps_malloc((n),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)
#else
#define POOL_ALLOC(n) malloc(n)
#endif

static tape_index_t index_data;
static tape_program_t *programs;
static tape_block_t *blocks;
static tape_checkpoint_t *checkpoints;
static tape_cursor_t scan;
static bool cache_valid,owner;
static uint64_t checkpoint_stride=1000000,next_checkpoint;
static tape_checkpoint_t run_begin,run_end,leader,mark;
static tape_checkpoint_t previous_begin,previous_end;
static uint64_t run_ticks;
static uint8_t run_level;
static bool have_run,leader_known,mark_known,header_leader;
static uint32_t observed,active_program,last_block;
static uint64_t covered_offset,covered_ticks;
static bool unknown_content;
static bool dependency_unknown;
static bool checksum_tail,long_continuation;
static uint8_t long_level;
static bool previous_high;
static program_metadata_t preview;
static uint32_t last_revision;
static tape_index_t time_index;
static tape_checkpoint_t *time_points,time_start;
static uint8_t time_samples[3][64],time_block[1024],time_level;
static uint64_t time_stride,time_next;
static bool time_owner;
static int seek_percent=-1;
typedef struct {uint64_t begin,end,begin_offset,end_offset;uint8_t level;bool have;} gap_tracker_t;
static gap_tracker_t deep_gap,time_gap;
static tape_gap_t *deep_gaps,*time_gaps;
static tape_cursor_t gap_cursor;
static int gap_percent=-1;
static const tape_index_t *byte_scan_index;
static tape_checkpoint_t byte_scan_resume;
static gap_tracker_t byte_scan_tracker;
static uint8_t gap_target_buffer[1024];
static uint64_t gap_offset_reads;
static bool gap_offset_cancelled;

static void gap_init(tape_index_t *idx,tape_gap_t **pool,gap_tracker_t *tracker) {
    byte_scan_index=NULL;
    if(!*pool)*pool=POOL_ALLOC(TAPE_GAP_CAPACITY*sizeof(**pool));
    idx->gaps=*pool;idx->gap_capacity=*pool ? TAPE_GAP_CAPACITY:0;
    idx->gap_count=0;idx->gaps_ready=idx->gaps_truncated=false;
    memset(tracker,0,sizeof(*tracker));
}
static void gap_store(tape_index_t *idx,const gap_tracker_t *t) {
    if(!t->have || t->end-t->begin<TAPE_GAP_MIN_US*16)return;
    if(idx->gap_count==idx->gap_capacity){idx->gaps_truncated=true;return;}
    tape_gap_t *pool=idx==&index_data ? deep_gaps:time_gaps;
    pool[idx->gap_count++]=(tape_gap_t){t->begin/16,t->end/16,t->begin_offset,t->end_offset,UINT64_MAX};
}
static void gap_feed(tape_index_t *idx,gap_tracker_t *t,uint8_t level,uint64_t begin,uint64_t end,uint64_t offset,uint64_t next_offset) {
    if(t->have && t->level!=level){gap_store(idx,t);t->have=false;}
    if(!t->have){t->begin=begin;t->begin_offset=offset;t->level=level;t->have=true;}
    t->end=end;t->end_offset=next_offset;
}

bool tape_analyzer_supported(const char *path) {
    const char *ext=path ? strrchr(path,'.'):NULL;
    return ext && (!strcasecmp(ext,".lep") || !strcasecmp(ext,".l16") || !strcasecmp(ext,".wav"));
}
size_t tape_analyzer_pool_bytes(void) {
    return TAPE_INDEX_PROGRAM_CAPACITY*sizeof(tape_program_t)+
        TAPE_INDEX_BLOCK_CAPACITY*sizeof(tape_block_t)+TAPE_INDEX_CHECKPOINT_CAPACITY*sizeof(tape_checkpoint_t)+
        TAPE_GAP_CAPACITY*sizeof(tape_gap_t);
}
static bool allocate_pools(void) {
    if(!programs)programs=POOL_ALLOC(TAPE_INDEX_PROGRAM_CAPACITY*sizeof(*programs));
    if(!blocks)blocks=POOL_ALLOC(TAPE_INDEX_BLOCK_CAPACITY*sizeof(*blocks));
    if(!checkpoints)checkpoints=POOL_ALLOC(TAPE_INDEX_CHECKPOINT_CAPACITY*sizeof(*checkpoints));
    return programs && blocks && checkpoints;
}
static bool stat_matches(const tape_source_identity_t *id,const struct stat *s) {
    return s->st_size>=0 && id->size_bytes==(uint64_t)s->st_size &&
        id->mtime_seconds==(int64_t)s->st_mtime && id->ctime_seconds==(int64_t)s->st_ctime &&
        id->device==(uint64_t)s->st_dev && id->inode==(uint64_t)s->st_ino;
}
/* Three bounded probes are explicitly only an identity hint. Full waveform
 * scanning is never repeated on a cache hit. The source is always opened rb. */
static bool fingerprint(cmt_file_t *f,uint64_t size,uint32_t *value) {
    uint8_t bytes[64];uint32_t hash=2166136261U;
    uint64_t offsets[3]={0,size>64 ? (size-64)/2:0,size>64 ? size-64:0};
    for(unsigned p=0;p<3;++p) {
        size_t n=size<sizeof(bytes) ? (size_t)size:sizeof(bytes);
        if(offsets[p]>LONG_MAX || cmt_fseek(f,(long)offsets[p],SEEK_SET) || cmt_fread(bytes,1,n,f)!=n)return false;
        for(size_t j=0;j<n;++j)hash=(hash^bytes[j])*16777619U;
    }
    *value=hash;return true;
}
bool tape_index_source_valid(const tape_index_t *idx) {
    struct stat s;
    if(!idx || !idx->source.stat_valid || idx->source.storage_epoch!=cmt_storage_epoch() ||
       cmt_stat(idx->source.path,&s) || !stat_matches(&idx->source,&s))return false;
    cmt_file_t *f=cmt_fopen(idx->source.path,"rb");uint32_t hash=0;
    bool ok=f && fingerprint(f,idx->source.size_bytes,&hash);
    if(f)cmt_fclose(f);
    return ok && idx->source.fingerprint_valid && hash==idx->source.fingerprint;
}
static bool time_identity(const char *path,file_format_t format,uint64_t size) {
    struct stat st;
    if(!path || strlen(path)>=TAPE_INDEX_PATH_CAPACITY || cmt_stat(path,&st) ||
       !S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size!=size || size>LONG_MAX)return false;
    byte_scan_index=NULL;memset(&time_index,0,sizeof(time_index));
    strcpy(time_index.source.path,path);time_index.source.format=format;time_index.source.size_bytes=size;
    time_index.source.mtime_seconds=st.st_mtime;time_index.source.ctime_seconds=st.st_ctime;
    time_index.source.device=st.st_dev;time_index.source.inode=st.st_ino;
    time_index.source.storage_epoch=cmt_storage_epoch();time_index.source.stat_valid=true;
    time_index.schema_version=TAPE_INDEX_SCHEMA_VERSION;time_index.algorithm_version=3;time_index.time_only=true;
    return true;
}
static void time_add(const tape_checkpoint_t *cp) {
    if(time_index.checkpoint_capacity==1) {
        if(!time_index.checkpoint_count){time_start=*cp;time_index.checkpoint_count=1;}
        return;
    }
    if(time_index.checkpoint_count==time_index.checkpoint_capacity) {
        unsigned out=0;
        for(unsigned i=0;i<time_index.checkpoint_count;i+=2)time_points[out++]=time_points[i];
        time_index.checkpoint_count=out;time_stride*=2;
    }
    time_points[time_index.checkpoint_count++]=*cp;
}
bool tape_time_scan_begin(const char *path,file_format_t format,uint64_t bytes) {
    if((format!=FILE_FORMAT_LEP && format!=FILE_FORMAT_L16) ||
       __atomic_exchange_n(&time_owner,true,__ATOMIC_ACQUIRE))return false;
    if(!time_identity(path,format,bytes)){__atomic_store_n(&time_owner,false,__ATOMIC_RELEASE);return false;}
    if(!time_points)time_points=POOL_ALLOC(TAPE_INDEX_CHECKPOINT_CAPACITY*sizeof(*time_points));
    time_index.checkpoint_capacity=time_points ? TAPE_INDEX_CHECKPOINT_CAPACITY:1;
    time_index.checkpoints=time_points ? time_points:&time_start;
    time_index.synthetic_preroll_us=300000;time_index.synthetic_tail_us=500000;
    time_index.state=TAPE_INDEX_SCANNING;time_stride=time_next=1000000;time_level=0;
    gap_init(&time_index,&time_gaps,&time_gap);
    memset(time_samples,0,sizeof(time_samples));
    tape_checkpoint_t cp={.kind=TAPE_CHECKPOINT_EDGE,.valid=true};time_add(&cp);return true;
}
bool tape_time_scan_feed(const uint8_t *bytes,size_t count) {
    if(time_index.state!=TAPE_INDEX_SCANNING || !bytes || count>time_index.source.size_bytes-time_index.scanned_bytes)return false;
    uint64_t begin=time_index.scanned_bytes,size=time_index.source.size_bytes,n=size<64 ? size:64;
    uint64_t positions[3]={0,(size-n)/2,size-n};
    for(unsigned p=0;p<3;++p) {
        uint64_t left=begin>positions[p] ? begin:positions[p],right=begin+count;
        if(right>positions[p]+n)right=positions[p]+n;
        if(right>left)memcpy(time_samples[p]+(size_t)(left-positions[p]),bytes+(size_t)(left-begin),(size_t)(right-left));
    }
    unsigned unit=time_index.source.format==FILE_FORMAT_L16 ? 256:800;
    for(size_t i=0;i<count;++i) {
        if(time_index.waveform_ticks16/16>=time_next) {
            tape_checkpoint_t cp={.source_offset=begin+i,.time_us=time_index.waveform_ticks16/16,
                .time_ticks16=time_index.waveform_ticks16,.kind=TAPE_CHECKPOINT_EDGE,.level=time_level,.valid=true};
            time_add(&cp);time_next=cp.time_us+time_stride;
        }
        int value=(int8_t)bytes[i];
        unsigned units=value==0 ? 127:value<0 ? -value:value;
        if(value)time_level=value>0;
        uint32_t ticks=units*unit;
        if(time_index.waveform_ticks16>UINT64_MAX-ticks)return false;
        gap_feed(&time_index,&time_gap,time_level,time_index.waveform_ticks16,time_index.waveform_ticks16+ticks,begin+i,begin+i+1);
        time_index.waveform_ticks16+=ticks;
    }
    time_index.scanned_bytes+=count;time_index.waveform_us=time_index.waveform_ticks16/16;
    ++time_index.cursor_read_calls;time_index.cursor_read_bytes+=count;
    return true;
}
bool tape_time_scan_end(void) {
    struct stat st;
    bool ok=time_index.state==TAPE_INDEX_SCANNING && time_index.scanned_bytes==time_index.source.size_bytes &&
        time_index.source.storage_epoch==cmt_storage_epoch() && !cmt_stat(time_index.source.path,&st) && stat_matches(&time_index.source,&st);
    if(ok) {
        /* Only closed gaps lead to a following signal. Trailing silence
         * must not create a skip target just before EOF. */
        time_index.gaps_ready=!time_index.gaps_truncated;
        uint32_t hash=2166136261U;size_t n=time_index.source.size_bytes<64 ? (size_t)time_index.source.size_bytes:64;
        for(unsigned p=0;p<3;++p)for(size_t i=0;i<n;++i)hash=(hash^time_samples[p][i])*16777619U;
        time_index.source.fingerprint=hash;time_index.source.fingerprint_valid=true;time_index.source.fingerprint_version=1;
        tape_checkpoint_t cp={.source_offset=time_index.scanned_bytes,.time_us=time_index.waveform_us,
            .time_ticks16=time_index.waveform_ticks16,.kind=TAPE_CHECKPOINT_EDGE,.level=time_level,.valid=true};
        time_add(&cp);time_index.duration_known=time_index.scan_complete=true;time_index.state=TAPE_INDEX_FINISHED;
    } else time_index.state=TAPE_INDEX_ERROR;
    __atomic_store_n(&time_owner,false,__ATOMIC_RELEASE);return ok;
}
void tape_time_scan_cancel(void) {
    if(time_index.state==TAPE_INDEX_SCANNING){time_index.state=TAPE_INDEX_CANCELLED;time_index.scan_complete=false;}
    __atomic_store_n(&time_owner,false,__ATOMIC_RELEASE);
}
const tape_index_t *tape_time_result(void){return &time_index;}
void tape_time_navigation_duration(const tape_index_t *idx,uint64_t ticks) {
    if(idx==&time_index && !time_index.duration_known) {
        time_index.waveform_ticks16=ticks;time_index.waveform_us=(ticks+8)/16;
        time_index.duration_known=true;
    }
}
const tape_index_t *tape_time_navigation(const char *path,tape_analyzer_control_fn control,void *context) {
    if(!tape_analyzer_supported(path) || __atomic_load_n(&time_owner,__ATOMIC_ACQUIRE))return NULL;
    if(time_index.scan_complete && !strcmp(time_index.source.path,path) && tape_index_source_valid(&time_index))return &time_index;
    file_format_t format=file_format_detect_from_name(path);
    if(format==FILE_FORMAT_WAV)return tape_time_prepare(path,control,context);
    if(control && !control(context))return NULL;
    struct stat st;
    if(cmt_stat(path,&st) || st.st_size<0 || !time_identity(path,format,(uint64_t)st.st_size))return NULL;
    cmt_file_t *file=cmt_fopen(path,"rb");
    bool ok=file && fingerprint(file,time_index.source.size_bytes,&time_index.source.fingerprint);
    if(file)cmt_fclose(file);
    if(!ok){time_index.state=TAPE_INDEX_ERROR;return NULL;}
    time_index.source.fingerprint_valid=true;time_index.source.fingerprint_version=1;
    time_index.synthetic_preroll_us=300000;time_index.synthetic_tail_us=500000;
    time_start=(tape_checkpoint_t){.kind=TAPE_CHECKPOINT_EDGE,.valid=true};
    time_index.checkpoints=&time_start;time_index.checkpoint_count=time_index.checkpoint_capacity=1;
    time_index.scan_complete=true;time_index.state=TAPE_INDEX_FINISHED;
    return tape_index_source_valid(&time_index) ? &time_index:NULL;
}
const tape_index_t *tape_time_prepare(const char *path,tape_analyzer_control_fn control,void *context) {
    if(!tape_analyzer_supported(path) || __atomic_load_n(&time_owner,__ATOMIC_ACQUIRE))return NULL;
    if(time_index.scan_complete && time_index.duration_known && !strcmp(time_index.source.path,path) && tape_index_source_valid(&time_index))return &time_index;
    struct stat st;if(cmt_stat(path,&st) || st.st_size<0)return NULL;
    file_format_t format=file_format_detect_from_name(path);
    if(format==FILE_FORMAT_WAV) {
        if(!time_identity(path,format,(uint64_t)st.st_size))return NULL;
        cmt_file_t *file=cmt_fopen(path,"rb");wav_info_t info;
        bool ok=file && wav_reader_parse(file,&info) && wav_reader_supported(&info) &&
            fingerprint(file,time_index.source.size_bytes,&time_index.source.fingerprint);
        if(file)cmt_fclose(file);
        if(!ok){time_index.state=TAPE_INDEX_ERROR;return NULL;}
        time_index.source.fingerprint_valid=true;time_index.source.fingerprint_version=1;
        time_start=(tape_checkpoint_t){.source_offset=info.data_offset,.kind=TAPE_CHECKPOINT_WAV,.valid=true};
        time_index.checkpoints=&time_start;time_index.checkpoint_count=time_index.checkpoint_capacity=1;
        time_index.waveform_ticks16=wav_pulse_time(info.data_size,info.sample_rate,16000000);
        time_index.waveform_us=(time_index.waveform_ticks16+8)/16;
        time_index.duration_known=time_index.scan_complete=true;time_index.state=TAPE_INDEX_FINISHED;
        if(!tape_index_source_valid(&time_index)){time_index.scan_complete=false;time_index.state=TAPE_INDEX_ERROR;return NULL;}
        return &time_index;
    }
    if(!tape_time_scan_begin(path,format,(uint64_t)st.st_size))return NULL;
    cmt_file_t *file=cmt_fopen(path,"rb");bool ok=file!=NULL;
    while(ok && time_index.scanned_bytes<time_index.source.size_bytes) {
        if(control && !control(context)){ok=false;break;}
        size_t n=time_index.source.size_bytes-time_index.scanned_bytes;if(n>sizeof(time_block))n=sizeof(time_block);
        ok=cmt_fread(time_block,1,n,file)==n && tape_time_scan_feed(time_block,n);
    }
    if(file)cmt_fclose(file);
    if(!ok){tape_time_scan_cancel();return NULL;}
    return tape_time_scan_end() ? &time_index:NULL;
}
int tape_time_seek_percent(void){return seek_percent;}
int tape_gap_scan_percent(void){return gap_percent;}
uint64_t tape_gap_offset_read_bytes(void){return gap_offset_reads;}
static bool gap_offset_target(const tape_index_t *idx,tape_gap_t *gap,
                               tape_analyzer_control_fn control,void *context) {
    if(gap->target_offset!=UINT64_MAX)return true;
    cmt_file_t *file=cmt_fopen(idx->source.path,"rb");if(!file)return false;
    uint64_t at=gap->end_offset,ticks=0;
    unsigned unit=idx->source.format==FILE_FORMAT_L16 ? 256:800;
    bool ok=true;
    while(at>gap->begin_offset && ticks<TAPE_GAP_LEAD_US*16) {
        if(control && !control(context)){gap_offset_cancelled=true;ok=false;break;}
        size_t n=at-gap->begin_offset;if(n>sizeof(gap_target_buffer))n=sizeof(gap_target_buffer);
        uint64_t first=at-n;
        if(cmt_fseek(file,(long)first,SEEK_SET) || cmt_fread(gap_target_buffer,1,n,file)!=n){ok=false;break;}
        gap_offset_reads+=n;
        for(size_t i=n;i && ticks<TAPE_GAP_LEAD_US*16;--i) {
            int value=(int8_t)gap_target_buffer[i-1];
            ticks+=(value==0 ? 127:value<0 ? -value:value)*unit;--at;
        }
    }
    cmt_fclose(file);if(ok)gap->target_offset=at;return ok;
}
static bool gap_offset_consider(const tape_index_t *idx,tape_gap_t *gap,uint64_t position,bool forward,
                                uint64_t exclude,bool *found,uint64_t *target,
                                tape_analyzer_control_fn control,void *context) {
    if((forward && gap->end_offset<=position) || (!forward && gap->begin_offset>=position))return true;
    if(!gap_offset_target(idx,gap,control,context))return false;
    uint64_t at=gap->target_offset;
    if(at==exclude || (forward ? at<=position:at>=position))return true;
    if(!*found || (forward ? at<*target:at>*target)){*found=true;*target=at;}
    return true;
}
static bool gap_offset_cached(const tape_index_t *idx,uint64_t position,bool forward,uint64_t exclude,
                              bool *found,uint64_t *target,tape_analyzer_control_fn control,void *context) {
    tape_gap_t *pool=idx==&time_index ? time_gaps:idx==&index_data ? deep_gaps:NULL;
    for(unsigned step=0;step<idx->gap_count;++step) {
        unsigned i=forward ? step:idx->gap_count-1-step;
        tape_gap_t gap=idx->gaps[i];
        if(!gap_offset_consider(idx,&gap,position,forward,exclude,found,target,control,context))return false;
        if(pool)pool[i].target_offset=gap.target_offset;
        if(*found)break;
    }
    return true;
}
tape_gap_result_t tape_gap_find_offset(const tape_index_t *idx,uint64_t position,bool forward,
                                      uint64_t exclude,uint64_t *target,
                                      tape_analyzer_control_fn control,void *context) {
    gap_offset_reads=0;gap_offset_cancelled=false;
    if(!idx || !target || !idx->scan_complete ||
       (idx->source.format!=FILE_FORMAT_LEP && idx->source.format!=FILE_FORMAT_L16) || !tape_index_source_valid(idx))return TAPE_GAP_ERROR;
    if(control && !control(context))return TAPE_GAP_CANCELLED;
    bool found=false;uint64_t at=0;
    bool covered=idx->gaps_ready || (byte_scan_index==idx && byte_scan_tracker.begin_offset>=position);
    if((forward || covered) && !idx->gaps_truncated) {
        if(!gap_offset_cached(idx,position,forward,exclude,&found,&at,control,context))goto offset_error;
        if(found || idx->gaps_ready || (!forward && covered)){if(found)*target=at;return found ? TAPE_GAP_FOUND:TAPE_GAP_NONE;}
    }
    tape_index_t *owned=idx==&time_index ? &time_index:idx==&index_data ? &index_data:NULL;
    if(!owned)return TAPE_GAP_ERROR;
    if(byte_scan_index!=idx || (!forward && idx->gaps_truncated)) {
        gap_init(owned,owned==&time_index ? &time_gaps:&deep_gaps,&byte_scan_tracker);
        memset(&byte_scan_resume,0,sizeof(byte_scan_resume));byte_scan_index=idx;
    }
    if(!tape_cursor_open(&gap_cursor,idx,byte_scan_resume.valid ? &byte_scan_resume:NULL))goto offset_error;
    gap_tracker_t tracker=byte_scan_tracker;uint64_t last=gap_cursor.offset;
    gap_percent=idx->source.size_bytes ? (int)(gap_cursor.offset*100/idx->source.size_bytes):100;
    if(control && !control(context)){gap_offset_cancelled=true;goto offset_scan_error;}
    for(;;) {
        if(gap_cursor.offset-last>=1024) {
            last=gap_cursor.offset;gap_percent=(int)(gap_cursor.offset*100/idx->source.size_bytes);
            if(control && !control(context)){gap_offset_cancelled=true;goto offset_scan_error;}
        }
        uint8_t level;uint32_t ticks;uint64_t begin=gap_cursor.ticks16,offset=gap_cursor.offset;
        if(!tape_cursor_next(&gap_cursor,&level,&ticks))break;
        if(tracker.have && tracker.level!=level) {
            unsigned before=owned->gap_count;gap_store(owned,&tracker);
            if(tracker.end-tracker.begin>=TAPE_GAP_MIN_US*16) {
                tape_gap_t gap={tracker.begin/16,tracker.end/16,tracker.begin_offset,tracker.end_offset,UINT64_MAX};
                if(!gap_offset_consider(idx,&gap,position,forward,exclude,&found,&at,control,context))goto offset_scan_error;
                if(owned->gap_count>before) {
                    tape_gap_t *pool=owned==&time_index ? time_gaps:deep_gaps;pool[before].target_offset=gap.target_offset;
                }
            }
            tracker.have=false;
        }
        if(!tracker.have){tracker.begin=begin;tracker.begin_offset=offset;tracker.level=level;tracker.have=true;}
        tracker.end=gap_cursor.ticks16;tracker.end_offset=gap_cursor.offset;
        if((forward && found) || (!forward && tracker.begin_offset>position))break;
    }
    if(gap_cursor.error)goto offset_scan_error;
    bool complete=gap_cursor.offset==gap_cursor.end && !gap_cursor.remaining_ticks16;
    if(complete){owned->gaps_ready=!owned->gaps_truncated;tape_time_navigation_duration(idx,gap_cursor.ticks16);gap_percent=100;}
    if(control && !control(context)){gap_offset_cancelled=true;goto offset_scan_error;}
    tape_cursor_checkpoint(&gap_cursor,&byte_scan_resume);byte_scan_tracker=tracker;
    gap_offset_reads+=gap_cursor.read_bytes;tape_cursor_close(&gap_cursor);gap_percent=-1;
    if(!tape_index_source_valid(idx))return TAPE_GAP_ERROR;
    /* A resumed scan can end before another qualifying gap. Previously cached
     * entries still supply the closest backward candidate. */
    if(!found && !forward && !owned->gaps_truncated &&
       !gap_offset_cached(idx,position,forward,exclude,&found,&at,control,context))goto offset_error;
    if(found)*target=at;
    return found ? TAPE_GAP_FOUND:TAPE_GAP_NONE;
offset_scan_error:
    gap_offset_reads+=gap_cursor.read_bytes;tape_cursor_close(&gap_cursor);
offset_error:
    byte_scan_index=NULL;gap_percent=-1;
    return gap_offset_cancelled ? TAPE_GAP_CANCELLED:TAPE_GAP_ERROR;
}
static void gap_candidate(uint64_t begin,uint64_t end,uint64_t position,bool forward,uint64_t exclude,bool *found,uint64_t *target) {
    if(end<begin || end-begin<TAPE_GAP_MIN_US)return;
    uint64_t at=end-TAPE_GAP_LEAD_US;
    if(at==exclude)return;
    if((forward && at<=position) || (!forward && at>=position))return;
    if(!*found || (forward ? at<*target:at>*target)){*found=true;*target=at;}
}
tape_gap_result_t tape_gap_find(const tape_index_t *idx,uint64_t position,bool forward,
                               uint64_t *target,tape_analyzer_control_fn control,void *context) {
    return tape_gap_find_excluding(idx,position,forward,UINT64_MAX,target,control,context);
}
tape_gap_result_t tape_gap_find_excluding(const tape_index_t *idx,uint64_t position,bool forward,
                                         uint64_t exclude,uint64_t *target,
                                         tape_analyzer_control_fn control,void *context) {
    if(!idx || !target || !idx->scan_complete || !tape_index_source_valid(idx))return TAPE_GAP_ERROR;
    if(control && !control(context))return TAPE_GAP_CANCELLED;
    bool found=false;uint64_t at=0;
    if(idx->gaps_ready) {
        for(unsigned i=0;i<idx->gap_count;++i)
            gap_candidate(idx->gaps[i].begin_us,idx->gaps[i].end_us,position,forward,exclude,&found,&at);
    } else {
        /* WAV ordinary PLAY reaches here only on an explicit gap request.
         * The cursor uses PLAY hysteresis, with no content decoder state. */
        tape_index_t *owned=idx==&time_index ? &time_index:idx==&index_data ? &index_data:NULL;
        gap_tracker_t tracker={0};
        if(owned)gap_init(owned,owned==&time_index ? &time_gaps:&deep_gaps,&tracker);
        if(!tape_cursor_open(&gap_cursor,idx,NULL))return TAPE_GAP_ERROR;
        uint64_t first=gap_cursor.offset,last=first;
        gap_percent=0;
        for(;;) {
            if(gap_cursor.offset-last>=1024 || gap_cursor.offset==first) {
                gap_percent=gap_cursor.end>first ? (int)((gap_cursor.offset-first)*100/(gap_cursor.end-first)):100;
                if(control && !control(context)) {
                    tape_cursor_close(&gap_cursor);gap_percent=-1;return TAPE_GAP_CANCELLED;
                }
                last=gap_cursor.offset;
            }
            uint8_t level;uint32_t ticks;uint64_t begin=gap_cursor.ticks16,offset=gap_cursor.offset;
            if(!tape_cursor_next(&gap_cursor,&level,&ticks))break;
            if(tracker.have && tracker.level!=level) {
                gap_candidate(tracker.begin/16,tracker.end/16,position,forward,exclude,&found,&at);
                if(owned)gap_store(owned,&tracker);
                tracker.have=false;
            }
            if(!tracker.have){tracker.begin=begin;tracker.begin_offset=offset;tracker.level=level;tracker.have=true;}
            tracker.end=gap_cursor.ticks16;tracker.end_offset=gap_cursor.offset;
            if(!idx->duration_known) {
                uint64_t lookahead=position>UINT64_MAX-TAPE_GAP_LEAD_US ? UINT64_MAX:position+TAPE_GAP_LEAD_US;
                if((forward && found) || (!forward && gap_cursor.ticks16/16>lookahead))break;
            }
        }
        bool ok=!gap_cursor.error;
        bool complete=gap_cursor.offset==gap_cursor.end && !gap_cursor.remaining_ticks16;
        uint64_t total_ticks=gap_cursor.ticks16;
        tape_cursor_close(&gap_cursor);
        if(complete)gap_percent=100;
        if(control && !control(context)){gap_percent=-1;return TAPE_GAP_CANCELLED;}
        gap_percent=-1;
        if(!ok || !tape_index_source_valid(idx))return TAPE_GAP_ERROR;
        if(owned)owned->gaps_ready=complete && !owned->gaps_truncated;
        if(complete)tape_time_navigation_duration(idx,total_ticks);
    }
    if(found)*target=at;
    return found ? TAPE_GAP_FOUND:TAPE_GAP_NONE;
}
void tape_cursor_close(tape_cursor_t *c) {
    if(c && c->file){cmt_fclose(c->file);c->file=NULL;}
}
static bool cursor_restore(tape_cursor_t *c,const tape_checkpoint_t *cp) {
    if(!cp || !cp->valid || cp->source_offset>c->end || cp->source_offset>LONG_MAX ||
       cp->kind!=(c->format==FILE_FORMAT_WAV ? TAPE_CHECKPOINT_WAV:TAPE_CHECKPOINT_EDGE) ||
       cp->level>1 || cp->hysteresis>1)return false;
    if(c->format==FILE_FORMAT_WAV && (cp->source_offset<c->wav.data_offset ||
       cp->sample_index!=cp->source_offset-c->wav.data_offset ||
       cp->time_ticks16>UINT64_MAX-cp->remaining_ticks16 ||
       cp->time_ticks16+cp->remaining_ticks16!=wav_pulse_time(cp->sample_index,c->wav.sample_rate,16000000)))return false;
    if(cp->time_us!=cp->time_ticks16/16)return false;
    if(cmt_fseek(c->file,(long)cp->source_offset,SEEK_SET))return false;
    c->offset=cp->source_offset;c->ticks16=cp->time_ticks16;c->sample_index=cp->sample_index;
    c->remaining_ticks16=cp->remaining_ticks16;c->level=cp->level;c->hysteresis=cp->hysteresis;
    c->position=c->available=0;return true;
}
bool tape_cursor_open(tape_cursor_t *c,const tape_index_t *idx,const tape_checkpoint_t *cp) {
    if(!c || !idx || !tape_analyzer_supported(idx->source.path))return false;
    tape_cursor_close(c);memset(c,0,sizeof(*c));c->format=idx->source.format;c->time_known=true;
    c->file=cmt_fopen(idx->source.path,"rb");if(!c->file)return false;
    c->end=idx->source.size_bytes;c->limit_ticks16=UINT64_MAX;
    if(c->format==FILE_FORMAT_WAV) {
        if(!wav_reader_parse(c->file,&c->wav) || !wav_reader_supported(&c->wav))goto fail;
        c->offset=c->wav.data_offset;c->end=c->offset+c->wav.data_size;
    }
    if(c->offset>LONG_MAX || cmt_fseek(c->file,(long)c->offset,SEEK_SET))goto fail;
    if(cp && !cursor_restore(c,cp))goto fail;
    return true;
fail:
    c->error=true;tape_cursor_close(c);return false;
}
void tape_cursor_checkpoint(const tape_cursor_t *c,tape_checkpoint_t *cp) {
    memset(cp,0,sizeof(*cp));cp->source_offset=c->offset;cp->time_ticks16=c->ticks16;
    cp->time_us=c->ticks16/16;cp->sample_index=c->sample_index;cp->remaining_ticks16=c->remaining_ticks16;
    cp->kind=c->format==FILE_FORMAT_WAV ? TAPE_CHECKPOINT_WAV:TAPE_CHECKPOINT_EDGE;
    cp->level=c->level;cp->hysteresis=c->hysteresis;cp->valid=!c->error && c->time_known;
}
bool tape_cursor_seek_offset(tape_cursor_t *c,const tape_index_t *idx,uint64_t offset,
                             tape_analyzer_control_fn control,void *context) {
    if(!c || !idx || (idx->source.format!=FILE_FORMAT_LEP && idx->source.format!=FILE_FORMAT_L16))return false;
    tape_cursor_close(c);
    if(!idx->scan_complete || !tape_index_source_valid(idx) || (control && !control(context)) ||
       !tape_cursor_open(c,idx,NULL))return false;
    if(offset>c->end)offset=c->end;
    uint8_t level=0;bool found=false;
    if(offset<c->end) {
        if(cmt_fseek(c->file,(long)offset,SEEK_SET) || cmt_fread(c->buffer,1,1,c->file)!=1)goto fail_offset;
        ++c->read_calls;++c->read_bytes;
        if(c->buffer[0]){level=c->buffer[0]<128;found=true;}
    }else found=true; /* Empty EOF needs no level reconstruction. */
    uint64_t before=offset;
    while(!found && before) {
        if(control && !control(context))goto fail_offset;
        size_t n=before>sizeof(c->buffer) ? sizeof(c->buffer):(size_t)before;
        before-=n;
        if(cmt_fseek(c->file,(long)before,SEEK_SET) || cmt_fread(c->buffer,1,n,c->file)!=n)goto fail_offset;
        ++c->read_calls;c->read_bytes+=n;
        for(size_t i=n;i;--i)if(c->buffer[i-1]){level=c->buffer[i-1]<128;found=true;break;}
    }
    if(cmt_fseek(c->file,(long)offset,SEEK_SET))goto fail_offset;
    c->offset=offset;c->level=level;c->time_known=offset==0;
    c->ticks16=0;c->position=c->available=0;c->remaining_ticks16=0;
    return true;
fail_offset:
    tape_cursor_close(c);return false;
}
static int peek(tape_cursor_t *c) {
    if(c->offset>=c->end)return -1;
    if(c->position==c->available) {
        size_t n=c->end-c->offset;if(n>sizeof(c->buffer))n=sizeof(c->buffer);
        c->available=cmt_fread(c->buffer,1,n,c->file);c->position=0;
        ++c->read_calls;c->read_bytes+=c->available;
        if(c->available!=n)c->error=true;
        if(!c->available)return -1;
    }
    return c->buffer[c->position];
}
bool tape_cursor_next(tape_cursor_t *c,uint8_t *level,uint32_t *ticks) {
    if(!c || !c->file || !level || !ticks || c->error || c->ticks16>=c->limit_ticks16)return false;
    uint32_t duration=c->remaining_ticks16;c->remaining_ticks16=0;
    if(!duration) {
        int value=peek(c);if(value<0 || c->error)return false;
        if(c->format==FILE_FORMAT_WAV) {
            uint64_t start=c->sample_index;unsigned n=0;
            c->level=wav_pulse_hysteresis(c->hysteresis,(uint8_t)value);
            do {
                uint8_t next=wav_pulse_hysteresis(c->hysteresis,(uint8_t)value);
                if(n && next!=c->level)break;
                c->hysteresis=next;++c->position;++c->offset;++c->sample_index;++n;
                value=peek(c);
            }while(n<256 && value>=0 && !c->error);
            duration=(uint32_t)(wav_pulse_time(c->sample_index,c->wav.sample_rate,16000000)-
                               wav_pulse_time(start,c->wav.sample_rate,16000000));
        } else {
            int signed_value=(int8_t)value;
            if(signed_value)c->level=signed_value>0;
            unsigned units=signed_value==0 ? 127:signed_value<0 ? -signed_value:signed_value;
            duration=units*(c->format==FILE_FORMAT_L16 ? 256U:800U);
            ++c->position;++c->offset;
        }
    }
    uint64_t room=c->limit_ticks16-c->ticks16;if(duration>room)duration=(uint32_t)room;
    if(c->error || !duration)return false;
    *level=c->level;*ticks=duration;c->ticks16+=duration;return true;
}
static uint32_t add_checkpoint(const tape_checkpoint_t *cp) {
    if(index_data.checkpoint_count==TAPE_INDEX_CHECKPOINT_CAPACITY) {
        /* Compact only ordinary time checkpoints. Program anchors are pinned
         * in the first PROGRAM_CAPACITY slots and never silently discarded. */
        unsigned out=TAPE_INDEX_PROGRAM_CAPACITY;
        for(unsigned i=TAPE_INDEX_PROGRAM_CAPACITY;i<index_data.checkpoint_count;i+=2)checkpoints[out++]=checkpoints[i];
        index_data.checkpoint_count=out;checkpoint_stride*=2;
    }
    uint32_t n=index_data.checkpoint_count++;checkpoints[n]=*cp;return n;
}
static uint32_t block_add(tape_block_kind_t kind,tape_validity_t validity,
                         const tape_checkpoint_t *begin,const tape_checkpoint_t *end,uint32_t program) {
    if(kind==TAPE_BLOCK_GAP && index_data.block_count) {
        tape_block_t *previous=&blocks[index_data.block_count-1];
        if(previous->kind==TAPE_BLOCK_GAP && previous->range.offset_end==begin->source_offset &&
           previous->range.time_end_us==begin->time_us) {
            previous->range.offset_end=end->source_offset;previous->range.time_end_us=end->time_us;
            covered_offset=end->source_offset;covered_ticks=end->time_ticks16;return index_data.block_count-1;
        }
    }
    if(index_data.block_count==TAPE_INDEX_BLOCK_CAPACITY){index_data.truncated=true;return TAPE_INDEX_NO_ENTRY;}
    uint32_t n=index_data.block_count++;tape_block_t *b=&blocks[n];memset(b,0,sizeof(*b));
    b->range=(tape_range_t){begin->source_offset,end->source_offset,begin->time_us,end->time_us};
    b->range_flags=TAPE_RANGE_OFFSET_VALID|TAPE_RANGE_TIME_VALID;b->kind=kind;b->validity=validity;
    b->program_index=program;b->next_in_program=TAPE_INDEX_NO_ENTRY;
    b->protocol=PROGRAM_SHARP;b->source=PROGRAM_TAPE_DECODE;
    b->checksum_present=kind==TAPE_BLOCK_HEADER || kind==TAPE_BLOCK_PAYLOAD;
    b->checksum_verified=b->checksum_present && validity==TAPE_VALIDITY_VERIFIED;
    if(program!=TAPE_INDEX_NO_ENTRY && program<index_data.program_count_recognized) {
        tape_program_t *p=&programs[program];
        if(p->block_count)blocks[last_block].next_in_program=n;else p->first_block=n;
        ++p->block_count;last_block=n;
    }
    if(end->source_offset>covered_offset){covered_offset=end->source_offset;covered_ticks=end->time_ticks16;}
    return n;
}
static void observe(mz_tape_observation_t event,bool header,void *context) {
    (void)context;
    if(event==MZ_OBSERVE_LEADER){
        /* Decoding still uses physical LOW only. The immediately preceding
         * short HIGH is part of the original first leader pulse, not a new
         * polarity candidate. Retain it in the restorable start boundary. */
        leader=previous_high && previous_end.time_ticks16==run_begin.time_ticks16 ? previous_begin:run_begin;
        leader_known=true;mark_known=false;header_leader=header;
    }
    else if(event==MZ_OBSERVE_MARK){mark=run_begin;mark_known=leader_known;header_leader=header;}
    else observed|=1U<<event;
}
static bool same_program(const program_metadata_t *a,const program_metadata_t *b) {
    return a->protocol==b->protocol && a->load==b->load && a->exec==b->exec &&
        a->declared_size==b->declared_size && !strcmp(a->title,b->title);
}
static void collect(void) {
    uint32_t revision=preflight_record_autoname_metadata_revision();
    if(!observed && revision==last_revision)return;
    last_revision=revision;
    program_metadata_t metadata;preflight_tape_metadata_snapshot(&metadata);metadata.format=index_data.source.format;
    preview=metadata;
    /* AUX decoders provide proven metadata, but not a proven entire loading
     * chain/start boundary. Retain their candidates without inventing anchors. */
    if(!observed && (metadata.protocol==PROGRAM_SPECTRUM || metadata.protocol==PROGRAM_CPM) &&
       (metadata.confidence==PROGRAM_HEADER_VALID || metadata.confidence==PROGRAM_PAYLOAD_VALID)) {
        if(active_program==TAPE_INDEX_NO_ENTRY || !same_program(&programs[active_program].metadata,&metadata)) {
            if(index_data.program_count_recognized<TAPE_INDEX_PROGRAM_CAPACITY) {
                active_program=index_data.program_count_recognized++;
                tape_program_t *p=&programs[active_program];memset(p,0,sizeof(*p));
                p->first_block=p->start_checkpoint=TAPE_INDEX_NO_ENTRY;
            } else {index_data.truncated=true;active_program=TAPE_INDEX_NO_ENTRY;}
        }
        if(active_program!=TAPE_INDEX_NO_ENTRY) {
            programs[active_program].metadata=metadata;
            (void)preflight_record_autoname_get_detected_aux_profile(&programs[active_program].aux_profile);
        }
    }
    if(observed&(1U<<MZ_OBSERVE_HEADER)) {
        bool duplicate=active_program!=TAPE_INDEX_NO_ENTRY &&
            programs[active_program].chain_complete && same_program(&programs[active_program].metadata,&metadata) &&
            run_end.time_us-programs[active_program].chain.time_end_us<1000000;
        if(!duplicate) {
            if(index_data.program_count_recognized==TAPE_INDEX_PROGRAM_CAPACITY){index_data.truncated=true;active_program=TAPE_INDEX_NO_ENTRY;}
            else {
                active_program=index_data.program_count_recognized++;tape_program_t *p=&programs[active_program];memset(p,0,sizeof(*p));
                p->first_block=p->start_checkpoint=TAPE_INDEX_NO_ENTRY;
                if(leader_known && mark_known && header_leader){
                    p->chain.offset_begin=leader.source_offset;p->chain.time_begin_us=leader.time_us;
                    p->chain_begin_ticks16=leader.time_ticks16;
                    p->start_checkpoint=active_program;checkpoints[active_program]=leader;
                    p->range_flags=TAPE_RANGE_OFFSET_VALID|TAPE_RANGE_TIME_VALID;
                }
            }
        }
        if(active_program!=TAPE_INDEX_NO_ENTRY)programs[active_program].metadata=metadata;
    }
    if(observed && leader_known && mark_known) {
        tape_checkpoint_t end=run_end;
        uint32_t program=active_program;
        if(observed&(1U<<MZ_OBSERVE_INVALID))program=header_leader ? TAPE_INDEX_NO_ENTRY:active_program;
        if(leader.source_offset>covered_offset) {
            tape_checkpoint_t gap=leader;gap.source_offset=covered_offset;gap.time_ticks16=covered_ticks;gap.time_us=covered_ticks/16;
            /* Unclassified intervening waveform is never declared verified. */
            block_add(TAPE_BLOCK_UNKNOWN,TAPE_VALIDITY_UNCERTAIN,&gap,&leader,TAPE_INDEX_NO_ENTRY);
            unknown_content=true;
        }
        bool valid=!(observed&(1U<<MZ_OBSERVE_INVALID));
        block_add(TAPE_BLOCK_LEADER,valid ? TAPE_VALIDITY_VERIFIED:TAPE_VALIDITY_UNCERTAIN,&leader,&mark,program);
        block_add(header_leader ? TAPE_BLOCK_HEADER:TAPE_BLOCK_PAYLOAD,
                  valid ? TAPE_VALIDITY_VERIFIED:TAPE_VALIDITY_INVALID,&mark,&end,program);
        leader_known=mark_known=false;
    }
    if(active_program!=TAPE_INDEX_NO_ENTRY &&
       ((observed&((1U<<MZ_OBSERVE_HEADER)|(1U<<MZ_OBSERVE_VALID))) ||
        ((observed&(1U<<MZ_OBSERVE_INVALID)) && !header_leader))) {
        tape_program_t *p=&programs[active_program];p->metadata=metadata;
        p->chain.offset_end=run_end.source_offset;p->chain.time_end_us=run_end.time_us;
        p->chain_end_ticks16=run_end.time_ticks16;
        if(metadata.confidence==PROGRAM_PAYLOAD_VALID) {
            p->chain_complete=true;
            /* A checksum alone cannot prove an unknown BASIC/module dependency.
             * Only a complete recognized machine-code chain with its entry
             * inside the loaded image is independently startable here. */
            uint32_t load_end=(uint32_t)metadata.load+metadata.declared_size;
            p->dependencies_verified=metadata.protocol==PROGRAM_SHARP && metadata.file_type==1 &&
                metadata.declared_size && load_end<=65536U && metadata.exec>=metadata.load && metadata.exec<load_end;
            if(dependency_unknown && metadata.source!=PROGRAM_LOADER_EMBEDDED)p->dependencies_verified=false;
            if(!p->dependencies_verified)dependency_unknown=true;
            p->seekable=p->dependencies_verified && p->start_checkpoint!=TAPE_INDEX_NO_ENTRY;
            p->validity=p->seekable ? TAPE_VALIDITY_VERIFIED:TAPE_VALIDITY_UNCERTAIN;
        } else if(metadata.confidence==PROGRAM_INVALID) {
            p->chain_complete=false;p->seekable=false;p->validity=TAPE_VALIDITY_INVALID;
        }
    }
    checksum_tail=(observed&((1U<<MZ_OBSERVE_HEADER)|(1U<<MZ_OBSERVE_VALID)|(1U<<MZ_OBSERVE_INVALID)))!=0;
    observed=0;
}
static void flush_run(void) {
    if(!have_run)return;
    uint64_t units=(run_ticks+128U)/256U;
    bool tail=checksum_tail && run_level==1 && run_begin.time_ticks16==covered_ticks;
    checksum_tail=false;
    observed=0;
    bool flat=units>1024 || long_continuation;
    if(!units || flat)preflight_record_autoname_break_signal();
    else preflight_record_autoname_feed_level_interval((uint16_t)units,run_level);
    collect();
    if(tail && index_data.block_count) {
        /* The validated checksum ends on the decisive LOW half-wave. Preserve
         * its original HIGH half too, rather than cutting a symbol in half. */
        tape_block_t *b=&blocks[index_data.block_count-1];
        if(b->kind==TAPE_BLOCK_HEADER || b->kind==TAPE_BLOCK_PAYLOAD) {
            b->range.offset_end=run_end.source_offset;b->range.time_end_us=run_end.time_us;
            covered_offset=run_end.source_offset;covered_ticks=run_end.time_ticks16;
            if(b->program_index<index_data.program_count_recognized) {
                tape_program_t *p=&programs[b->program_index];
                p->chain.offset_end=run_end.source_offset;p->chain.time_end_us=run_end.time_us;p->chain_end_ticks16=run_end.time_ticks16;
            }
        }
    }
    if(flat && run_begin.source_offset>=covered_offset) {
        if(run_begin.source_offset>covered_offset) {
            tape_checkpoint_t begin=run_begin;begin.source_offset=covered_offset;begin.time_ticks16=covered_ticks;begin.time_us=covered_ticks/16;
            block_add(TAPE_BLOCK_UNKNOWN,TAPE_VALIDITY_UNCERTAIN,&begin,&run_begin,TAPE_INDEX_NO_ENTRY);unknown_content=true;
        }
        block_add(TAPE_BLOCK_GAP,TAPE_VALIDITY_UNCERTAIN,&run_begin,&run_end,TAPE_INDEX_NO_ENTRY);
    }
    previous_begin=run_begin;previous_end=run_end;previous_high=run_level==1 && !flat;
    have_run=false;
}
static void finish_scan(bool success) {
    flush_run();preflight_tape_metadata_end();preflight_mz_tape_decoder_set_observer(NULL,NULL);
    index_data.cursor_read_calls=scan.read_calls;index_data.cursor_read_bytes=scan.read_bytes;
    if(success) {
        index_data.gaps_ready=!index_data.gaps_truncated;
        index_data.waveform_ticks16=scan.ticks16;index_data.waveform_us=(scan.ticks16+8)/16;
        index_data.duration_known=true;index_data.scan_complete=true;
        tape_checkpoint_t end;tape_cursor_checkpoint(&scan,&end);
        if(covered_offset<end.source_offset) {
            tape_checkpoint_t begin=end;begin.source_offset=covered_offset;begin.time_us=covered_ticks/16;begin.time_ticks16=covered_ticks;
            block_add(TAPE_BLOCK_UNKNOWN,TAPE_VALIDITY_UNCERTAIN,&begin,&end,TAPE_INDEX_NO_ENTRY);unknown_content=true;
        }
        add_checkpoint(&end);
        for(unsigned i=0;i<index_data.program_count_recognized;++i) {
            if(index_data.truncated){programs[i].seekable=false;programs[i].validity=TAPE_VALIDITY_UNCERTAIN;}
            if(programs[i].validity==TAPE_VALIDITY_VERIFIED)++index_data.program_count_verified;
        }
        index_data.index_complete=!unknown_content && !index_data.truncated &&
            index_data.program_count_recognized && index_data.program_count_recognized==index_data.program_count_verified;
        index_data.state=TAPE_INDEX_FINISHED;
        cache_valid=tape_index_source_valid(&index_data);
        if(!cache_valid)index_data.state=TAPE_INDEX_ERROR;
    } else {index_data.state=TAPE_INDEX_ERROR;cache_valid=false;}
    tape_cursor_close(&scan);__atomic_store_n(&owner,false,__ATOMIC_RELEASE);
}
bool tape_analyzer_begin(const char *path) {
    if(!tape_analyzer_supported(path) || strlen(path)>=TAPE_INDEX_PATH_CAPACITY ||
       __atomic_exchange_n(&owner,true,__ATOMIC_ACQUIRE))return false;
    if(cache_valid && !strcmp(index_data.source.path,path) && tape_index_source_valid(&index_data)) {
        index_data.cache_hit=true;__atomic_store_n(&owner,false,__ATOMIC_RELEASE);return true;
    }
    cache_valid=false;memset(&index_data,0,sizeof(index_data));
    index_data.state=TAPE_INDEX_ERROR;struct stat st;
    if(!allocate_pools() || cmt_stat(path,&st) || !S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size>LONG_MAX)goto fail;
    index_data.source.format=file_format_detect_from_name(path);
    strcpy(index_data.source.path,path);index_data.source.size_bytes=st.st_size;
    index_data.source.mtime_seconds=st.st_mtime;index_data.source.ctime_seconds=st.st_ctime;
    index_data.source.device=st.st_dev;index_data.source.inode=st.st_ino;
    index_data.source.storage_epoch=cmt_storage_epoch();index_data.source.stat_valid=true;
    if(!tape_cursor_open(&scan,&index_data,NULL))goto fail;
    if(!fingerprint(scan.file,index_data.source.size_bytes,&index_data.source.fingerprint) ||
       cmt_fseek(scan.file,(long)scan.offset,SEEK_SET))goto fail;
    index_data.source.fingerprint_valid=true;index_data.source.fingerprint_version=1;
    index_data.schema_version=TAPE_INDEX_SCHEMA_VERSION;index_data.algorithm_version=3;
    index_data.program_capacity=TAPE_INDEX_PROGRAM_CAPACITY;index_data.block_capacity=TAPE_INDEX_BLOCK_CAPACITY;
    index_data.checkpoint_capacity=TAPE_INDEX_CHECKPOINT_CAPACITY;
    index_data.programs=programs;index_data.blocks=blocks;index_data.checkpoints=checkpoints;
    gap_init(&index_data,&deep_gaps,&deep_gap);
    memset(checkpoints,0,TAPE_INDEX_PROGRAM_CAPACITY*sizeof(*checkpoints));
    index_data.checkpoint_count=TAPE_INDEX_PROGRAM_CAPACITY;
    tape_checkpoint_t start;tape_cursor_checkpoint(&scan,&start);add_checkpoint(&start);
    covered_offset=scan.offset;covered_ticks=0;
    checkpoint_stride=1000000;next_checkpoint=checkpoint_stride;
    have_run=leader_known=mark_known=unknown_content=dependency_unknown=checksum_tail=long_continuation=previous_high=false;run_ticks=0;observed=0;
    active_program=last_block=TAPE_INDEX_NO_ENTRY;
    program_metadata_reset(&preview,index_data.source.format);
    preflight_tape_metadata_begin(index_data.source.format);
    last_revision=preflight_record_autoname_metadata_revision();
    preflight_mz_tape_decoder_set_observer(observe,NULL);
    index_data.synthetic_preroll_us=scan.format==FILE_FORMAT_WAV ? 0:300000;
    index_data.synthetic_tail_us=scan.format==FILE_FORMAT_WAV ? 0:500000;
    if(scan.format==FILE_FORMAT_WAV){index_data.waveform_ticks16=wav_pulse_time(scan.wav.data_size,scan.wav.sample_rate,16000000);index_data.waveform_us=(index_data.waveform_ticks16+8)/16;index_data.duration_known=true;}
    index_data.state=TAPE_INDEX_SCANNING;return true;
fail:
    tape_cursor_close(&scan);__atomic_store_n(&owner,false,__ATOMIC_RELEASE);return false;
}
bool tape_analyzer_step(void) {
    if(index_data.state!=TAPE_INDEX_SCANNING)return false;
    if(index_data.source.storage_epoch!=cmt_storage_epoch()){finish_scan(false);return false;}
    uint64_t begin=scan.offset;
    do {
        tape_checkpoint_t at;tape_cursor_checkpoint(&scan,&at);
        uint8_t level;uint32_t ticks;
        if(!tape_cursor_next(&scan,&level,&ticks)){finish_scan(!scan.error);return false;}
        gap_feed(&index_data,&deep_gap,level,at.time_ticks16,scan.ticks16,at.source_offset,scan.offset);
        if(at.time_us>=next_checkpoint){add_checkpoint(&at);next_checkpoint=at.time_us+checkpoint_stride;}
        if(have_run && level!=run_level)flush_run();
        if(level!=long_level)long_continuation=false;
        if(!have_run){run_begin=at;run_level=level;run_ticks=0;have_run=true;}
        run_ticks+=ticks;tape_cursor_checkpoint(&scan,&run_end);
        /* A long flat run cannot contain a supported leader. Bound decoder
         * work without splitting real transitions or losing accumulated time. */
        if(run_ticks>524288U){flush_run();long_continuation=true;long_level=level;}
        index_data.scanned_bytes=scan.offset;
    }while(scan.offset-begin<1024U);
    return true;
}
void tape_analyzer_cancel(void) {
    if(index_data.state==TAPE_INDEX_SCANNING){
        preflight_mz_tape_decoder_set_observer(NULL,NULL);tape_cursor_close(&scan);
        index_data.state=TAPE_INDEX_CANCELLED;cache_valid=false;have_run=false;
        __atomic_store_n(&owner,false,__ATOMIC_RELEASE);
    }
}
void tape_analyzer_invalidate(void){tape_analyzer_cancel();cache_valid=false;}
const tape_index_t *tape_analyzer_result(void){return &index_data;}
void tape_analyzer_preview(program_metadata_t *out){if(out)*out=preview;}
uint8_t tape_analyzer_percent(void) {
    if(index_data.state==TAPE_INDEX_FINISHED)return 100;
    if(index_data.state==TAPE_INDEX_SCANNING && scan.format==FILE_FORMAT_WAV)
        return scan.wav.data_size ? (uint8_t)((scan.offset-scan.wav.data_offset)*100/scan.wav.data_size):0;
    return index_data.source.size_bytes ? (uint8_t)(index_data.scanned_bytes*100/index_data.source.size_bytes):0;
}
/* WAV has no compulsory waveform prescan in ordinary PLAY. Resolve the last
 * stable threshold backwards only when a time seek is actually requested.
 * In a long deadband the exact answer can require looking back to the start;
 * report progress and poll cancellation between bounded 1 KiB reads. */
static bool seek_wav_time(tape_cursor_t *c,const tape_index_t *idx,uint64_t target,
                          tape_analyzer_control_fn control,void *context) {
    if(!tape_cursor_open(c,idx,NULL))return false;
    if(!target)return true;
    tape_checkpoint_t cp={.kind=TAPE_CHECKPOINT_WAV,.valid=true,.time_ticks16=target,.time_us=target/16};
    if(target==idx->waveform_ticks16) {
        cp.source_offset=c->end;cp.sample_index=c->wav.data_size;
        if(!cursor_restore(c,&cp))goto failed;
        return true;
    }
    uint32_t rate=c->wav.sample_rate;
    uint64_t sample=(target/16000000)*rate+(target%16000000)*rate/16000000;
    while(sample<c->wav.data_size && wav_pulse_time(sample+1,rate,16000000)<=target)++sample;
    while(sample && wav_pulse_time(sample,rate,16000000)>target)--sample;
    uint64_t end=sample+1,total=end;uint8_t state=0;bool found=false;
    seek_percent=0;
    while(end && !found) {
        if(idx->source.storage_epoch!=cmt_storage_epoch() || (control && !control(context)))goto failed;
        uint64_t begin=end>sizeof(c->buffer) ? end-sizeof(c->buffer):0;size_t n=(size_t)(end-begin);
        if(cmt_fseek(c->file,(long)(c->wav.data_offset+begin),SEEK_SET) || cmt_fread(c->buffer,1,n,c->file)!=n)goto failed;
        for(size_t i=n;i;--i) {
            uint8_t low=wav_pulse_hysteresis(0,c->buffer[i-1]),high=wav_pulse_hysteresis(1,c->buffer[i-1]);
            if(low==high){state=low;found=true;break;}
        }
        end=begin;seek_percent=(int)((total-end)*100/total);
    }
    cp.source_offset=c->wav.data_offset+sample+1;cp.sample_index=sample+1;
    cp.level=cp.hysteresis=state;
    cp.remaining_ticks16=(uint32_t)(wav_pulse_time(sample+1,rate,16000000)-target);
    seek_percent=-1;
    if(!cursor_restore(c,&cp))goto failed;
    return true;
failed:
    seek_percent=-1;tape_cursor_close(c);return false;
}
bool tape_cursor_seek(tape_cursor_t *c,const tape_index_t *idx,uint64_t us,tape_analyzer_control_fn control,void *ctx) {
    if(!c)return false;
    tape_cursor_close(c);
    if(!idx || !idx->scan_complete || !tape_index_source_valid(idx))return false;
    if(idx->duration_known && us>idx->waveform_us)us=idx->waveform_us;
    if(us>UINT64_MAX/16)us=UINT64_MAX/16;
    uint64_t target=idx->duration_known && us==idx->waveform_us ? idx->waveform_ticks16:us*16;
    if(idx->time_only && idx->source.format==FILE_FORMAT_WAV) {
        if(control && !control(ctx))return false;
        return seek_wav_time(c,idx,target,control,ctx);
    }
    const tape_checkpoint_t *best=NULL;
    for(unsigned i=0;i<idx->checkpoint_count;++i) {
        const tape_checkpoint_t *cp=&idx->checkpoints[i];
        if(cp->valid && cp->time_ticks16<=target && (!best || cp->time_ticks16>best->time_ticks16))best=cp;
    }
    if(!best || !tape_cursor_open(c,idx,best))return false;
    if(control && !control(ctx))goto fail;
    uint64_t polled=c->offset;
    while(c->ticks16<target) {
        if(c->offset-polled>=1024U) {
            polled=c->offset;
            if(idx->source.storage_epoch!=cmt_storage_epoch() || (control && !control(ctx)))goto fail;
        }
        uint8_t level;uint32_t ticks;
        if(!tape_cursor_next(c,&level,&ticks)) {
            if(!c->error && c->offset==c->end && !idx->duration_known){tape_time_navigation_duration(idx,c->ticks16);return true;}
            goto fail;
        }
        if(c->ticks16>target) {
            c->remaining_ticks16=(uint32_t)(c->ticks16-target);c->ticks16=target;
        }
    }
    return true;
fail:
    tape_cursor_close(c);return false;
}
