#include "storage/fs.h"
/* SPDX-License-Identifier: GPL-3.0-only
 * MZCMTEmu ESP32-S3 streaming TAP implementation, 2026-11-12.
 * Protocol state sequence and timings ported from bales0/SD2CMT2-Reborn:
 * src/play/tap_playback.cpp, tap_speed.cpp at 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * Original project: https://github.com/bales0/MZ-SD2CMT2-Reborn (GPLv3).
 * AVR timer/ISR/SD drivers are replaced with a bounded foreground pulse source.
 */
#include "audio/tap_stream.h"
#include "formats/program_metadata.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

typedef struct {
    uint16_t pilot, sync_low, sync_high, one, zero;
} timing_t;
static const timing_t timings[TAP_SPEED_COUNT] = {
    {9798,2321,2979,7749,3874}, {7621,1805,2317,6027,3013},
    {6532,1547,1986,5166,2583}, {5487,1300,1668,4340,2170}
};
static const uint16_t bauds[TAP_SPEED_COUNT] = {1400,1800,2100,2500};
typedef enum { IDLE, PILOT, SYNC_LOW, SYNC_HIGH, DATA, TERMINATOR, GAP, NEXT, END } phase_t;
static cmt_file_t *fp;
static timing_t timing;
static uint32_t file_size, block_end;
static uint16_t remaining, pilot_halves;
static uint8_t flag, current_byte, mask, half;
static uint32_t gap_ticks;
static phase_t phase;
static program_metadata_t metadata,pair_header;
static program_metadata_store_t metadata_store;
static bool pair_pending;static uint32_t block_number,total_blocks,metadata_revision;
static uint8_t block_xor;static uint16_t block_length;
static void publish_metadata(void){program_metadata_publish(&metadata_store,&metadata);++metadata_revision;}
void tap_stream_metadata(program_metadata_t *out){program_metadata_snapshot(&metadata_store,out);}
uint32_t tap_stream_metadata_revision(void){return metadata_revision;}
static bool error;
static bool cancelled;
static tap_stream_cancel_fn scan_cancel;static void *scan_context;
void tap_stream_set_scan_control(tap_stream_cancel_fn fn,void *context) {scan_cancel=fn;scan_context=context;}
bool tap_stream_analysis_cancelled(void) {return cancelled;}
static bool analysis_cancel(void) {if(scan_cancel && scan_cancel(scan_context)){cancelled=true;return true;}return false;}

uint16_t tap_stream_speed_baud(tap_speed_t speed)
{
    return (unsigned)speed < TAP_SPEED_COUNT ? bauds[speed] : 0;
}
bool tap_stream_is_tap_path(const char *path)
{
    if (!path) return false;
    const char *ext = strrchr(path,'.');
    return ext && strcasecmp(ext,".tap") == 0;
}
static bool fail(void) { error = true; phase = IDLE; return false; }
static uint16_t pilot_cycles(uint8_t block_flag) { return block_flag == 0 ? 4032 : 1610; }
static uint32_t block_gap(uint8_t block_flag) { return block_flag == 0 ? 5600000U : 16000000U; }

/* Called both in the validation pass and when entering a playback block.
 * Subtraction-based bounds keep untrusted length fields away from overflow. */
