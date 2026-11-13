#include "audio/edge_capture.h"
#include "driver/gpio.h"
#include "driver/mcpwm_cap.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <stdint.h>

#define CAPTURE_GPIO GPIO_NUM_5
#define FIFO_SIZE 8192U
#define FIFO_MASK (FIFO_SIZE-1U)
static DRAM_ATTR edge_capture_event_t fifo[FIFO_SIZE];
static DRAM_ATTR volatile uint32_t head,tail,enqueued_edges;
static DRAM_ATTR uint32_t min_free=FIFO_SIZE-1;
static DRAM_ATTR edge_capture_fault_t fault;
static DRAM_ATTR portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static mcpwm_cap_timer_handle_t timer;
static mcpwm_cap_channel_handle_t channel,snapshot;
static bool initialized,channel_enabled,timer_enabled,timer_started,snapshot_enabled;
static DRAM_ATTR volatile bool running,paused,overrun;
static DRAM_ATTR uint32_t ticks_per_us,unit_us=1,last_count,last_hardware;
static DRAM_ATTR int64_t last_wall_us;
static DRAM_ATTR uint8_t active_level;

/* Only the foreground consumer touches tail. Producer is the hardware-edge
 * callback, or pause with that callback excluded by the same internal lock. */
static bool IRAM_ATTR push(uint8_t level,uint64_t ticks,bool rising,bool physical_edge) {
    uint64_t divisor=(uint64_t)ticks_per_us*unit_us;
    uint64_t units=(ticks+divisor/2)/divisor;
    if(!units)units=1;
    if(units>UINT32_MAX)units=UINT32_MAX;
    uint32_t h=__atomic_load_n(&head,__ATOMIC_RELAXED),t=__atomic_load_n(&tail,__ATOMIC_ACQUIRE);
    uint32_t next=(h+1)&FIFO_MASK;
    if(next==t) {fault=EDGE_CAPTURE_FIFO_OVERFLOW;
    overrun=true;
    running=false;
    return false;
    }
    fifo[h]=(edge_capture_event_t){level,rising,(uint32_t)units};
    uint32_t free_slots=(t-next-1)&FIFO_MASK;
    if(free_slots<min_free)min_free=free_slots;
    if(physical_edge){if(enqueued_edges==UINT32_MAX){fault=EDGE_CAPTURE_EDGE_LOSS;
    overrun=true;
    running=false;
    return false;
    }++enqueued_edges;
    }
    __atomic_store_n(&head,next,__ATOMIC_RELEASE);
    return true;
}
/* The 32-bit latch wraps about every 53.7 s at 80 MHz. Wall time chooses
 * only the wrap count; every sub-wrap interval comes from the hardware latch.
 * ISR scheduling latency therefore cannot change the captured pulse width. */
