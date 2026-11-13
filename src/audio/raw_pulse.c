#include "audio/raw_pulse.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

#include <stdbool.h>
#include <stdint.h>

/* CMT READ output. No RMT channel and no auxiliary GPIO are used here. */
#define RAW_PULSE_PIN_OUT              GPIO_NUM_4
#define RAW_PULSE_RES_HZ               1000000UL
#define RAW_PULSE_FIFO_SIZE             4096U
#define RAW_PULSE_FIFO_MASK             (RAW_PULSE_FIFO_SIZE - 1U)
#define RAW_PULSE_PPS_WINDOW_US         1000000UL

static raw_pulse_t s_fifo[RAW_PULSE_FIFO_SIZE];

/* Producer: player task. Consumer: GPTimer ISR. */
static volatile uint32_t s_head = 0U;
static volatile uint32_t s_tail = 0U;

static gptimer_handle_t s_timer = NULL;
static bool s_initialized = false;
static bool s_timer_running = false;

static volatile bool s_running = false;
static volatile bool s_paused = false;
static volatile bool s_underrun = false;
static volatile bool s_output_enabled = true;
static volatile bool s_read_invert = false;
static volatile uint32_t s_isr_depth = 0U;

/* One pulse is active on GPIO until the next GPTimer alarm. */
static volatile bool s_active_valid = false;
static raw_pulse_t s_active_pulse;
static volatile uint32_t s_active_remaining_us = 0U;
static volatile bool s_finish_requested = false;

/* Completion time, published with a 32-bit sequence to avoid non-lock-free
 * 64-bit atomics inside the ISR. Reset only while the timer is stopped. */
static uint32_t source_bytes;
static uint32_t s_time_seq, s_completed_low, s_completed_high;
static volatile uint32_t s_pulses_per_second = 0U;
static volatile uint32_t s_pps_window_us = 0U;
static volatile uint32_t s_pps_window_pulses = 0U;

/* Compatibility-only values. They do not drive the transport. */
static uint32_t s_legacy_tick_us = 16U;

static inline void IRAM_ATTR raw_pulse_write_pin_raw(int level)
{
    const uint32_t mask = (1UL << RAW_PULSE_PIN_OUT);

    if (level) {
        REG_WRITE(GPIO_OUT_W1TS_REG, mask);
    } else {
        REG_WRITE(GPIO_OUT_W1TC_REG, mask);
    }
}

static inline void IRAM_ATTR raw_pulse_write_pin_play(uint8_t level)
{
    bool out = level != 0U;

    if (s_read_invert) {
        out = !out;
    }
    if (!s_output_enabled) {
        out = false;
    }
    raw_pulse_write_pin_raw(out ? 1 : 0);
}

static inline uint32_t fifo_level_snapshot(uint32_t head, uint32_t tail)
{
    return (head - tail) & RAW_PULSE_FIFO_MASK;
}

static inline bool fifo_has_free(uint32_t head, uint32_t tail)
{
    return ((head + 1U) & RAW_PULSE_FIFO_MASK) != tail;
}

static inline void IRAM_ATTR raw_pulse_reset_pps_window(void)
{
    s_pps_window_us = 0U;
    s_pps_window_pulses = 0U;
}

static inline void IRAM_ATTR raw_pulse_commit_pps_window(void)
{
    if (s_pps_window_us != 0U && s_pps_window_pulses != 0U) {
        s_pulses_per_second = (uint32_t)(
            ((uint64_t)s_pps_window_pulses * 1000000ULL) /
            (uint64_t)s_pps_window_us);
    }
    raw_pulse_reset_pps_window();
}

