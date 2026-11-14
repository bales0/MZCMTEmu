/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from bales0/MZ-SD2CMT2-Reborn, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * ESP32-S3: native constant data and foreground algorithms; no AVR peripherals.
 */
/* Compatibility API backed by the native Sharp codec. It is the sole file
 * owner for the selected Sharp record; UL takes GPIO ownership only after TX drains. */
#include "audio/mzf_stream.h"
#include "audio/sharp_stream.h"
#include "play/mzf_loader.h"
#include "formats/mz_tape_profiles.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>

#define UL_REFERENCE_PUMP_BYTES 64U
#ifndef CMT_UL_TRACE
#define CMT_UL_TRACE 0
#endif

static loader_mode_t requested=LOADER_MODE_NORMAL_1_1;
static sharp_stream_info_t current;
static uint64_t emitted_ticks,emitted_us;
static bool ul_active, opened, local_error, prepared;
static const char *local_error_text;
static uint32_t ul_sent,ul_write_edges,ul_clock_windows,ul_low_max_us;
static uint8_t ul_last_write;
static int64_t ul_started,ul_low_started,ul_data_started;
static bool ul_low_pending;
#if CMT_UL_TRACE
/* Optional slow diagnostic journal; disabled in normal-speed builds. */
typedef struct {
    uint32_t us,sent;
    const char *phase;
    int8_t expected;
    uint8_t pair,write,motor,read_cmd,sense_cmd,read_pad,sense_pad;
} ul_trace_t;
#define UL_TRACE_COUNT 32U
static ul_trace_t ul_trace[UL_TRACE_COUNT];
static unsigned ul_trace_head,ul_trace_used;
static void trace_bus(mzf_loader_bus_debug_t state,uint8_t write,uint8_t read,uint8_t sense,bool force) {
    ul_trace_t e={.us=(uint32_t)(esp_timer_get_time()-ul_started),.sent=mzf_loader_get_transferred(),
        .phase=state.phase,.expected=state.expected_write,.pair=state.pair,.write=write,
        .motor=(uint8_t)gpio_get_level(GPIO_NUM_6),.read_cmd=read,.sense_cmd=sense,
        .read_pad=(uint8_t)gpio_get_level(GPIO_NUM_4),.sense_pad=(uint8_t)gpio_get_level(GPIO_NUM_7)};
    if(ul_trace_used && !force){
        const ul_trace_t *last=&ul_trace[(ul_trace_head+UL_TRACE_COUNT-1)%UL_TRACE_COUNT];
        if(e.sent==last->sent && e.phase==last->phase && e.pair==last->pair && e.write==last->write &&
           e.motor==last->motor && e.read_cmd==last->read_cmd && e.sense_cmd==last->sense_cmd &&
           e.read_pad==last->read_pad && e.sense_pad==last->sense_pad)return;
    }
    ul_trace[ul_trace_head]=e;ul_trace_head=(ul_trace_head+1)%UL_TRACE_COUNT;
    if(ul_trace_used<UL_TRACE_COUNT)++ul_trace_used;
}
static void trace_dump(void) {
    printf("ul: TRACE MCU pad inputs, not measured at Sharp/8255; chronological last %u events\n",ul_trace_used);
    for(unsigned i=0;i<ul_trace_used;++i){
        const ul_trace_t *e=&ul_trace[(ul_trace_head+UL_TRACE_COUNT-ul_trace_used+i)%UL_TRACE_COUNT];
        printf("ul: edge us=%lu sent=%lu pair=%u phase=%s expect=%d W5=%u M6=%u R4=%u/%u S7=%u/%u (cmd/pad)\n",
            (unsigned long)e->us,(unsigned long)e->sent,(unsigned)e->pair,e->phase,(int)e->expected,
            (unsigned)e->write,(unsigned)e->motor,(unsigned)e->read_cmd,(unsigned)e->read_pad,
            (unsigned)e->sense_cmd,(unsigned)e->sense_pad);
    }
}
#else
#define trace_bus(...) ((void)0)
static void trace_dump(void) { puts("ul: TRACE disabled"); }
#endif
static uint16_t selected_record=1;
static loader_mode_t selected_mode;
static char selected_path[1024];
static void release_bus(void);

bool mzf_stream_set_loader_mode(loader_mode_t mode) {
    if((unsigned)mode>=LOADER_MODE_COUNT) return false;
    requested=mode;
    return true;
}
loader_mode_t mzf_stream_get_loader_mode(void) { return requested;
    }
const sharp_stream_info_t *mzf_stream_details(void) { return &current;
    }
