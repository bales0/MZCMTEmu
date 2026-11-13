#include "audio/raw_pulse.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "hal/rmt_ll.h"
#include "soc/rmt_struct.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <stdint.h>

#define OUTPUT_GPIO GPIO_NUM_4
#define FIFO_SIZE 4096U
#define FIFO_MASK (FIFO_SIZE-1U)
#define RMT_TICKS_PER_US 16U
_Static_assert(RMT_LL_GET(INST_NUM)==1,"CMT owns the complete ESP32-S3 RMT clock group");
/* One source FIFO. Encoder copies into IDF's internal DMA ping/pong buffer;
 * source slots are released conservatively after physical interval deadlines.
 * No filesystem, allocation or task waits are allowed in either callback. */
static DRAM_ATTR raw_pulse_t fifo[FIFO_SIZE];
static DRAM_ATTR portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR uint32_t head,tail,encode_cursor;
static DRAM_ATTR uint32_t native_flags[FIFO_SIZE/32];
static DRAM_ATTR uint64_t encode_remaining,encoded_ticks,retired_tx_ticks;
static DRAM_ATTR uint8_t encode_level,token;
static DRAM_ATTR volatile bool running,paused,tx_done,encode_starved,finish_requested,accept_callbacks;
static DRAM_ATTR bool read_invert,output_enabled=true,backend_enabled=true,error,underrun;
static DRAM_ATTR int64_t launch_us,pause_started,paused_us;
static DRAM_ATTR uint32_t time_seq,completed_low,completed_high,pps;
static uint64_t pps_time;
static uint32_t pps_count,legacy_tick_us=16;
static rmt_channel_handle_t channel;
static rmt_encoder_handle_t encoder;
static bool initialized,channel_enabled;