static inline void IRAM_ATTR raw_pulse_account_completed(const raw_pulse_t *pulse)
{
    if (pulse == NULL || pulse->duration_us == 0U) {
        return;
    }

    /* Exactly one writer: timer ISR, or init after ISR shutdown. Plain atomic
     * loads/stores avoid adding libatomic RMW critical sections per interval. */
    uint32_t seq = __atomic_load_n(&s_time_seq, __ATOMIC_RELAXED);
    __atomic_store_n(&s_time_seq, seq + 1U, __ATOMIC_SEQ_CST);
    uint32_t low = __atomic_load_n(&s_completed_low, __ATOMIC_RELAXED);
    uint32_t next = low + pulse->duration_us;
    __atomic_store_n(&s_completed_low, next, __ATOMIC_RELAXED);
    if (next < low) {
        uint32_t high = __atomic_load_n(&s_completed_high, __ATOMIC_RELAXED);
        __atomic_store_n(&s_completed_high, high + 1U, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_time_seq, seq + 2U, __ATOMIC_RELEASE);

    if(pulse->count_pulse & RAW_PULSE_SOURCE_BYTE)__atomic_fetch_add(&source_bytes,1,__ATOMIC_RELAXED);
    s_pps_window_us += pulse->duration_us;
    if ((pulse->count_pulse & RAW_PULSE_COUNT) != 0U) {
        s_pps_window_pulses++;
    }

    if (s_pps_window_us >= RAW_PULSE_PPS_WINDOW_US) {
        raw_pulse_commit_pps_window();
    }
}

static bool IRAM_ATTR raw_pulse_pop_next(raw_pulse_t *pulse)
{
    uint32_t tail;
    uint32_t head;

    if (pulse == NULL) {
        return false;
    }

    tail = __atomic_load_n(&s_tail, __ATOMIC_RELAXED);
    head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;
    }

    *pulse = s_fifo[tail];
    tail = (tail + 1U) & RAW_PULSE_FIFO_MASK;
    __atomic_store_n(&s_tail, tail, __ATOMIC_RELEASE);
    return true;
}

static void IRAM_ATTR raw_pulse_arm_alarm(uint32_t duration_us)
{
    gptimer_alarm_config_t alarm = {
        .alarm_count = duration_us == 0U ? 1U : duration_us,
        .reload_count = 0U,
        .flags.auto_reload_on_alarm = false,
    };

    /* Every active interval begins at count zero. */
    (void)gptimer_set_raw_count(s_timer, 0U);
    (void)gptimer_set_alarm_action(s_timer, &alarm);
}

static bool IRAM_ATTR raw_pulse_begin_next(void)
{
    raw_pulse_t next;

    if (!raw_pulse_pop_next(&next)) {
        return false;
    }

    if (next.duration_us == 0U) {
        next.duration_us = 1U;
    }

    s_active_pulse = next;
    s_active_remaining_us = next.duration_us;
    s_active_valid = true;
    raw_pulse_write_pin_play(next.level);
    raw_pulse_arm_alarm(next.duration_us);
    return true;
}

static bool IRAM_ATTR on_pulse_timer(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *edata,
    void *arg)
{
    (void)timer;
    (void)edata;
    (void)arg;

    __atomic_fetch_add(&s_isr_depth, 1U, __ATOMIC_RELAXED);

    if (!s_running || s_paused) {
        __atomic_fetch_sub(&s_isr_depth, 1U, __ATOMIC_RELAXED);
        return false;
    }

    if (s_active_valid) {
        raw_pulse_account_completed(&s_active_pulse);
        s_active_valid = false;
        s_active_remaining_us = 0U;
    }

    if (!raw_pulse_begin_next()) {
        s_running = false;
        s_active_valid = false;
        s_active_remaining_us = 0U;

        if (!s_finish_requested) {
            s_underrun = true;
        }
        raw_pulse_write_pin_raw(0);
    }

    __atomic_fetch_sub(&s_isr_depth, 1U, __ATOMIC_RELEASE);
    return false;
}

static void raw_pulse_wait_isr_idle(void)
{
    while (__atomic_load_n(&s_isr_depth, __ATOMIC_ACQUIRE) != 0U) {
        vTaskDelay(1);
    }
}

static void raw_pulse_stop_timer(void)
{
    if (s_timer != NULL && s_timer_running) {
        (void)gptimer_stop(s_timer);
        s_timer_running = false;
    }
}

static bool raw_pulse_start_timer_for_active(void)
{
    if (s_timer == NULL || !s_active_valid) {
        return false;
    }

    if (s_active_remaining_us == 0U) {
        s_active_remaining_us = 1U;
    }

    raw_pulse_write_pin_play(s_active_pulse.level);
    raw_pulse_arm_alarm(s_active_remaining_us);

    if (!s_timer_running) {
        if (gptimer_start(s_timer) != ESP_OK) {
            return false;
        }
        s_timer_running = true;
    }

    return true;
}

