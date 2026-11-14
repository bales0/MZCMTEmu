#include "storage/fs.h"
/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from bales0/MZ-SD2CMT2-Reborn, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * ESP32-S3: native constant data and foreground algorithms; no AVR peripherals.
 */
#include "audio/sharp_stream.h"
#include "formats/mz_tape_profiles.h"
#include "formats/sidecar.h"
#include "formats/quickdisk.h"
#include "formats/mz_title.h"
#include "play/mzf_loader.h"
#include <stdio.h>
#include <string.h>

typedef enum { HEADER, LOADER, PAYLOAD, TURBO, EXTRA, UL, FINISHED } stage_t;
typedef enum { LEADER, MARK_LONG, MARK_SHORT, MARK_FINAL, LOAD_BYTE, BITS, STOP_BIT,
    LOAD_CHECKSUM, CHECKSUM_BITS, CHECKSUM_STOP, TRAILING, BOUNDARY } step_t;
static cmt_file_t *fp;
/* Valid only while this immutable source handle stays open. SD removal/STOP
 * closes the session and invalidates both the QD directory and durations. */
static char open_path[1024];
static struct {uint16_t record;loader_mode_t mode;uint64_t ticks;} duration_cache[8];
static unsigned duration_next;
static bool qd_source;
static sharp_analysis_cancel_fn analysis_cancel;
static void *analysis_context;
static bool analysis_cancelled;
void sharp_stream_set_analysis_control(sharp_analysis_cancel_fn cancel,void *context) {
    analysis_cancel=cancel;
    analysis_context=context;
}
bool sharp_stream_analysis_cancelled(void) { return analysis_cancelled; }
static bool analysis_poll(void *context) {
    (void)context;
    if(analysis_cancel && analysis_cancel(analysis_context)) analysis_cancelled=true;
    return analysis_cancelled;
}
static uint32_t qd_position;
static uint8_t header[128], loader_bytes[128];
static sharp_stream_info_t details;
static mz_tape_profile_t native_profile,payload_profile;
static uint32_t file_size,payload_start,payload_end,declared_length,stage_length,read_bytes,loops;
static uint16_t loader_length,checksum;
static uint8_t byte,bits,checksum_index;
static uint16_t pending_high_ticks;
static bool pending_source_byte,last_source_byte;
bool sharp_stream_last_pulse_source_byte(void){return last_source_byte;}
static stage_t stage;
static step_t step;
static bool error;
static bool fail(void) { error=true;
    return false;
    }
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0]|(uint16_t)((uint16_t)p[1]<<8);
    }
static bool source_seek(uint32_t pos) {
    if(!fp || pos<payload_start || pos>payload_end) return false;
    if(qd_source) {
        if(!quickdisk_seek(pos)) return false;
        qd_position=pos;
        return true;
    }
    return !cmt_fseek(fp,(long)pos,SEEK_SET);
}
static int16_t source_read(void *buf,uint16_t n) {
    long pos=qd_source ? (long)qd_position : cmt_ftell(fp);
    if(pos<0 || (uint32_t)pos<payload_start || (uint32_t)pos>payload_end) return -1;
    uint32_t left=payload_end-(uint32_t)pos;
    if(n>left) n=(uint16_t)left;
    size_t got=qd_source ? quickdisk_read(buf,n) : cmt_fread(buf,1,n,fp);
    if(qd_source) { qd_position+=(uint32_t)got; return got==n ? (int16_t)got : -1; }
    return cmt_ferror(fp) ? -1 : (int16_t)got;
}
static bool locate(uint16_t wanted) {
    uint32_t pos=0;
    uint16_t count=0;
    while(pos<file_size) {
        uint8_t h[128];
        if(file_size-pos<128 || cmt_fseek(fp,(long)pos,SEEK_SET) || cmt_fread(h,1,128,fp)!=128) return fail();
        uint32_t length=le16(h+0x12);
        if(length>file_size-pos-128 || count==65535) return fail();
        ++count;
        if(count==wanted) { memcpy(header,h,128);
            payload_start=pos+128;
            payload_end=payload_start+length;
            declared_length=length;
            }
        pos+=128+length;
        if(details.format!=FILE_FORMAT_MZT) break;
    }
    details.records=count;
    return count && wanted<=count ? true : fail();
}
static void begin_stage(stage_t next) {
    pending_source_byte=last_source_byte=false;
    stage=next;
    step=LEADER;
    loops=0;
    read_bytes=0;
    checksum=0;
    bits=0;
    checksum_index=0;
    stage_length=stage==HEADER ? 128 : stage==LOADER ? loader_length :
        stage==EXTRA ? file_size-payload_end : declared_length;
    if(stage==PAYLOAD || stage==TURBO) { if(!source_seek(payload_start)) fail();
        }
    if(stage==EXTRA && cmt_fseek(fp,(long)payload_end,SEEK_SET)) fail();
}
static bool read_byte(uint8_t *out,bool peek) {
    if(read_bytes>=stage_length) return false;
    if(stage==HEADER) *out=header[read_bytes];
    else if(stage==LOADER) *out=loader_bytes[read_bytes];
    else if(qd_source) {
        if(source_read(out,1)!=1) return fail();
        if(peek && !source_seek(qd_position-1)) return fail();
    }
    else { int c=cmt_fgetc(fp);
        if(c==EOF) return fail();
        *out=(uint8_t)c;
        if(peek && cmt_ungetc(c,fp)==EOF) return fail();
        }
    if(!peek) ++read_bytes;
    return true;
}
static bool tc_trailing(void) { return mzf_loader_is_tc_turbo() && stage!=HEADER;
    }