static void publish_completed(uint64_t value) {
    uint32_t seq=__atomic_load_n(&time_seq,__ATOMIC_RELAXED);
    __atomic_store_n(&time_seq,seq+1,__ATOMIC_SEQ_CST);
    __atomic_store_n(&completed_low,(uint32_t)value,__ATOMIC_RELAXED);
    __atomic_store_n(&completed_high,(uint32_t)(value>>32),__ATOMIC_RELAXED);
    __atomic_store_n(&time_seq,seq+2,__ATOMIC_RELEASE);
}
static uint64_t completed_ticks(void) {
    for(;;) {
        uint32_t before=__atomic_load_n(&time_seq,__ATOMIC_ACQUIRE);
        if(before&1)continue;
        uint32_t low=__atomic_load_n(&completed_low,__ATOMIC_RELAXED),high=__atomic_load_n(&completed_high,__ATOMIC_RELAXED);
        if(before==__atomic_load_n(&time_seq,__ATOMIC_ACQUIRE))return ((uint64_t)high<<32)|low;
    }
}
uint64_t raw_pulse_get_completed_us(void) {return (completed_ticks()+8)/16;}
static uint64_t IRAM_ATTR event_ticks(uint32_t index) {
    return (__atomic_load_n(&native_flags[index/32],__ATOMIC_RELAXED)&(1U<<(index%32))) ? fifo[index].duration_us:(uint64_t)fifo[index].duration_us*16;
}
static bool IRAM_ATTR half_symbol(uint16_t *duration,uint8_t *level) {
    if(!encode_remaining) {
        uint32_t h=__atomic_load_n(&head,__ATOMIC_ACQUIRE);
        if(encode_cursor==h)return false;
        raw_pulse_t event=fifo[encode_cursor];
        encode_remaining=event_ticks(encode_cursor);
        encode_cursor=(encode_cursor+1)&FIFO_MASK;
        encoded_ticks+=encode_remaining;
        encode_level=(uint8_t)((event.level!=0)^read_invert);
        if(!output_enabled)encode_level=0;
    }
    uint16_t n=encode_remaining>32767 ? 32767:(uint16_t)encode_remaining;
    *duration=n;*level=encode_level;encode_remaining-=n;return true;
}
static size_t IRAM_ATTR encode(const void *data,size_t size,size_t written,size_t free_symbols,
                               rmt_symbol_word_t *out,bool *done,void *context) {
    (void)data;(void)size;(void)written;(void)context;
    size_t count=0;*done=false;
    portENTER_CRITICAL_ISR(&lock);
    if(!accept_callbacks) {*done=true;portEXIT_CRITICAL_ISR(&lock);return 0;}
    while(count<free_symbols) {
        uint16_t first,second;uint8_t level0,level1;
        if(!half_symbol(&first,&level0)) {encode_starved=true;*done=true;break;}
        rmt_symbol_word_t symbol={0};symbol.duration0=first;symbol.level0=level0;
        if(!half_symbol(&second,&level1)) {
            /* A zero second duration is the documented terminal marker. */
            encode_starved=true;*done=true;out[count++]=symbol;break;
        }
        symbol.duration1=second;symbol.level1=level1;out[count++]=symbol;
    }
    portEXIT_CRITICAL_ISR(&lock);
    return count;
}
static bool IRAM_ATTR transmitted(rmt_channel_handle_t handle,const rmt_tx_done_event_data_t *event,void *context) {
    (void)handle;(void)event;(void)context;
    portENTER_CRITICAL_ISR(&lock);
    if(accept_callbacks) {
        tx_done=true;running=false;
        underrun=encode_starved && (!finish_requested || encode_cursor!=__atomic_load_n(&head,__ATOMIC_ACQUIRE));
    }
    portEXIT_CRITICAL_ISR(&lock);
    return false;
}
static void retire_intervals(void) {
    portENTER_CRITICAL(&lock);
    int64_t now=paused ? pause_started:esp_timer_get_time();
    int64_t elapsed=now-launch_us-paused_us;
    uint64_t limit=tx_done ? encoded_ticks:elapsed>0 ? (uint64_t)elapsed*16:0;
    uint64_t completed=completed_ticks();
    unsigned budget=tx_done ? FIFO_SIZE:128;
    while(tail!=encode_cursor && budget--) {
        raw_pulse_t *event=&fifo[tail];
        uint64_t ticks=event_ticks(tail);
        if(retired_tx_ticks+ticks>limit)break;
        retired_tx_ticks+=ticks;completed+=ticks;
        pps_time+=ticks;if(event->count_pulse & RAW_PULSE_COUNT)++pps_count;
        if(pps_time>=16000000) {pps=(uint32_t)((uint64_t)pps_count*16000000/pps_time);pps_time=0;pps_count=0;}
        __atomic_store_n(&tail,(tail+1)&FIFO_MASK,__ATOMIC_RELEASE);
    }
    publish_completed(completed);
    portEXIT_CRITICAL(&lock);
}
void raw_pulse_stop(void) {
    if(initialized) {
        portENTER_CRITICAL(&lock);
        accept_callbacks=false;
        /* Reactivate the functional clock before using IDF's stop/reset path. */
        rmt_ll_enable_group_clock(&RMT,true);
        portEXIT_CRITICAL(&lock);
        if(channel_enabled) {
            if(rmt_disable(channel)!=ESP_OK)error=true;
            else channel_enabled=false;
        }
    }
    running=paused=false;
    /* GPIO is physically handed back to UL/idle, not left behind an RMT mux. */
    gpio_reset_pin(OUTPUT_GPIO);gpio_set_level(OUTPUT_GPIO,0);gpio_set_direction(OUTPUT_GPIO,GPIO_MODE_OUTPUT);
}
void raw_pulse_init(void) {
    raw_pulse_stop();
    if(channel_enabled) {error=true;return;}
    if(!initialized) {
        rmt_tx_channel_config_t config={.gpio_num=-1,.clk_src=RMT_CLK_SRC_DEFAULT,
            .resolution_hz=16000000,.mem_block_symbols=128,.trans_queue_depth=1,.intr_priority=3,.flags.with_dma=1};
        rmt_simple_encoder_config_t encode_config={.callback=encode,.min_chunk_size=1};
        rmt_tx_event_callbacks_t callbacks={.on_trans_done=transmitted};
        if(rmt_new_tx_channel(&config,&channel)!=ESP_OK) {error=true;return;}
        if(rmt_new_simple_encoder(&encode_config,&encoder)!=ESP_OK ||
           rmt_tx_register_event_callbacks(channel,&callbacks,NULL)!=ESP_OK) {
            if(encoder)rmt_del_encoder(encoder);
            rmt_del_channel(channel);channel=NULL;encoder=NULL;error=true;return;
        }
        initialized=true;
    }
    head=tail=encode_cursor=0;encode_remaining=encoded_ticks=retired_tx_ticks=0;
    running=paused=tx_done=encode_starved=finish_requested=accept_callbacks=underrun=error=false;
    launch_us=pause_started=paused_us=0;pps=pps_count=0;pps_time=0;publish_completed(0);
}
bool raw_pulse_start(void) {
    if(!initialized || error || running || !backend_enabled || !output_enabled || head==tail)return false;
    if(channel_enabled && rmt_disable(channel)!=ESP_OK) {error=true;return false;}
    channel_enabled=false;
    if(rmt_tx_switch_gpio(channel,OUTPUT_GPIO,false)!=ESP_OK || rmt_enable(channel)!=ESP_OK) {error=true;raw_pulse_stop();return false;}
    channel_enabled=true;
    portENTER_CRITICAL(&lock);
    encode_cursor=tail;encode_remaining=encoded_ticks=retired_tx_ticks=0;
    tx_done=encode_starved=paused=false;paused_us=0;running=accept_callbacks=true;
    portEXIT_CRITICAL(&lock);
    rmt_transmit_config_t config={.loop_count=0,.flags.eot_level=0,.flags.queue_nonblocking=1};
    if(rmt_transmit(channel,encoder,&token,sizeof(token),&config)!=ESP_OK) {error=true;raw_pulse_stop();return false;}
    /* The idle channel starts inside rmt_transmit. Timestamping afterwards,
     * with a small guard, releases source slots conservatively, never early.
     * TX-done reconciles the exact sum of actually transmitted intervals. */
    launch_us=esp_timer_get_time()+10;
    return true;
}
bool raw_pulse_service(void) { if(initialized)retire_intervals();return !error; }
void raw_pulse_pause(void) {
    portENTER_CRITICAL(&lock);
    if(running && !paused) {
        pause_started=esp_timer_get_time();
        /* ESP32-S3: gate only the RMT source clock, retain duration counter,
         * symbol pointer and DMA state. CMT owns this entire RMT group; RX is
         * on MCPWM. Do not reset/disable a transaction to implement pause. */
        rmt_ll_enable_group_clock(&RMT,false);paused=true;
    }
    portEXIT_CRITICAL(&lock);
}
void raw_pulse_resume(void) {
    portENTER_CRITICAL(&lock);
    if(running && paused) {
        rmt_ll_enable_group_clock(&RMT,true);
        paused_us+=esp_timer_get_time()-pause_started;paused=false;
    }
    portEXIT_CRITICAL(&lock);
}
void raw_pulse_finish(void) { portENTER_CRITICAL(&lock);finish_requested=true;portEXIT_CRITICAL(&lock); }
static size_t write_events(const raw_pulse_t *events,const uint32_t *ticks,size_t count) {
    if(!events || error)return 0;
    size_t written=0;uint32_t h=__atomic_load_n(&head,__ATOMIC_RELAXED);
    while(written<count) {
        uint32_t next=(h+1)&FIFO_MASK;
        if(next==__atomic_load_n(&tail,__ATOMIC_ACQUIRE))break;
        raw_pulse_t event=events[written];
        if(ticks)event.duration_us=ticks[written];
        if(!event.duration_us)event.duration_us=1;
        uint32_t mask=1U<<(h%32);
        uint32_t flags=__atomic_load_n(&native_flags[h/32],__ATOMIC_RELAXED);
        __atomic_store_n(&native_flags[h/32],ticks ? flags|mask:flags&~mask,__ATOMIC_RELAXED);
        fifo[h]=event;h=next;++written;__atomic_store_n(&head,h,__ATOMIC_RELEASE);
    }
    return written;
}
bool raw_pulse_native_ticks16(void) {return true;}
size_t raw_pulse_write(const raw_pulse_t *events,size_t count) {return write_events(events,NULL,count);}
size_t raw_pulse_write_ticks16(const raw_pulse_t *events,const uint32_t *ticks,size_t count) {return ticks ? write_events(events,ticks,count):0;}
size_t raw_pulse_level(void) {return (__atomic_load_n(&head,__ATOMIC_ACQUIRE)-__atomic_load_n(&tail,__ATOMIC_ACQUIRE))&FIFO_MASK;}
size_t raw_pulse_free(void) {return FIFO_SIZE-1-raw_pulse_level();}
uint32_t raw_pulse_get_pulses_per_second(void) {return pps;}
bool raw_pulse_is_running(void) {return running;}
bool raw_pulse_is_underrun(void) {return underrun;}
void raw_pulse_clear_underrun(void) {underrun=false;}
void raw_pulse_set_output_enabled(bool enabled) {output_enabled=enabled;if(!enabled)raw_pulse_stop();}
void raw_pulse_set_rmt_enabled(bool enabled) {if(!running)backend_enabled=enabled;}
void raw_pulse_set_read_invert(bool invert) {if(!running)read_invert=invert;}
bool raw_pulse_get_read_invert(void) {return read_invert;}
void raw_pulse_set_tick_us(uint32_t tick) {if(tick)legacy_tick_us=tick;}
uint32_t raw_pulse_get_tick_us(void) {return legacy_tick_us;}
size_t raw_pulse_write_samples(const uint8_t *samples,size_t count) {(void)samples;(void)count;return 0;}
size_t raw_pulse_sample_free(void) {return 0;}
size_t raw_pulse_sample_level(void) {return 0;}

bool raw_pulse_monitor_active(void) {
    portENTER_CRITICAL(&lock);
    bool active=running && !paused && tail!=__atomic_load_n(&head,__ATOMIC_ACQUIRE)
        && !(fifo[tail].count_pulse & RAW_PULSE_MONITOR_MUTE);
    portEXIT_CRITICAL(&lock);return active;
}
