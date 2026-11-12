#include "raw_timer.h"

#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PIN_OUT 4

#define RAW_TIMER_RES_HZ 40000000UL

/* Must be a power of two. One logical sample occupies one byte. */
#define RAW_FIFO_SIZE 65536U
#define RAW_FIFO_MASK (RAW_FIFO_SIZE - 1U)

static uint8_t s_fifo[RAW_FIFO_SIZE];

/*
 * SPSC ring:
 * - producer (task) updates only s_head
 * - consumer (timer ISR) updates only s_tail
 */
static volatile uint32_t s_head = 0;
static volatile uint32_t s_tail = 0;

static gptimer_handle_t s_timer = NULL;
static bool s_initialized = false;
static bool s_timer_started = false;
static int s_sample_rate = 0;

static volatile bool s_running = false;
static volatile bool s_paused = false;
static volatile bool s_underrun = false;
static volatile bool s_output_enabled = true;
static volatile bool s_source_finished = false;

/*
 * Set after the final sample has been put on GPIO.  The timer keeps the final
 * level for one full sample period and drives idle LOW only on the following
 * alarm.  Without this state the player can observe an empty FIFO immediately
 * after the final sample is fetched and terminate the output too early.
 */
static volatile bool s_finish_pending = false;

static volatile uint8_t s_output_sample = 0;
static volatile bool s_output_sample_valid = false;

/* Used only during sample playback, never for STOP/PAUSE/idle. */
static volatile bool s_read_invert = false;

static inline void IRAM_ATTR pin_out_write_raw(int level)
{
    uint32_t mask = (1UL << PIN_OUT);

    if (level) {
        REG_WRITE(GPIO_OUT_W1TS_REG, mask);
    } else {
        REG_WRITE(GPIO_OUT_W1TC_REG, mask);
    }
}

static inline void IRAM_ATTR pin_out_write_play(int level)
{
    int out = s_read_invert ? !level : level;
    pin_out_write_raw(out);
}

static inline uint32_t fifo_level_snapshot(uint32_t head, uint32_t tail)
{
    return (head - tail) & RAW_FIFO_MASK;
}

static inline bool fifo_has_free_sample(uint32_t head, uint32_t tail)
{
    return (((head + 1U) & RAW_FIFO_MASK) != tail);
}

/*
 * Fetch exactly one following sample into the output latch.  At a normal EOF
 * it deliberately leaves s_running unchanged: on_timer() needs one more
 * sample-clock interval to hold the sample it has just output.
 */
static bool IRAM_ATTR raw_timer_prepare_next_sample(void)
{
    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);

    if (tail == head) {
        if (!s_source_finished) {
            s_underrun = true;
            s_running = false;
        }
        s_output_sample_valid = false;
        return false;
    }

    s_output_sample = s_fifo[tail] ? 1U : 0U;
    tail = (tail + 1U) & RAW_FIFO_MASK;
    __atomic_store_n(&s_tail, tail, __ATOMIC_RELEASE);
    s_output_sample_valid = true;
    return true;
}

static void raw_timer_apply_rate(int sample_rate)
{
    if (sample_rate <= 0) {
        sample_rate = 1;
    }

    uint32_t period_ticks = (uint32_t)(
        (RAW_TIMER_RES_HZ + ((uint32_t)sample_rate / 2U)) /
        (uint32_t)sample_rate
    );

    if (period_ticks == 0U) {
        period_ticks = 1U;
    }

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = period_ticks,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };

    gptimer_set_alarm_action(s_timer, &alarm_config);
    s_sample_rate = sample_rate;
}

static bool IRAM_ATTR on_timer(
    gptimer_handle_t t,
    const gptimer_alarm_event_data_t *edata,
    void *arg)
{
    (void)t;
    (void)edata;
    (void)arg;

    if (!s_running || s_paused) {
        if (s_output_enabled) {
            pin_out_write_raw(0);
        }
        return false;
    }

    /*
     * The last real sample was output during the preceding alarm.  Its full
     * period has now elapsed, so enter idle only here.
     */
    if (s_finish_pending) {
        s_finish_pending = false;
        s_running = false;
        s_output_sample_valid = false;
        if (s_output_enabled) {
            pin_out_write_raw(0);
        }
        return false;
    }

    if (!s_output_sample_valid &&
        !raw_timer_prepare_next_sample()) {
        if (s_output_enabled) {
            pin_out_write_raw(0);
        }
        return false;
    }

    uint8_t sample = s_output_sample;
    s_output_sample_valid = false;

    if (s_output_enabled) {
        pin_out_write_play(sample ? 1 : 0);
    }

    /*
     * Prefetch the following sample.  At EOF the fetch fails but the sample
     * just put on GPIO remains valid until the next timer alarm.
     */
    if (!raw_timer_prepare_next_sample() && s_source_finished) {
        s_finish_pending = true;
    }

    return false;
}

