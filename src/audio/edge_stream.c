#include "storage/fs.h"
#include "audio/edge_stream.h"

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
static bool scan_duration(uint64_t *duration){
    uint8_t block[1024];uint32_t done=0;uint64_t units=0;
    *duration=0;
    while(done<s_total_bytes){
        edge_scan_action_t action=scan_control ? scan_control(done,s_total_bytes,scan_context):EDGE_SCAN_CONTINUE;
        if(action==EDGE_SCAN_CANCEL){scan_cancelled=true;return false;}
        if(action==EDGE_SCAN_SKIP)return cmt_fseek(s_fp,0,SEEK_SET)==0;
        size_t n=s_total_bytes-done;if(n>sizeof(block))n=sizeof(block);
        if(cmt_fread(block,1,n,s_fp)!=n)return false;
        for(size_t i=0;i<n;++i){int value=(int8_t)block[i];units+=value==0 ? EDGE_ZERO_UNITS:value<0 ? (unsigned)-value:(unsigned)value;}
        done+=(uint32_t)n;
    }
    if(scan_control && scan_control(done,s_total_bytes,scan_context)==EDGE_SCAN_CANCEL){scan_cancelled=true;return false;}
    if(cmt_fseek(s_fp,0,SEEK_SET))return false;
    *duration=units*(s_is_l16 ? 16U:50U)+EDGE_IDLE_PREROLL_US+EDGE_IDLE_TAIL_US;return true;
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

bool edge_stream_open(const char *path, edge_stream_info_t *info)
{
    bool is_l16;
    uint32_t sample_rate;

    edge_stream_close();
    scan_cancelled=false;
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
    if(!scan_duration(&duration)){edge_stream_close();return false;}
    if (info != NULL) {
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
