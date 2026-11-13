#include "storage/fs.h"
/* SPDX-License-Identifier: GPL-3.0-only
 * Protocol algorithms from bales0/MZ-SD2CMT2-Reborn, firmware 2.0.3,
 * commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * Native ESP32-S3 foreground processing; no AVR peripheral code.
 */
#include "audio/mzf_writer.h"
#include "storage/record_file.h"
#include "formats/mz_tape_decoder.h"
#include "formats/mz_loader_profiles.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif

static program_metadata_t program;
static program_metadata_store_t program_store;
void mzf_writer_metadata(program_metadata_t *out){program_metadata_snapshot(&program_store,out);}
static void publish_metadata(void){program_metadata_publish(&program_store,&program);}
static cmt_file_t *file;
static char target[256];
static uint8_t *payload;
static uint8_t header[128],tc_loader[MZ_TC_LOADER_BYTES];
static uint32_t expected,received;
static bool valid_header,valid_block,recovery,tc_pending;
static mz_copier_profile_t copier;
static loader_mode_t detected;
static const char *error_text="";
static uint16_t difference_u16(uint16_t left, uint16_t right)
{
    return (left >= right) ? (uint16_t)(left - right) :
                             (uint16_t)(right - left);
}

/* The decoder learns logical HIGH/external WRITE LOW in 16 us units x8.
   Use both leader SHORT and header-mark LONG timing so L16 quantization cannot
   collapse Normal 1:3 and 1:4 to the same SHORT value.  The old SHORT-only
   rule remains as a defensive fallback if LONG mark timing is unavailable. */
static loader_mode_t profile_from_header_short_only(
    uint16_t short_high_x8, uint16_t leader_pulses)
{
    uint16_t d1 = difference_u16(short_high_x8, 120U);
    uint16_t d2 = difference_u16(short_high_x8, 57U);
    uint16_t d3 = difference_u16(short_high_x8, 44U);
    uint16_t d4 = difference_u16(short_high_x8, 39U);

    if ((leader_pulses >= 8000U) && (leader_pulses <= 13000U) &&
        (d4 < d3) && (d4 < d2) && (d4 < d1))
    {
        return LOADER_MODE_NORMAL_1_4;
    }
    if ((d2 < d1) && (d2 <= d3)) return LOADER_MODE_NORMAL_1_2;
    if ((d3 < d1) && (d3 < d2)) return LOADER_MODE_NORMAL_1_3;
    return LOADER_MODE_NORMAL_1_1;
}

static loader_mode_t profile_from_header_tone(uint16_t short_high_x8,
                                                     uint16_t long_high_x8,
                                                     uint16_t leader_pulses)
{
    uint16_t score1;
    uint16_t score2;
    uint16_t score3;
    uint16_t score4;

    if (short_high_x8 == 0U) return LOADER_MODE_NORMAL_1_1;
    if (long_high_x8 == 0U)
        return profile_from_header_short_only(short_high_x8, leader_pulses);

    score1 = (uint16_t)(difference_u16(short_high_x8, 120U) +
                        difference_u16(long_high_x8, 235U));
    score2 = (uint16_t)(difference_u16(short_high_x8, 57U) +
                        difference_u16(long_high_x8, 117U));
    score3 = (uint16_t)(difference_u16(short_high_x8, 44U) +
                        difference_u16(long_high_x8, 88U));
    score4 = (uint16_t)(difference_u16(short_high_x8, 39U) +
                        difference_u16(long_high_x8, 79U));

    if ((score4 < score3) && (score4 < score2) && (score4 < score1))
        return LOADER_MODE_NORMAL_1_4;
    if ((score2 < score1) && (score2 <= score3) && (score2 <= score4))
        return LOADER_MODE_NORMAL_1_2;
    if ((score3 < score1) && (score3 < score2) && (score3 <= score4))
        return LOADER_MODE_NORMAL_1_3;
    return LOADER_MODE_NORMAL_1_1;
}

static loader_mode_t profile_from_copier(mz_copier_profile_t profile)
{
    switch (profile)
    {
        case MZ_COPIER_IC_1_1: return LOADER_MODE_IC_1_1;
        case MZ_COPIER_IC_1_4: return LOADER_MODE_IC_1_4;
        case MZ_COPIER_IC_1_3: return LOADER_MODE_IC_1_3;
        case MZ_COPIER_IC_1_2: return LOADER_MODE_IC_1_2;
        case MZ_COPIER_TC_1_1: return LOADER_MODE_TC_1_1;
        case MZ_COPIER_TC_1_3: return LOADER_MODE_TC_1_3;
        case MZ_COPIER_TC_1_2: return LOADER_MODE_TC_1_2;
        case MZ_COPIER_NONE:
        default: return LOADER_MODE_NORMAL_1_1;
    }
}