static bool read_block_header(uint32_t position, uint16_t *length, uint8_t *block_flag)
{
    uint8_t bytes[3];
    if (position > file_size || file_size-position < 4U ||
        cmt_fread(bytes,1,sizeof(bytes),fp) != sizeof(bytes)) return fail();
    *length = (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1]<<8);
    *block_flag = bytes[2];
    if (*length < 2U || (uint32_t)*length > file_size-position-2U ||
        (*block_flag != 0 && *block_flag != 255)) return fail();
    return true;
}
static unsigned popcount(uint8_t value)
{
    unsigned count=0;
    for (; value; value &= (uint8_t)(value-1)) ++count;
    return count;
}
static uint32_t byte_ticks(uint8_t value)
{
    unsigned ones = popcount(value);
    return 2U * (ones*timing.one + (8U-ones)*timing.zero);
}
static bool scan(tap_stream_info_t *info)
{
    uint32_t pos=0;
    uint8_t buffer[256];
    while (pos < file_size) {
        if(analysis_cancel())return fail();
        uint16_t length;
        uint8_t block_flag;
        if (!read_block_header(pos,&length,&block_flag)) return false;
        info->total_ticks += (uint64_t)pilot_cycles(block_flag)*2U*timing.pilot +
                             timing.sync_low + timing.sync_high + timing.pilot;
        info->total_ticks += byte_ticks(block_flag);
        uint32_t left = (uint32_t)length-1U;
        while (left) {
            if(analysis_cancel())return fail();
            size_t n = left < sizeof(buffer) ? left : sizeof(buffer);
            if (cmt_fread(buffer,1,n,fp) != n) return fail();
            for (size_t i=0;i<n;++i) info->total_ticks += byte_ticks(buffer[i]);
            left -= (uint32_t)n;
        }
        pos += 2U + length;
        if (pos < file_size) info->total_ticks += block_gap(block_flag);
        ++info->block_count;
    }
    return pos == file_size && cmt_fseek(fp,0,SEEK_SET) == 0 ? true : fail();
}
static bool begin_block(void)
{
    long position = cmt_ftell(fp);
    uint16_t length;
    if (position < 0 || !read_block_header((uint32_t)position,&length,&flag)) return fail();
    block_end = (uint32_t)position+2U+length;
    ++block_number;block_length=length;block_xor=flag;
    program_metadata_reset(&metadata,FILE_FORMAT_TAP);metadata.confidence=PROGRAM_UNKNOWN;
    if(flag==0 && length==19) {
        uint8_t h[19];h[0]=flag;long restore=cmt_ftell(fp);
        if(cmt_fread(h+1,1,18,fp)!=18 || cmt_fseek(fp,restore,SEEK_SET))return fail();
        pair_pending=program_metadata_zx(&metadata,h);
        if(pair_pending)pair_header=metadata;else metadata.confidence=PROGRAM_INVALID;
    } else if(flag==255 && pair_pending && pair_header.declared_size==(uint32_t)length-2) {
        metadata=pair_header;pair_pending=false;
    } else pair_pending=false;
    metadata.record=block_number;metadata.records=total_blocks;publish_metadata();
    remaining=length; current_byte=flag; mask=0x80; half=0;
    pilot_halves=(uint16_t)(pilot_cycles(flag)*2U);
    phase=PILOT;
    return true;
}
void tap_stream_close(void)
{
    if (fp) cmt_fclose(fp);
    fp=NULL; phase=IDLE; error=false; file_size=0; block_end=0;
}
bool tap_stream_open(const char *path, tap_speed_t speed, tap_stream_info_t *info)
{
    tap_stream_close();
    cancelled=false;
    if (info) memset(info,0,sizeof(*info));
    if (!path || (unsigned)speed >= TAP_SPEED_COUNT) return fail();
    fp=cmt_fopen(path,"rb");
    if (!fp) return fail();
    timing=timings[speed];
    long end;
    tap_stream_info_t result={0};
    if (cmt_fseek(fp,0,SEEK_END) || (end=cmt_ftell(fp)) <= 0 ||
        (uint64_t)end > UINT32_MAX || cmt_fseek(fp,0,SEEK_SET)) goto bad;
    file_size=(uint32_t)end;
    result.total_bytes=file_size; result.baud=bauds[speed];
    if (!scan(&result))goto bad;
    block_number=0;total_blocks=result.block_count;pair_pending=false;
    if(!begin_block())goto bad;
    if (info) *info=result;
    return true;
bad:
    tap_stream_close();
    return fail();
}
bool tap_stream_next_pulse(uint8_t *level, uint32_t *ticks, bool *count_pulse)
{
    if (!fp || error || !level || !ticks || !count_pulse || phase == END) return false;
    *count_pulse=false;
    if (phase == NEXT && !begin_block()) return false;
    switch (phase) {
        case PILOT:
            *level=(uint8_t)((pilot_halves & 1U) == 0 ? 0 : 1);
            *ticks=timing.pilot; *count_pulse=(*level != 0);
            if (--pilot_halves == 0) phase=SYNC_LOW;
            return true;
        case SYNC_LOW:
            *level=0; *ticks=timing.sync_low; phase=SYNC_HIGH; return true;
        case SYNC_HIGH:
            *level=1; *ticks=timing.sync_high; *count_pulse=true; phase=DATA; return true;
        case DATA:
            *level=half; *ticks=(current_byte & mask) ? timing.one : timing.zero;
            *count_pulse=(half != 0);
            if (half == 0) half=1;
            else {
                half=0; mask >>= 1;
                if (!mask) {
                    if (--remaining == 0) phase=TERMINATOR;
                    else {
                        int value=cmt_fgetc(fp);
                        if (value == EOF) return fail();
                        current_byte=(uint8_t)value;block_xor^=current_byte;mask=0x80;
                    }
                }
            }
            return true;
        case TERMINATOR:
            if(flag==255 && metadata.protocol==PROGRAM_SPECTRUM) {
                metadata.received_bytes=block_length-2;metadata.confidence=block_xor ? PROGRAM_INVALID:PROGRAM_PAYLOAD_VALID;publish_metadata();
            }
            *level=0; *ticks=timing.pilot;
            if (block_end == file_size) phase=END;
            else { gap_ticks=block_gap(flag); phase=GAP; }
            return true;
        case GAP:
            *level=0; *ticks=gap_ticks; phase=NEXT; return true;
        default: return fail();
    }
}
bool tap_stream_is_eof(void) { return fp && !error && phase == END; }
bool tap_stream_has_error(void) { return error; }