void raw_pulse_init(void)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << RAW_PULSE_PIN_OUT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    (void)gpio_config(&out_cfg);
    raw_pulse_write_pin_raw(0);

    if (!s_initialized) {
        gptimer_config_t config = {
            .clk_src = GPTIMER_CLK_SRC_DEFAULT,
            .direction = GPTIMER_COUNT_UP,
            .resolution_hz = RAW_PULSE_RES_HZ,
            .intr_priority = 3,
        };
        gptimer_event_callbacks_t callbacks = {
            .on_alarm = on_pulse_timer,
        };

        if (gptimer_new_timer(&config, &s_timer) != ESP_OK ||
            gptimer_register_event_callbacks(s_timer, &callbacks, NULL) != ESP_OK ||
            gptimer_enable(s_timer) != ESP_OK) {
            s_timer = NULL;
            s_initialized = false;
            return;
        }
        s_initialized = true;
    }

    raw_pulse_stop_timer();
    raw_pulse_wait_isr_idle();
    __atomic_store_n(&source_bytes,0,__ATOMIC_RELAXED);

    __atomic_store_n(&s_head, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tail, 0U, __ATOMIC_RELAXED);

    s_running = false;
    s_paused = false;
    s_underrun = false;
    s_finish_requested = false;
    s_active_valid = false;
    s_active_remaining_us = 0U;
    s_pulses_per_second = 0U;
    uint32_t seq = __atomic_load_n(&s_time_seq, __ATOMIC_RELAXED);
    __atomic_store_n(&s_time_seq, seq + 1U, __ATOMIC_SEQ_CST);
    __atomic_store_n(&s_completed_low, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&s_completed_high, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&s_time_seq, seq + 2U, __ATOMIC_RELEASE);
    raw_pulse_reset_pps_window();
}

bool raw_pulse_start(void)
{
    if (!s_initialized || s_timer == NULL) {
        return false;
    }

    raw_pulse_stop_timer();
    raw_pulse_wait_isr_idle();

    s_paused = false;
    s_underrun = false;
    s_running = true;

    if (!s_active_valid && !raw_pulse_begin_next()) {
        s_running = false;
        raw_pulse_write_pin_raw(0);
        return false;
    }

    if (!raw_pulse_start_timer_for_active()) {
        s_running = false;
        raw_pulse_write_pin_raw(0);
        return false;
    }

    return true;
}

bool raw_pulse_service(void)
{
    /* All real-time work is done by GPTimer. Foreground only feeds the FIFO. */
    return s_initialized;
}

void raw_pulse_stop(void)
{
    s_running = false;
    s_paused = false;
    raw_pulse_stop_timer();
    raw_pulse_wait_isr_idle();

    __atomic_store_n(&s_head, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tail, 0U, __ATOMIC_RELAXED);

    s_underrun = false;
    s_finish_requested = false;
    s_active_valid = false;
    s_active_remaining_us = 0U;

    if (s_pulses_per_second == 0U) {
        raw_pulse_commit_pps_window();
    } else {
        raw_pulse_reset_pps_window();
    }

    raw_pulse_write_pin_raw(0);
}

void raw_pulse_pause(void)
{
    uint64_t elapsed = 0U;

    if (!s_initialized || !s_running || s_paused) {
        return;
    }

    /* Stop future ISR transitions first, then keep the unplayed part of active pulse. */
    s_paused = true;
    raw_pulse_stop_timer();
    raw_pulse_wait_isr_idle();

    if (s_active_valid && s_timer != NULL) {
        (void)gptimer_get_raw_count(s_timer, &elapsed);
        if (elapsed >= s_active_remaining_us) {
            s_active_remaining_us = 1U;
        } else {
            s_active_remaining_us -= (uint32_t)elapsed;
            if (s_active_remaining_us == 0U) {
                s_active_remaining_us = 1U;
            }
        }
    }

    /* MOTOR/USER pause must expose an inactive CMT output. */
    raw_pulse_write_pin_raw(0);
}

void raw_pulse_resume(void)
{
    if (!s_initialized || !s_running || !s_paused || s_underrun) {
        return;
    }

    if (!s_active_valid && !raw_pulse_begin_next()) {
        s_running = false;
        if (!s_finish_requested) {
            s_underrun = true;
        }
        raw_pulse_write_pin_raw(0);
        return;
    }

    s_paused = false;
    if (!raw_pulse_start_timer_for_active()) {
        s_running = false;
        s_underrun = true;
        raw_pulse_write_pin_raw(0);
    }
}

