/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from bales0/MZ-SD2CMT2-Reborn, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * ESP32-S3: native constant data and foreground algorithms; no AVR peripherals.
 */
/* Compatibility API backed by the native Sharp codec. It is the sole file
 * owner for the selected Sharp record; UL takes GPIO ownership only after TX drains. */
#include "audio/mzf_stream.h"
#include "audio/sharp_stream.h"
#include "formats/mz_tape_profiles.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>

static loader_mode_t requested=LOADER_MODE_NORMAL_1_1;
static sharp_stream_info_t current;
static uint64_t emitted_ticks,emitted_us;
static bool ul_active, opened, local_error;
static uint32_t ul_sent;
static int64_t ul_started;
static uint16_t selected_record=1;
static loader_mode_t selected_mode;
static char selected_path[256];

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
    sharp_stream_info_t info;
    if(!sharp_stream_open(path,record,mode,&info)) return false;
    sharp_stream_close();
    current=info;
    selected_record=record;
    selected_mode=mode;
    snprintf(selected_path,sizeof(selected_path),"%s",path);
    return true;
}
bool mzf_stream_open(const char *path,mzf_stream_info_t *info) {
    mzf_stream_close();
    uint16_t record=path && !strcmp(path,selected_path) ? selected_record : 1;
    loader_mode_t mode=path && !strcmp(path,selected_path) ? selected_mode : requested;
    if(!sharp_stream_open(path,record,mode,&current)) { local_error=true;
        return false;
        }
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
static void release_bus(void) {
    if(ul_active) { gpio_set_level(GPIO_NUM_4,0);
        gpio_set_level(GPIO_NUM_7,1);
        gpio_set_level(GPIO_NUM_41,0);
        }
    ul_active=false;
}
void mzf_stream_close(void) {
    release_bus();
    sharp_stream_close();
    opened=false;
    local_error=false;
    emitted_ticks=emitted_us=0;
}
bool mzf_stream_next_pulse(uint8_t *level,uint32_t *duration_us) {
    uint32_t ticks;
    bool count;
    if(!level || !duration_us || !opened || !sharp_stream_next_pulse(level,&ticks,&count)) return false;
    emitted_ticks+=ticks;
    uint64_t end=(emitted_ticks+8)/16;
    *duration_us=(uint32_t)(end-emitted_us);
    emitted_us=end;
    return true;
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
bool mzf_stream_begin_ultrafast(void) {
    if(!mzf_stream_is_ultrafast_ready()) return false;
    gpio_config_t cfg={.pin_bit_mask=1ULL<<5,.mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    if(gpio_config(&cfg)!=0 || gpio_set_direction(GPIO_NUM_4,GPIO_MODE_OUTPUT)!=0 ||
        gpio_set_direction(GPIO_NUM_7,GPIO_MODE_OUTPUT)!=0 || !sharp_stream_begin_ul((uint64_t)esp_timer_get_time())) {
        local_error=true;
        gpio_set_level(GPIO_NUM_4,0);
        gpio_set_level(GPIO_NUM_7,1);
        return false;
    }
    ul_active=true;
    ul_started=esp_timer_get_time();
    gpio_set_level(GPIO_NUM_41,0);
    gpio_set_level(GPIO_NUM_4,sharp_stream_bus_read());
    gpio_set_level(GPIO_NUM_7,sharp_stream_bus_sense());
    return true;
}
void mzf_stream_service_ultrafast(void) {
    int64_t until=esp_timer_get_time()+1500;
    for(unsigned budget=0; ul_active && budget<512 && esp_timer_get_time()<until; ++budget) {
        if(!sharp_stream_service_ul((uint64_t)esp_timer_get_time(),(uint8_t)gpio_get_level(GPIO_NUM_5))) {
            local_error=true;
            release_bus();
            return;
        }
        ul_sent=sharp_stream_ul_bytes();
        gpio_set_level(GPIO_NUM_4,sharp_stream_bus_read());
        gpio_set_level(GPIO_NUM_7,sharp_stream_bus_sense());
        if(sharp_stream_is_eof()) { release_bus();
            return;
            }
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

void mzf_stream_clear_selection(void) { selected_path[0]=0;
    selected_record=1;
    }
bool mzf_stream_select(const char *path,uint16_t record) { return mzf_stream_select_mode(path,record,requested);
    }

void mzf_stream_pause_ul(bool pause) { if(ul_active) sharp_stream_pause_ul((uint64_t)esp_timer_get_time(),pause); }