static bool fail(const char *message) { error_text=message;program.confidence=PROGRAM_INVALID;publish_metadata();return false; }
void mzf_writer_init(void) {
    if(!payload) payload=heap_caps_malloc(65535,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
void mzf_writer_abort_delete(void) {
    if(file) { cmt_fclose(file); file=NULL; if(target[0]) cmt_remove(target); }
    mz_tape_decoder_stop();
}
bool mzf_writer_open(const char *path) {
    mzf_writer_abort_delete();
    program_metadata_reset(&program,FILE_FORMAT_MZF);publish_metadata();
    valid_header=valid_block=recovery=tc_pending=false;
    expected=received=0; copier=MZ_COPIER_NONE; detected=LOADER_MODE_NORMAL_1_1;error_text="";
    if(!payload) return fail("MZF PSRAM");
    if(!path || strlen(path)>=sizeof(target)) return fail("MZF PATH");
    snprintf(target,sizeof(target),"%s",path);
    file=record_file_create(path,sizeof(target));
    if(!file)return fail("MZF CREATE");
    mz_tape_decoder_begin_header();
    return true;
}
static void begin_payload(void) {
    expected=(uint32_t)header[18]|(uint32_t)header[19]<<8;
    received=0;valid_header=true;
    program_metadata_sharp(&program,header,copier==MZ_COPIER_NONE ? PROGRAM_TAPE_DECODE:PROGRAM_LOADER_EMBEDDED);
    program.valid|=PROGRAM_RECORDED_MODE;program.recorded_loader_mode=detected;publish_metadata();
    mz_tape_decoder_start_data(expected);
}
static bool accept_event(const mz_tape_decoder_event_t *event) {
    switch(event->type) {
    case MZ_TAPE_DECODER_EVENT_HEADER_VALID: {
        const uint8_t *source=mz_tape_decoder_get_header();
        if(valid_header || tc_pending || !source) return fail("MZF HEADER");
        if(mz_loader_profile_recognize_ic(source,header,&copier)) detected=profile_from_copier(copier);
        else if(mz_loader_profile_recognize_tc_header(source)) {
            program.confidence=PROGRAM_LOADER_ONLY;program.protocol=PROGRAM_SHARP;program.chain_stage=1;publish_metadata();
            tc_pending=true; mz_tape_decoder_start_data(MZ_TC_LOADER_BYTES); return true;
        } else {
            memcpy(header,source,128);
            detected=profile_from_header_tone(mz_tape_decoder_get_header_short_high_x8(),
                mz_tape_decoder_get_header_long_high_x8(),event->leader_pulses);
        }
        begin_payload();return true;
    }
    case MZ_TAPE_DECODER_EVENT_DATA_BYTE:
        if(tc_pending) {
            if(event->byte_index>=sizeof(tc_loader)) return fail("TC SIZE");
            tc_loader[event->byte_index]=event->value;return true;
        }
        if(!valid_header || event->byte_index>=expected) return fail("MZF SIZE");
        /* A malformed block may reacquire a complete leader. Overwrite by
         * decoder position so a partial first attempt cannot be prepended. */
        payload[event->byte_index]=event->value;received=event->byte_index+1;
        if(!expected || received*100/expected!=program.received_bytes*100/expected){program.received_bytes=received;publish_metadata();}
        return true;
    case MZ_TAPE_DECODER_EVENT_BLOCK_VALID:
        if(tc_pending) {
            if(!mz_loader_profile_normalize_tc(mz_tape_decoder_get_header(),tc_loader,header,&copier)) return fail("TC LOADER");
            detected=profile_from_copier(copier);tc_pending=false;begin_payload();return true;
        }
        if(!valid_header || received!=expected) return fail("MZF SIZE");
        valid_block=true;program.received_bytes=received;program.confidence=PROGRAM_PAYLOAD_VALID;publish_metadata();mz_tape_decoder_stop(); return true;
    case MZ_TAPE_DECODER_EVENT_BLOCK_INVALID:
        if(tc_pending) return fail("TC CHECKSUM");
        if(recovery || copier!=MZ_COPIER_NONE) return fail("MZF CHECKSUM");
        recovery=true;received=0;mz_tape_decoder_start_recovery_data(expected);return true;
    default: return true;
    }
}
bool mzf_writer_feed(uint8_t level,uint32_t duration_us) {
    if(!file || error_text[0]) return false;
    if(valid_block) return true;
    /* Same independent L16 quantization used by the reference decoder.
     * Long silence is a signal break, never a wrapped short pulse. */
    uint64_t units=((uint64_t)duration_us+8)/16;
    if(!units || units>1024) { mz_tape_decoder_break_signal();return true; }
    if(mz_tape_decoder_feed_interval((uint16_t)units,level)) {
        mz_tape_decoder_event_t event;
        while(mz_tape_decoder_take_event(&event)) if(!accept_event(&event)) return false;
    }
    return true;
}
bool mzf_writer_complete(void) { return valid_block && !error_text[0]; }
bool mzf_writer_finalize(void) {
    if(!file || !mzf_writer_complete()) { mzf_writer_abort_delete();return fail("MZF INCOMPLETE"); }
    bool ok=cmt_fwrite(header,1,128,file)==128 && cmt_fwrite(payload,1,expected,file)==expected && !cmt_fflush(file);
    if(cmt_fclose(file)) ok=false;
    file=NULL;mz_tape_decoder_stop();
    if(!ok) { cmt_remove(target);return fail("MZF WRITE"); }
    return true;
}
const char *mzf_writer_error(void) { return error_text; }
uint8_t mzf_writer_progress(void) { return !valid_header ? 0:!expected ? 100:(uint8_t)((received*100U)/expected); }
bool mzf_writer_profile(loader_mode_t *mode) { if(!mode || !valid_header) return false;*mode=detected;return true; }
const uint8_t *mzf_writer_header(void) { return valid_header ? header:NULL; }