size_t raw_pulse_write(const raw_pulse_t *pulses, size_t count)
{
    uint32_t head;
    uint32_t tail;
    size_t written = 0U;

    if (!s_initialized || pulses == NULL || count == 0U) {
        return 0U;
    }

    head = __atomic_load_n(&s_head, __ATOMIC_RELAXED);
    tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);

    while (written < count && fifo_has_free(head, tail)) {
        raw_pulse_t pulse = pulses[written];
        if (pulse.duration_us == 0U) {
            pulse.duration_us = 1U;
        }
        s_fifo[head] = pulse;
        head = (head + 1U) & RAW_PULSE_FIFO_MASK;
        written++;
    }

    if (written != 0U) {
        __atomic_store_n(&s_head, head, __ATOMIC_RELEASE);
        s_finish_requested = false;
    }

    return written;
}

void raw_pulse_finish(void)
{
    s_finish_requested = true;
}

size_t raw_pulse_free(void)
{
    const uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    return (RAW_PULSE_FIFO_SIZE - 1U) - fifo_level_snapshot(head, tail);
}

size_t raw_pulse_level(void)
{
    const uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    return fifo_level_snapshot(head, tail) + (s_active_valid ? 1U : 0U);
}

uint32_t raw_pulse_get_pulses_per_second(void)
{
    return s_pulses_per_second;
}

bool raw_pulse_is_running(void)
{
    return s_running;
}

bool raw_pulse_is_underrun(void)
{
    return s_underrun;
}

void raw_pulse_clear_underrun(void)
{
    s_underrun = false;
}

void raw_pulse_set_output_enabled(bool enabled)
{
    s_output_enabled = enabled;
    if (!enabled) {
        raw_pulse_stop();
    }
}

void raw_pulse_set_rmt_enabled(bool enabled)
{
    (void)enabled;
    /* Deliberately a no-op: no GPIO12, no RMT tick generator, no RMT channel. */
}

void raw_pulse_set_read_invert(bool invert)
{
    s_read_invert = invert;
}

bool raw_pulse_get_read_invert(void)
{
    return s_read_invert;
}

void raw_pulse_set_tick_us(uint32_t tick_us)
{
    if (tick_us != 0U) {
        s_legacy_tick_us = tick_us;
    }
}

uint32_t raw_pulse_get_tick_us(void)
{
    return s_legacy_tick_us;
}

size_t raw_pulse_write_samples(const uint8_t *samples, size_t count)
{
    (void)samples;
    (void)count;
    /* WAV is handled by raw_out/raw_timer; keeping this avoids a stale API break. */
    return 0U;
}

size_t raw_pulse_sample_free(void)
{
    return 0U;
}

size_t raw_pulse_sample_level(void)
{
    return 0U;
}

uint64_t raw_pulse_get_completed_us(void)
{
    for (;;) {
        uint32_t before=__atomic_load_n(&s_time_seq,__ATOMIC_ACQUIRE);
        if (before & 1U) continue;
        uint32_t low=__atomic_load_n(&s_completed_low,__ATOMIC_RELAXED);
        uint32_t high=__atomic_load_n(&s_completed_high,__ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (before == __atomic_load_n(&s_time_seq,__ATOMIC_RELAXED))
            return ((uint64_t)high<<32) | low;
    }
}

bool raw_pulse_native_ticks16(void) {return false;}
size_t raw_pulse_write_ticks16(const raw_pulse_t *events,const uint32_t *ticks,size_t count) {
    /* Capability is false on this diagnostic microsecond backend. */
    (void)events;(void)ticks;(void)count;return 0;
}

bool raw_pulse_monitor_active(void) {return s_running && !s_paused && s_active_valid && !(s_active_pulse.count_pulse & RAW_PULSE_MONITOR_MUTE);}

void raw_pulse_status(raw_pulse_status_t *out){
 *out=(raw_pulse_status_t){.fifo_used=(uint32_t)raw_pulse_level(),.fifo_capacity=RAW_PULSE_FIFO_SIZE-1,
 .time_valid=false,.running=s_running,.paused=s_paused,.underrun=s_underrun};
}
uint32_t raw_pulse_completed_source_bytes(void){return __atomic_load_n(&source_bytes,__ATOMIC_ACQUIRE);}