static uint64_t IRAM_ATTR elapsed_ticks(uint32_t count,int64_t wall) {
    uint64_t delta=(uint32_t)(count-last_count);
    uint64_t elapsed=wall>=last_wall_us ? (uint64_t)(wall-last_wall_us):0;
    uint64_t estimate=elapsed*ticks_per_us;
    if(estimate>delta+0x80000000ULL)
        delta+=((estimate-delta+0x80000000ULL)>>32)<<32;
    return delta;
}
static void IRAM_ATTR accept_capture(uint32_t count,uint8_t new_level,int64_t wall) {
    uint64_t ticks=elapsed_ticks(count,wall);
    /* Discard a latched interrupt from before start/resume. A live capture
     * cannot be >10ms in the future relative to the independent wall clock. */
    uint64_t elapsed=wall>=last_wall_us ? (uint64_t)(wall-last_wall_us):0;
    if(ticks>((elapsed+10000ULL)*ticks_per_us))return;
    if(!ticks)return;
    if(new_level==active_level) {fault=EDGE_CAPTURE_EDGE_LOSS;
    overrun=true;
    running=false;
    return;
    }
    if(!push(active_level,ticks,!active_level && new_level,true))return;
    last_count=last_hardware=count;
    last_wall_us=wall;
    active_level=new_level;
}
static bool IRAM_ATTR on_capture(mcpwm_cap_channel_handle_t handle,const mcpwm_capture_event_data_t *event,void *context) {
    (void)handle;
    (void)context;
    portENTER_CRITICAL_ISR(&lock);
    if(running && !paused && !overrun)
        accept_capture(event->cap_value,event->cap_edge==MCPWM_CAP_EDGE_POS ? 1:0,esp_timer_get_time());
    portEXIT_CRITICAL_ISR(&lock);
    return false;
}
static bool clock_snapshot(uint32_t *count) {
    return mcpwm_capture_channel_trigger_soft_catch(snapshot)==ESP_OK &&
        mcpwm_capture_get_latched_value(snapshot,count)==ESP_OK;
}
static void discard_partial_initialization(void) {
    if(snapshot_enabled)mcpwm_capture_channel_disable(snapshot);
    if(timer_started)mcpwm_capture_timer_stop(timer);
    if(timer_enabled)mcpwm_capture_timer_disable(timer);
    if(snapshot)mcpwm_del_capture_channel(snapshot);
    if(channel)mcpwm_del_capture_channel(channel);
    if(timer)mcpwm_del_capture_timer(timer);
    timer=NULL;
    channel=snapshot=NULL;
    snapshot_enabled=timer_started=timer_enabled=initialized=false;
}
void edge_capture_init(uint32_t units) {
    edge_capture_stop();
    if(channel_enabled) {overrun=true;
    return;
    }
    unit_us=units ? units:1;
    if(!edge_guard_init()){fault=EDGE_CAPTURE_GUARD_ERROR;
    overrun=true;
    return;
    }
    if(!initialized) {
        gpio_config_t input={.pin_bit_mask=1ULL<<CAPTURE_GPIO,.mode=GPIO_MODE_INPUT,
            .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_ENABLE,.intr_type=GPIO_INTR_DISABLE};
        mcpwm_capture_timer_config_t config={.group_id=0,.clk_src=MCPWM_CAPTURE_CLK_SRC_DEFAULT};
        mcpwm_capture_channel_config_t input_channel={.gpio_num=CAPTURE_GPIO,.prescale=1,.intr_priority=3,
            .flags.pos_edge=1,.flags.neg_edge=1};
        /* Separate software-only channel: snapshots never masquerade as WRITE
         * transitions and never trigger the real edge callback. */
        mcpwm_capture_channel_config_t snapshot_channel={.gpio_num=-1,.prescale=1};
        mcpwm_capture_event_callbacks_t callbacks={.on_cap=on_capture};
        uint32_t resolution;
        if(gpio_config(&input)!=ESP_OK || mcpwm_new_capture_timer(&config,&timer)!=ESP_OK ||
           mcpwm_capture_timer_get_resolution(timer,&resolution)!=ESP_OK || resolution<1000000 || resolution%1000000 ||
           mcpwm_new_capture_channel(timer,&input_channel,&channel)!=ESP_OK ||
           mcpwm_new_capture_channel(timer,&snapshot_channel,&snapshot)!=ESP_OK ||
           mcpwm_capture_channel_register_event_callbacks(channel,&callbacks,NULL)!=ESP_OK)goto failed;
        ticks_per_us=resolution/1000000;
        if(mcpwm_capture_timer_enable(timer)!=ESP_OK)goto failed;
        timer_enabled=true;
        if(mcpwm_capture_timer_start(timer)!=ESP_OK)goto failed;
        timer_started=true;
        if(mcpwm_capture_channel_enable(snapshot)!=ESP_OK)goto failed;
        snapshot_enabled=true;
        initialized=true;
    }
    __atomic_store_n(&head,0,__ATOMIC_RELAXED);
    __atomic_store_n(&tail,0,__ATOMIC_RELAXED);
    edge_guard_reset();
    enqueued_edges=0;
    min_free=FIFO_SIZE-1;
    fault=EDGE_CAPTURE_OK;
    running=paused=overrun=false;
    return;
failed:
    discard_partial_initialization();
    overrun=true;
}
void edge_capture_start(void) {
    if(!initialized || channel_enabled) {overrun=true;
    return;
    }
    uint32_t count,previous;
    if(!edge_guard_begin(enqueued_edges)){overrun=true;
    fault=EDGE_CAPTURE_GUARD_ERROR;
    return;
    }
    if(!clock_snapshot(&count) || mcpwm_capture_get_latched_value(channel,&previous)!=ESP_OK) {edge_guard_abort();
    overrun=true;
    return;
    }
    portENTER_CRITICAL(&lock);
    __atomic_store_n(&head,0,__ATOMIC_RELAXED);
    __atomic_store_n(&tail,0,__ATOMIC_RELAXED);
    last_count=count;
    last_hardware=previous;
    last_wall_us=esp_timer_get_time();
    active_level=(uint8_t)(gpio_get_level(CAPTURE_GPIO)!=0);
    overrun=paused=false;
    running=true;
    portEXIT_CRITICAL(&lock);
    if(mcpwm_capture_channel_enable(channel)!=ESP_OK) {edge_guard_abort();
    running=false;
    overrun=true;
    return;
    }
    channel_enabled=true;
    edge_guard_started();
}
void edge_capture_stop(void) {
    if(initialized && running && !paused && channel_enabled)edge_capture_pause();
    if(channel_enabled) {
        edge_guard_stopping();
        if(mcpwm_capture_channel_disable(channel)!=ESP_OK) {overrun=true;
        return;
        }
        channel_enabled=false;
        edge_guard_end(enqueued_edges);
    }
    edge_guard_abort();
    portENTER_CRITICAL(&lock);
    running=paused=false;
    portEXIT_CRITICAL(&lock);
}
void edge_capture_pause(void) {
    if(!initialized || !running || paused)return;
    edge_guard_stopping();
    if(channel_enabled && mcpwm_capture_channel_disable(channel)!=ESP_OK) {overrun=true;
    running=false;
    return;
    }
    channel_enabled=false;
    portENTER_CRITICAL(&lock);
    /* A captured edge may still await its ISR when disable masks the IRQ.
     * Flush that latched edge first, then the partial current-level tail. */
    uint32_t captured,count;
    if(mcpwm_capture_get_latched_value(channel,&captured)!=ESP_OK || !clock_snapshot(&count)) {
        overrun=true;
        running=false;
    } else {
        int64_t wall=esp_timer_get_time();
        if(captured!=last_hardware)accept_capture(captured,(uint8_t)!active_level,wall);
        uint64_t ticks=elapsed_ticks(count,wall);
        if(running && ticks)push(active_level,ticks,false,false);
        last_count=count;
        last_wall_us=wall;
    }
    paused=true;
    portEXIT_CRITICAL(&lock);
    edge_guard_end(enqueued_edges);
    edge_guard_status_t guard;
    edge_guard_snapshot(&guard);
    if(guard.confirmed || guard.unreliable){fault=guard.confirmed ? EDGE_CAPTURE_EDGE_LOSS:EDGE_CAPTURE_GUARD_ERROR;
    overrun=true;
    running=false;
    }
}
void edge_capture_resume(void) {
    if(!initialized || !running || !paused || overrun)return;
    uint32_t count,previous;
    if(!edge_guard_begin(enqueued_edges)){overrun=true;
    fault=EDGE_CAPTURE_GUARD_ERROR;
    running=false;
    return;
    }
    if(!clock_snapshot(&count) || mcpwm_capture_get_latched_value(channel,&previous)!=ESP_OK) {edge_guard_abort();
    overrun=true;
    running=false;
    return;
    }
    portENTER_CRITICAL(&lock);
    last_count=count;
    last_hardware=previous;
    last_wall_us=esp_timer_get_time();
    active_level=(uint8_t)(gpio_get_level(CAPTURE_GPIO)!=0);
    paused=false;
    portEXIT_CRITICAL(&lock);
    if(mcpwm_capture_channel_enable(channel)!=ESP_OK) {edge_guard_abort();
    running=false;
    overrun=true;
    return;
    }
    channel_enabled=true;
    edge_guard_started();
}
size_t edge_capture_read(edge_capture_event_t *out,size_t maximum) {
    if(!out || !initialized)return 0;
    uint32_t t=__atomic_load_n(&tail,__ATOMIC_RELAXED),h=__atomic_load_n(&head,__ATOMIC_ACQUIRE);
    size_t count=0;
    while(count<maximum && t!=h) {out[count++]=fifo[t];
    t=(t+1)&FIFO_MASK;
    }
    __atomic_store_n(&tail,t,__ATOMIC_RELEASE);
    return count;
}
size_t edge_capture_level(void) {
    uint32_t h=__atomic_load_n(&head,__ATOMIC_ACQUIRE),t=__atomic_load_n(&tail,__ATOMIC_ACQUIRE);
    return (h-t)&FIFO_MASK;
}
size_t edge_capture_free(void) { return FIFO_SIZE-1-edge_capture_level();
}
bool edge_capture_is_running(void) { return running;
}
bool edge_capture_is_overrun(void) { return overrun;
}
void edge_capture_clear_overrun(void) { overrun=false;
}

void edge_capture_status(edge_capture_status_t *out){
 edge_guard_poll();
 edge_guard_snapshot(&out->guard);
 portENTER_CRITICAL(&lock);
 uint32_t used=(head-tail)&FIFO_MASK;
 out->enqueued_edges=enqueued_edges;
 out->free_percent=(uint8_t)((FIFO_SIZE-1-used)*100/(FIFO_SIZE-1));
 out->min_free_percent=(uint8_t)(min_free*100/(FIFO_SIZE-1));
 out->fault=fault;
 portEXIT_CRITICAL(&lock);
 if(out->guard.unreliable){out->fault=fault=EDGE_CAPTURE_GUARD_ERROR;
 overrun=true;
 }
}