static void pulse(bool is_long,unsigned region,bool next_long,uint16_t *high,uint16_t *low) {
    const mz_tape_profile_t *p=stage==TURBO ? &payload_profile : &native_profile;
    *high=is_long ? p->long_high_ticks : p->short_high_ticks;
    *low=is_long ? p->long_low_ticks : p->short_low_ticks;
    bool ul=mzf_loader_is_ul_active();
    if(region==0 && ul) {
        *high=details.effective==LOADER_MODE_UL_MZ700 ? 3840 : 3807;
        *low=details.effective==LOADER_MODE_UL_MZ700 ? 4224 : 4141;
    } else if(stage!=TURBO && native_profile.id==MZ_TAPE_PROFILE_MZ800_NORMAL_1X) {
        if(region==0) *low=4141;
        else if(region==1) *low=is_long ? 7795 : 4096;
        else *low=is_long ? (next_long ? 7741 : 7786) : (next_long ? 4042 : 4087);
    }
}
static bool cycle(uint16_t *high,uint16_t *low) {
    for(;;) {
        switch(step) {
        case LEADER:
            if(!loops) loops=mzf_loader_is_ul_active() ? 5500 : stage==HEADER ? 11000 : 5500;
            pulse(false,0,false,high,low);
            if(--loops==0) { step=MARK_LONG;
                loops=stage==HEADER ? 40 : 20;
                } return true;
        case MARK_LONG:
            pulse(true,1,false,high,low);
            if(--loops==0) { step=MARK_SHORT;
                loops=stage==HEADER ? 40 : 20;
                } return true;
        case MARK_SHORT:
            pulse(false,1,false,high,low);
            if(--loops==0) { step=MARK_FINAL;
                loops=2;
                } return true;
        case MARK_FINAL:
            pulse(true,1,false,high,low);
            if(--loops==0) step=LOAD_BYTE;
            return true;
        case LOAD_BYTE:
            if(read_bytes==stage_length) { checksum_index=0;
                step=LOAD_CHECKSUM;
                continue;
                }
            if(!read_byte(&byte,false)) return fail();
            bits=8;
            step=BITS;
            continue;
        case BITS: {
            bool is_long=(byte&0x80)!=0, next=bits>1 ? (byte&0x40)!=0 : true;
            pulse(is_long,2,next,high,low);
                if(is_long) ++checksum;
            byte<<=1;
                if(--bits==0) step=STOP_BIT;
                return true;
        }
        case STOP_BIT: {
            bool next=(checksum&0x8000)!=0;
            if(read_bytes<stage_length) { uint8_t peek;
                    if(!read_byte(&peek,true)) return fail();
                    next=(peek&0x80)!=0;
                    }
            pulse(true,2,next,high,low);
                step=LOAD_BYTE;
                return true;
        }
        case LOAD_CHECKSUM:
            if(checksum_index==2) { loops=tc_trailing() ? 98 : 2;
                step=TRAILING;
                continue;
                }
            byte=checksum_index==0 ? (uint8_t)(checksum>>8) : (uint8_t)checksum;
            ++checksum_index;
            bits=8;
            step=CHECKSUM_BITS;
            continue;
        case CHECKSUM_BITS:
            pulse((byte&0x80)!=0,2,bits>1 ? (byte&0x40)!=0 : true,high,low);
            byte<<=1;
            if(--bits==0) step=CHECKSUM_STOP;
            return true;
        case CHECKSUM_STOP:
            pulse(true,2,checksum_index<2 ? (checksum&0x80)!=0 : !tc_trailing(),high,low);
            step=LOAD_CHECKSUM;
            return true;
        case TRAILING:
            pulse(!tc_trailing(),2,tc_trailing() ? false : loops>1,high,low);
            if(--loops==0) step=BOUNDARY;
            return true;
        case BOUNDARY: return false;
        default: return fail();
        }
    }
}
bool sharp_stream_at_boundary(void) { return fp && !error && step==BOUNDARY && !pending_high_ticks && stage!=UL && stage!=FINISHED;
    }