void raw_timer_init(int sample_rate)
{
    gpio_set_direction(PIN_OUT, GPIO_MODE_OUTPUT);
    pin_out_write_raw(0);

    if (!s_initialized) {
        gptimer_config_t config = {
            .clk_src = GPTIMER_CLK_SRC_DEFAULT,
            .direction = GPTIMER_COUNT_UP,
            .resolution_hz = RAW_TIMER_RES_HZ,
            .intr_priority = 3,
        };

        gptimer_new_timer(&config, &s_timer);

        gptimer_event_callbacks_t cbs = {
            .on_alarm = on_timer,
        };
        gptimer_register_event_callbacks(s_timer, &cbs, NULL);
        gptimer_enable(s_timer);

        s_initialized = true;
    }

    if (s_sample_rate != sample_rate) {
        raw_timer_apply_rate(sample_rate);
    }

    __atomic_store_n(&s_head, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tail, 0, __ATOMIC_RELAXED);

    s_running = false;
    s_paused = false;
    s_underrun = false;
    s_source_finished = false;
    s_finish_pending = false;
    s_output_sample = 0;
    s_output_sample_valid = false;

    /* Timer runs continuously; idle ISR keeps GPIO LOW. */
    if (!s_timer_started) {
        gptimer_start(s_timer);
        s_timer_started = true;
    }
}

bool raw_timer_start(void)
{
    if (!s_initialized || s_timer == NULL) {
        return false;
    }

    if (!s_output_sample_valid &&
        !raw_timer_prepare_next_sample()) {
        return false;
    }

    s_running = true;
    s_paused = false;
    s_finish_pending = false;
    return true;
}

void raw_timer_stop(void)
{
    __atomic_store_n(&s_head, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tail, 0, __ATOMIC_RELAXED);

    s_running = false;
    s_paused = false;
    s_underrun = false;
    s_source_finished = false;
    s_finish_pending = false;
    s_output_sample = 0;
    s_output_sample_valid = false;

    pin_out_write_raw(0);
}

void raw_timer_pause(void)
{
    if (!s_initialized) {
        return;
    }

    s_paused = true;
    pin_out_write_raw(0);
}

void raw_timer_resume(void)
{
    if (!s_initialized) {
        return;
    }

    if (s_running && !s_underrun) {
        s_paused = false;
    }
}

/*
 * Lock-free producer write.  The producer must not add data after a normal
 * finish, but accepting it safely here avoids a stale finish-pending state.
 */
size_t raw_timer_write(const uint8_t *data, size_t len)
{
    if (!s_initialized || s_timer == NULL || data == NULL || len == 0U) {
        return 0;
    }

    uint32_t head = __atomic_load_n(&s_head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    size_t written = 0;

    while (written < len) {
        if (!fifo_has_free_sample(head, tail)) {
            break;
        }

        s_fifo[head] = data[written] ? 1U : 0U;
        head = (head + 1U) & RAW_FIFO_MASK;
        written++;
    }

    if (written > 0U) {
        __atomic_store_n(&s_head, head, __ATOMIC_RELEASE);
        s_source_finished = false;
        s_finish_pending = false;
        s_paused = false;
    }

    return written;
}

void raw_timer_finish(void)
{
    if (!s_initialized || s_timer == NULL) {
        return;
    }

    s_source_finished = true;
}

size_t raw_timer_free(void)
{
    uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    uint32_t level = fifo_level_snapshot(head, tail);

    return (RAW_FIFO_SIZE - 1U) - level;
}

size_t raw_timer_level(void)
{
    uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);

    /*
     * finish_pending represents the final sample still held on the GPIO pin.
     * Count it so player.c cannot report EOF and stop the backend early.
     */
    return fifo_level_snapshot(head, tail) +
        (s_output_sample_valid ? 1U : 0U) +
        (s_finish_pending ? 1U : 0U);
}

bool raw_timer_is_running(void)
{
    return s_running;
}

bool raw_timer_is_underrun(void)
{
    return s_underrun;
}

void raw_timer_clear_underrun(void)
{
    s_underrun = false;
}

void raw_timer_set_output_enabled(bool enabled)
{
    s_output_enabled = enabled;

    if (!enabled) {
        s_running = false;
        s_paused = false;
        s_underrun = false;
        s_source_finished = false;
        s_finish_pending = false;
        s_output_sample = 0;
        s_output_sample_valid = false;
    }
}

void raw_timer_set_read_invert(bool invert)
{
    s_read_invert = invert;
}

bool raw_timer_get_read_invert(void)
{
    return s_read_invert;
}