bool mzf_stream_select_mode(const char *path,uint16_t record,loader_mode_t mode) {
    /* STOP may reselect an interrupted UL record without closing its source. */
    release_bus();
    sharp_stream_info_t info;
    if(!sharp_stream_preview(path,record,mode,&info)){prepared=false;opened=false;local_error=true;return false;}
    prepared=true;
    opened=false;
    local_error=false;local_error_text=NULL;
    current=info;
    selected_record=record;
    selected_mode=mode;
    snprintf(selected_path,sizeof(selected_path),"%s",path);
    return true;
}
bool mzf_stream_open(const char *path,mzf_stream_info_t *info) {
    bool reuse=prepared && path && !strcmp(path,selected_path);
    if(!reuse)mzf_stream_close();
    uint16_t record=path && !strcmp(path,selected_path) ? selected_record : 1;
    loader_mode_t mode=path && !strcmp(path,selected_path) ? selected_mode : requested;
    /* MZF starts from a bounded header/loader preview, with no waveform dry run. */
    bool fast=file_format_detect_from_name(path)==FILE_FORMAT_MZF;
    if((!reuse || current.duration_deferred) &&
       !(fast ? sharp_stream_preview(path,record,mode,&current):sharp_stream_open(path,record,mode,&current))) { local_error=true;
        return false;
        }
    prepared=false;
    opened=true;
    ul_sent=0;
    ul_started=0;
    if(info) {
        uint64_t us=(current.total_ticks+8)/16;
        info->sample_rate=1000000;
        info->total_seconds=(uint32_t)(us/1000000);
        info->total_samples=us>UINT32_MAX ? UINT32_MAX : (uint32_t)us;
        info->baud_rate=1200;
    }
    return true;
}
bool mzf_stream_continue_record(const char *path,loader_mode_t mode,mzf_stream_info_t *info) {
    if(!opened || ul_active || !sharp_stream_continue_record(path,mode,&current)) { local_error=true;return false; }
    selected_record=current.record;selected_mode=mode;snprintf(selected_path,sizeof(selected_path),"%s",path);
    emitted_ticks=emitted_us=0;ul_sent=0;ul_started=0;
    uint64_t us=(current.total_ticks+8)/16;
    if(info) { info->sample_rate=1000000;info->total_seconds=(uint32_t)(us/1000000);
        info->total_samples=us>UINT32_MAX ? UINT32_MAX:(uint32_t)us;info->baud_rate=1200; }
    return true;
}
static void release_bus(void) {
    if(ul_active) { gpio_set_level(GPIO_NUM_4,0);
        gpio_set_level(GPIO_NUM_7,1);
        gpio_set_level(GPIO_NUM_41,0);
        }
    ul_active=false;ul_low_pending=false;
}
void mzf_stream_close(void) {
    release_bus();
    sharp_stream_close();
    prepared=false;
    opened=false;
    local_error=false;local_error_text=NULL;
    emitted_ticks=emitted_us=0;
}
bool mzf_stream_next_ticks16(uint8_t *level,uint32_t *ticks) {
    bool count;
    if(!level || !ticks || !opened || !sharp_stream_next_pulse(level,ticks,&count))return false;
    emitted_ticks+=*ticks;emitted_us=(emitted_ticks+8)/16;return true;
}
bool mzf_stream_next_pulse(uint8_t *level,uint32_t *duration_us) {
    uint32_t ticks;uint64_t before=emitted_us;
    if(!duration_us || !mzf_stream_next_ticks16(level,&ticks))return false;
    *duration_us=(uint32_t)(emitted_us-before);return true;
}
bool mzf_stream_at_boundary(void) { return sharp_stream_at_boundary();
    }
uint32_t mzf_stream_boundary_delay_us(bool motor_low_seen) { return sharp_stream_boundary_delay_us(motor_low_seen);
    }
bool mzf_stream_advance(void) { return sharp_stream_advance();
    }
bool mzf_stream_is_eof(void) { return opened && sharp_stream_is_eof();
    }
bool mzf_stream_has_error(void) { return local_error || sharp_stream_has_error();
    }
uint32_t mzf_stream_get_samples_emitted(void) { return emitted_us>UINT32_MAX ? UINT32_MAX : (uint32_t)emitted_us;
    }
void mzf_stream_set_ultrafast_enabled(bool enabled) { requested=enabled ? LOADER_MODE_UL : LOADER_MODE_NORMAL_1_1;
    }
bool mzf_stream_get_ultrafast_enabled(void) { return requested==LOADER_MODE_UL || requested==LOADER_MODE_UL_MZ800 || requested==LOADER_MODE_UL_MZ700;
    }
bool mzf_stream_is_ultrafast_ready(void) { return opened && sharp_stream_ul_ready();
    }
bool mzf_stream_is_ultrafast_active(void) { return ul_active;
    }