bool sharp_stream_ul_ready(void) {
    return sharp_stream_at_boundary() && mzf_loader_is_ul_active() &&
        ((stage==HEADER && mzf_loader_is_header_only()) || stage==LOADER);
}
uint32_t sharp_stream_boundary_delay_us(bool motor_low_seen) {
    if(sharp_stream_ul_ready()) return 0;
    if((stage==TURBO || stage==PAYLOAD) && file_format_is_record_container(details.format) && details.record<details.records)
        return motor_low_seen ? 0:120000;
    if(stage==TURBO || stage==EXTRA || (stage==PAYLOAD &&
        (file_format_is_record_container(details.format) || payload_end==file_size))) return 0;
    uint32_t delay=0;
    if(stage==HEADER && (mzf_loader_is_ic_turbo() || mzf_loader_is_mz700_fast3())) delay=345000;
    else if(stage==LOADER && mzf_loader_is_tc_turbo()) delay=110000;
    else delay=120000;
    if(motor_low_seen && !(stage==LOADER && mzf_loader_is_tc_turbo())) delay=0;
    if(stage==HEADER && mzf_loader_is_mz700_fast3()) delay+=400000;
    return delay;
}
bool sharp_stream_advance(void) {
    if(!sharp_stream_at_boundary() || sharp_stream_ul_ready()) return false;
    if(stage==HEADER) {
        if(mzf_loader_is_ic_turbo() || mzf_loader_is_mz700_fast3()) begin_stage(TURBO);
        else if(mzf_loader_is_active()) begin_stage(LOADER);
        else begin_stage(PAYLOAD);
    } else if(stage==LOADER && mzf_loader_is_tc_turbo()) begin_stage(TURBO);
    else if(!file_format_is_record_container(details.format) && payload_end<file_size && stage!=EXTRA && !mzf_loader_is_active()) begin_stage(EXTRA);
    else stage=FINISHED;
    return !error;
}
bool sharp_stream_next_pulse(uint8_t *level,uint32_t *ticks,bool *count) {
    last_source_byte=false;
    if(!fp || error || !level || !ticks || !count || stage==FINISHED || stage==UL) return false;
    if(pending_high_ticks) { *level=1;
        *ticks=pending_high_ticks;
        *count=true;
        pending_high_ticks=0;
        last_source_byte=pending_source_byte;pending_source_byte=false;
        return true;
        }
    bool data_byte=(stage==PAYLOAD || stage==TURBO || stage==EXTRA) && step==STOP_BIT;
    uint16_t high,low;
    if(!cycle(&high,&low)) return false;
    pending_source_byte=data_byte;
    /* Fixed reference connector inversion: logical HIGH is first physical LOW. */
    *level=0;
    *ticks=high;
    *count=false;
    pending_high_ticks=low;
    return true;
}
void sharp_stream_close(void) {
    open_path[0]=0;pending_source_byte=last_source_byte=false;
    memset(duration_cache,0,sizeof(duration_cache));
    duration_next=0;
    quickdisk_close();
    qd_source=false;
    qd_position=0;
    if(fp) cmt_fclose(fp);
    fp=NULL;
    error=false;
    pending_high_ticks=0;
    stage=FINISHED;
    mzf_loader_reset();
    mzf_loader_set_source_io(NULL,NULL);
}
static bool prepare_record(const char *path,loader_mode_t requested,sharp_stream_info_t *info,bool cancellable,bool measure) {
    (void)mz_title_decode_display(header+1,17,details.title,sizeof(details.title));
    details.payload_bytes=declared_length;
    details.effective=sidecar_resolve(path,details.record,requested,&details.sidecar);
    /* Snapshot original source before the generated transport loader patches it. */
    program_metadata_reset(&details.metadata,details.format);
    details.metadata.record=details.record;details.metadata.records=details.records;
    uint8_t first_payload[90];size_t first_count=declared_length<sizeof(first_payload) ? declared_length:sizeof(first_payload);
    if(!source_seek(payload_start) || source_read(first_payload,(uint16_t)first_count)!=(int16_t)first_count)return fail();
    (void)program_metadata_source_sharp(&details.metadata,header,first_payload,first_count);
    mzf_loader_set_source_io(source_seek,source_read);
    bool active=mzf_loader_prepare(FILE_FORMAT_MZF,details.effective,header,payload_end,payload_start);
    bool native=details.effective==LOADER_MODE_NORMAL_1_1 || details.effective==LOADER_MODE_NORMAL_1_2 ||
        details.effective==LOADER_MODE_NORMAL_1_3 || details.effective==LOADER_MODE_NORMAL_1_4 || details.effective==LOADER_MODE_MZ700_1X;
    if(!active && !native) { details.effective=LOADER_MODE_NORMAL_1_1;
        details.fallback=true;
        mzf_loader_reset();
        }
    mz_tape_profile_read(mz_tape_profile_for_mode(details.effective),&payload_profile);
    mz_tape_profile_id_t base=mzf_loader_is_active() ? (mzf_loader_is_mz700_fast3() ? MZ_TAPE_PROFILE_MZ700_NORMAL_1X : MZ_TAPE_PROFILE_MZ800_NORMAL_1X) : payload_profile.id;
    if(!mz_tape_profile_read(base,&native_profile)) goto bad;
    loader_length=0;
    if(active) {
        if(!mzf_loader_patch_loader_header(header)) goto bad;
        if(!mzf_loader_is_header_only() && !mzf_loader_is_ic_turbo() && !mzf_loader_is_mz700_fast3()) {
            loader_length=mzf_loader_build_loader(loader_bytes,sizeof(loader_bytes));
            if(!loader_length) goto bad;
        }
    }
    details.metadata.transfer_loader_mode=details.effective;details.metadata.valid|=PROGRAM_TRANSFER_MODE;
    details.ultrafast=mzf_loader_is_ul_active();
    details.transfer_bytes=declared_length;
    if(!file_format_is_record_container(details.format) && !mzf_loader_is_active())
        details.transfer_bytes+=file_size-payload_end;
    begin_stage(HEADER);
    /* Reuse duration only within the same validated open source session. */
    bool cached=false;
    for(unsigned i=0;i<8;++i)if(duration_cache[i].record==details.record && duration_cache[i].mode==details.effective){
        details.total_ticks=duration_cache[i].ticks;cached=true;break;
    }
    details.duration_cached=cached;
    details.duration_deferred=!cached && !measure;
    if(!cached && measure){
        unsigned poll_budget=0;
        for(;;){
            uint16_t high,low;
            for(;;){
                step_t before=step;
                if(!cycle(&high,&low))break;
                details.total_ticks+=(uint32_t)high+low;
                /* Identical leader/mark cycles need no per-pulse dry run.
                 * Leave the final cycle to the real FSM (including trailer). */
                if(step==before && loops>1 && (step==LEADER || step==MARK_LONG || step==MARK_SHORT || step==MARK_FINAL || step==TRAILING)){
                    details.total_ticks+=(uint64_t)(loops-1)*((uint32_t)high+low);loops=1;
                }
                if(++poll_budget==1024){poll_budget=0;if(cancellable && analysis_poll(NULL))goto bad;}
            }
            if(error)goto bad;
            if(sharp_stream_ul_ready() || stage==FINISHED)break;
            if(!sharp_stream_advance())goto bad;
        }
        duration_cache[duration_next].record=details.record;
        duration_cache[duration_next].mode=details.effective;
        duration_cache[duration_next].ticks=details.total_ticks;
        duration_next=(duration_next+1)%8;
    }
    pending_high_ticks=0;
    begin_stage(HEADER);
    if(info) *info=details;
    return true;
bad: sharp_stream_close();
    return fail();
}
static bool open_source(const char *path,uint16_t record,loader_mode_t requested,sharp_stream_info_t *info,bool measure) {
    analysis_cancelled=false;
    if(!path || !record || strlen(path)>=sizeof(open_path) || (unsigned)requested>=LOADER_MODE_COUNT)return fail();
    bool reuse=fp && !error && !strcmp(open_path,path);
    if(!reuse)sharp_stream_close();
    memset(&details,0,sizeof(details));
    if(info)memset(info,0,sizeof(*info));
    details.format=file_format_detect_from_name(path);
    details.record=record;details.requested=requested;details.source_reused=reuse;
    if(!file_format_is_sharp_tape(details.format))goto bad;
    if(!reuse){
        fp=cmt_fopen(path,"rb");
        if(!fp)return fail();
        long end;
        if(cmt_fseek(fp,0,SEEK_END) || (end=cmt_ftell(fp))<(details.format==FILE_FORMAT_MZQ ? 8:128) || (uint64_t)end>UINT32_MAX)goto bad;
        file_size=(uint32_t)end;
        qd_source=details.format==FILE_FORMAT_MZQ;
        if(qd_source && !quickdisk_open(fp,file_size,analysis_poll,NULL))goto bad;
        snprintf(open_path,sizeof(open_path),"%s",path);
    }
    if(qd_source){
        if(!quickdisk_select(record,header,&declared_length))goto bad;
        details.records=quickdisk_count();payload_start=0;payload_end=declared_length;qd_position=0;
    }else if(!locate(record))goto bad;
    return prepare_record(path,requested,info,true,measure);
bad:
    sharp_stream_close();return fail();
}
bool sharp_stream_open(const char *path,uint16_t record,loader_mode_t requested,sharp_stream_info_t *info){return open_source(path,record,requested,info,true);}
bool sharp_stream_preview(const char *path,uint16_t record,loader_mode_t requested,sharp_stream_info_t *info){return open_source(path,record,requested,info,false);}
/* Advance an already validated MZT/QD session without reopening or rescanning. */
bool sharp_stream_continue_record(const char *path,loader_mode_t requested,sharp_stream_info_t *info) {
    if(!fp || error || stage!=FINISHED || !path || strcmp(path,open_path) ||
       !file_format_is_record_container(details.format) || details.record>=details.records ||
       (unsigned)requested>=LOADER_MODE_COUNT) return false;
    file_format_t format=details.format;
    uint16_t record=details.record+1,records=details.records;
    if(qd_source){
        if(!quickdisk_select(record,header,&declared_length))return fail();
        payload_start=0;payload_end=declared_length;qd_position=0;
    } else {
        uint32_t position=payload_end;
        if(position>file_size || file_size-position<128 || cmt_fseek(fp,(long)position,SEEK_SET) ||
           cmt_fread(header,1,128,fp)!=128)return fail();
        declared_length=le16(header+18);payload_start=position+128;
        if(declared_length>file_size-payload_start)return fail();
        payload_end=payload_start+declared_length;
    }
    memset(&details,0,sizeof(details));
    details.format=format;details.record=record;details.records=records;
    details.requested=requested;details.source_reused=true;
    mzf_loader_reset();
    return prepare_record(path,requested,info,false,true);
}
bool sharp_stream_begin_ul(uint64_t now_us) {
    if(!sharp_stream_ul_ready() || !mzf_loader_begin(now_us)) return fail();
    stage=UL;
    return true;
}
bool sharp_stream_service_ul(uint64_t now_us,uint8_t write_level) {
    if(stage!=UL || !mzf_loader_bus_step(now_us,write_level)) return fail();
    if(mzf_loader_is_finished()) stage=FINISHED;
    return true;
}
uint8_t sharp_stream_bus_read(void) { return mzf_loader_bus_read();
    }
uint8_t sharp_stream_bus_sense(void) { return mzf_loader_bus_sense();
    }
uint8_t sharp_stream_ul_progress(void) { return mzf_loader_get_progress_percent();
    }
bool sharp_stream_is_eof(void) { return fp && !error && stage==FINISHED;
    }
bool sharp_stream_has_error(void) { return error;
    }

uint32_t sharp_stream_ul_bytes(void) { return mzf_loader_get_transferred();
    }

void sharp_stream_pause_ul(uint64_t now_us,bool pause) { if(stage==UL) mzf_loader_bus_pause(now_us,pause); }

const char *sharp_stream_error(void){return mzf_loader_get_error_text();}