static const char *ul_receiver_placement(void) {
    switch(mzf_loader_get_variant()){
    case MZF_LOADER_VARIANT_HIGH:case MZF_LOADER_VARIANT_MZ700_UL_HIGH:return "HIGH";
    case MZF_LOADER_VARIANT_MZ800_HEADER:return mzf_loader_is_mz800_header_high() ? "HIGH":"LOW";
    case MZF_LOADER_VARIANT_LOW:case MZF_LOADER_VARIANT_MZ700_UL_LOW:return "LOW";
    default:return "OTHER";
    }
}
bool mzf_stream_begin_ultrafast(void) {
    if(!mzf_stream_is_ultrafast_ready()) return false;
    gpio_config_t cfg={.pin_bit_mask=1ULL<<5,.mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    if(gpio_config(&cfg)!=0 || gpio_set_direction(GPIO_NUM_4,GPIO_MODE_INPUT_OUTPUT)!=0 ||
        gpio_set_direction(GPIO_NUM_7,GPIO_MODE_INPUT_OUTPUT)!=0 || !sharp_stream_begin_ul((uint64_t)esp_timer_get_time())) {
        local_error=true;
        gpio_set_level(GPIO_NUM_4,0);
        gpio_set_level(GPIO_NUM_7,1);
        return false;
    }
    ul_active=true;
    ul_started=esp_timer_get_time();
    ul_low_pending=false;
    ul_data_started=0;
#if CMT_UL_TRACE
    ul_trace_head=ul_trace_used=0;
#endif
    gpio_set_level(GPIO_NUM_41,0);
    /* Match reference startup: release SENSE before presenting READ ready. */
    gpio_set_level(GPIO_NUM_7,sharp_stream_bus_sense());
    gpio_set_level(GPIO_NUM_4,sharp_stream_bus_read());
    ul_write_edges=ul_clock_windows=ul_low_max_us=0;
    ul_last_write=(uint8_t)gpio_get_level(GPIO_NUM_5);
    printf("ul: BEGIN mode=%s WRITE5=%u bytes=%lu sequence=SD2CMT2 trace=%u start_delay_ms=%u ready_delay_us=%u initial_wait_ms=%u data_wait_ms=%u load=%04X exec=%04X receiver=%s\n",loader_mode_name(current.effective),(unsigned)gpio_get_level(GPIO_NUM_5),(unsigned long)current.payload_bytes,(unsigned)CMT_UL_TRACE,current.effective==LOADER_MODE_UL_MZ800 ? 700U:current.effective==LOADER_MODE_UL_MZ700 ? 0U:100U,current.effective==LOADER_MODE_UL_MZ800 ? 1000U:0U,current.effective==LOADER_MODE_UL ? 0U:5000U,current.effective==LOADER_MODE_UL_MZ700 ? 1000U:500U,(unsigned)current.metadata.load,(unsigned)current.metadata.exec,ul_receiver_placement());
    return true;
}
static uint8_t sample_write(void) {
    uint8_t value=(uint8_t)gpio_get_level(GPIO_NUM_5);
    if(value!=ul_last_write){++ul_write_edges;ul_last_write=value;}
    return value;
}
static void ul_fail(const char *why,mzf_loader_bus_debug_t before,uint8_t write,
                    uint8_t read,uint8_t sense,bool record) {
    local_error=true;local_error_text=why;
    if(ul_low_pending){
        uint32_t elapsed=(uint32_t)(esp_timer_get_time()-ul_low_started);
        if(elapsed>ul_low_max_us)ul_low_max_us=elapsed;
    }
    if(record)trace_bus(before,write,read,sense,true);
    release_bus();
    printf("ul: FAIL reason=%s phase=%s expect_WRITE=%d WRITE5=%u pair=%u READ4_CMD=%u SENSE7_CMD=%u write_edges=%lu sent=%lu/%lu elapsed_us=%llu clock_windows=%lu low_cmd_max_us=%lu\n",
        why,before.phase,(int)before.expected_write,(unsigned)write,(unsigned)before.pair,
        (unsigned)read,(unsigned)sense,(unsigned long)ul_write_edges,(unsigned long)mzf_loader_get_transferred(),
        (unsigned long)current.payload_bytes,(unsigned long long)(esp_timer_get_time()-ul_started),
        (unsigned long)ul_clock_windows,(unsigned long)ul_low_max_us);
    trace_dump();
}
/* ESP foreground adapter for the SD2CMT2 mzf_loader_pump sequence.
 * Startup remains nonblocking (UL800: 700 ms READ LOW, then 1 ms READ HIGH,
 * then up to 5 s for initial WRITE LOW). Data waits use the reference timeout.
 * Finish a complete byte before returning to the RTOS; no IRQ masking, extra
 * clock holds, short ACK deadline, or task delay between its four bit pairs. */
void mzf_stream_service_ultrafast(void) {
    int64_t until=esp_timer_get_time()+1500;
    uint32_t first_sent=sharp_stream_ul_bytes();
    while(ul_active) {
        uint32_t before_sent=sharp_stream_ul_bytes();
        uint8_t write_level=sample_write();
        uint8_t read_cmd=sharp_stream_bus_read(),sense_cmd=sharp_stream_bus_sense();
        trace_bus(mzf_loader_bus_debug(),write_level,read_cmd,sense_cmd,false);
        if(!sharp_stream_service_ul((uint64_t)esp_timer_get_time(),write_level)) {
            ul_fail(sharp_stream_error(),mzf_loader_bus_debug(),write_level,read_cmd,sense_cmd,true);
            return;
        }
        uint8_t next_read=sharp_stream_bus_read(),next_sense=sharp_stream_bus_sense();
        if(next_read!=read_cmd || next_sense!=sense_cmd){
            /* Same order and physical levels as reference read_set/sense_set. */
            gpio_set_level(GPIO_NUM_4,next_read);
            gpio_set_level(GPIO_NUM_7,next_sense);
            if(next_sense!=sense_cmd){
                if(!next_sense){
                    ul_low_started=esp_timer_get_time();
                    if(!ul_data_started)ul_data_started=ul_low_started;
                    ul_low_pending=true;++ul_clock_windows;
                }
                else if(ul_low_pending){
                    uint32_t elapsed=(uint32_t)(esp_timer_get_time()-ul_low_started);
                    if(elapsed>ul_low_max_us)ul_low_max_us=elapsed;
                    ul_low_pending=false;
                }
            }
        }
        ul_sent=sharp_stream_ul_bytes();
        if(sharp_stream_is_eof()) {
            release_bus();
            int64_t ended=esp_timer_get_time();
            uint64_t data_us=ul_data_started ? (uint64_t)(ended-ul_data_started):0;
            uint32_t bytes_per_s=data_us ? (uint32_t)((uint64_t)ul_sent*1000000ULL/data_us):0;
            printf("ul: END sent=%lu/%lu clock_windows=%lu low_cmd_max_us=%lu elapsed_ms=%llu data_ms=%llu bytes_per_s=%lu\n",
                (unsigned long)ul_sent,(unsigned long)current.payload_bytes,(unsigned long)ul_clock_windows,(unsigned long)ul_low_max_us,
                (unsigned long long)((ended-ul_started)/1000),(unsigned long long)(data_us/1000),(unsigned long)bytes_per_s);
            return;
        }
        if(!mzf_loader_bus_data_phase())return; /* ROM/startup wait, not a data half-byte. */
        if(ul_sent!=before_sent && ul_sent<current.payload_bytes &&
           (ul_sent-first_sent>=UL_REFERENCE_PUMP_BYTES || esp_timer_get_time()>=until))return;
    }
}
uint32_t mzf_stream_get_ultrafast_total_bytes(void) { return current.ultrafast ? current.payload_bytes : 0;
    }
uint32_t mzf_stream_get_ultrafast_sent_bytes(void) { return current.ultrafast ? ul_sent : 0;
    }
uint32_t mzf_stream_get_baud_rate(void) {
    if(ul_started && ul_sent) { int64_t dt=esp_timer_get_time()-ul_started;
        if(dt>0) return (uint32_t)((uint64_t)ul_sent*8000000ULL/(uint64_t)dt);
        }
    switch(current.effective) {
    case LOADER_MODE_NORMAL_1_2: case LOADER_MODE_IC_1_2: case LOADER_MODE_TC_1_2: return 2400;
    case LOADER_MODE_NORMAL_1_3: case LOADER_MODE_IC_1_3: case LOADER_MODE_TC_1_3: case LOADER_MODE_MZ700_3X: return 3600;
    case LOADER_MODE_NORMAL_1_4: case LOADER_MODE_IC_1_4: return 4800;
    default:return 1200;
    }
}

void mzf_stream_clear_selection(void) {
    if(prepared){sharp_stream_close();prepared=false;}
    selected_path[0]=0;
    selected_record=1;
    }
bool mzf_stream_select(const char *path,uint16_t record) { return mzf_stream_select_mode(path,record,requested);
    }

void mzf_stream_pause_ul(bool pause) { if(ul_active) sharp_stream_pause_ul((uint64_t)esp_timer_get_time(),pause); }

const char *mzf_stream_error(void){return local_error_text ? local_error_text:sharp_stream_error();}

bool mzf_stream_last_pulse_source_byte(void){return sharp_stream_last_pulse_source_byte();}
uint32_t mzf_stream_transfer_bytes(void){return current.transfer_bytes;}
