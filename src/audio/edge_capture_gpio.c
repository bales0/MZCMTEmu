/* Diagnostic fallback only; select CONFIG_MZ_CAPTURE_GPIO_FALLBACK. */
#include "audio/edge_capture.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

#include <stdbool.h>
#include <stdint.h>

#define EDGE_CAPTURE_PIN_IN      GPIO_NUM_5
#define EDGE_CAPTURE_RES_HZ      1000000UL
#define EDGE_CAPTURE_FIFO_SIZE   8192U
#define EDGE_CAPTURE_FIFO_MASK   (EDGE_CAPTURE_FIFO_SIZE - 1U)

static edge_capture_event_t s_fifo[EDGE_CAPTURE_FIFO_SIZE];

/* Producer: GPIO edge ISR. Consumer: recorder task. */
static volatile uint32_t s_head = 0U;
static volatile uint32_t s_tail = 0U;

static gptimer_handle_t s_timer = NULL;
static bool s_initialized = false;
static bool s_timer_started = false;
static bool s_isr_installed = false;

static volatile bool s_running = false;
static volatile bool s_paused = false;
static volatile bool s_overrun = false;
static volatile uint32_t s_isr_depth = 0U;
static volatile uint8_t s_active_level = 0U;
static volatile uint64_t s_last_count = 0U;
static uint32_t s_unit_us = 1U;

static inline uint32_t fifo_level_snapshot(uint32_t head, uint32_t tail)
{
    return (head - tail) & EDGE_CAPTURE_FIFO_MASK;
}

static inline uint8_t IRAM_ATTR edge_capture_pin_level(void)
{
    return (REG_READ(GPIO_IN_REG) & (1UL << EDGE_CAPTURE_PIN_IN)) ? 1U : 0U;
}

static uint32_t IRAM_ATTR edge_capture_elapsed_to_units(uint64_t elapsed_us)
{
    uint32_t unit_us = s_unit_us == 0U ? 1U : s_unit_us;
    uint64_t units = (elapsed_us + (unit_us / 2U)) / unit_us;

    if (units == 0U) {
        units = 1U;
    }
    if (units > UINT32_MAX) {
        units = UINT32_MAX;
    }
    return (uint32_t)units;
}

static bool IRAM_ATTR edge_capture_push(
    uint8_t level,
    uint32_t units,
    uint8_t rising)
{
    const uint32_t head = __atomic_load_n(&s_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    const uint32_t next = (head + 1U) & EDGE_CAPTURE_FIFO_MASK;

    if (next == tail) {
        s_overrun = true;
        s_running = false;
        return false;
    }

    s_fifo[head].level = level ? 1U : 0U;
    s_fifo[head].rising = rising ? 1U : 0U;
    s_fifo[head].units = units == 0U ? 1U : units;
    __atomic_store_n(&s_head, next, __ATOMIC_RELEASE);
    return true;
}

/* Must be called with the GPIO interrupt disabled or from that ISR. */
static void IRAM_ATTR edge_capture_close_interval(bool from_edge, uint8_t new_level)
{
    uint64_t now = 0U;
    uint64_t elapsed;
    const uint8_t old_level = s_active_level ? 1U : 0U;

    if (!s_initialized || s_timer == NULL || !s_running) {
        return;
    }

    if (gptimer_get_raw_count(s_timer, &now) != ESP_OK) {
        return;
    }

    elapsed = now >= s_last_count ? now - s_last_count : 0U;
    if (elapsed != 0U) {
        if (!edge_capture_push(old_level,
                               edge_capture_elapsed_to_units(elapsed),
                               from_edge && old_level == 0U && new_level != 0U)) {
            return;
        }
    }

    s_last_count = now;
    s_active_level = new_level ? 1U : 0U;
}

static void IRAM_ATTR edge_capture_gpio_isr(void *arg)
{
    const uint8_t level = edge_capture_pin_level();

    (void)arg;
    __atomic_fetch_add(&s_isr_depth, 1U, __ATOMIC_RELAXED);

    if (s_running && !s_paused && !s_overrun && level != s_active_level) {
        edge_capture_close_interval(true, level);
    }

    __atomic_fetch_sub(&s_isr_depth, 1U, __ATOMIC_RELEASE);
}

static void edge_capture_wait_isr_idle(void)
{
    while (__atomic_load_n(&s_isr_depth, __ATOMIC_ACQUIRE) != 0U) {
        vTaskDelay(1);
    }
}

void edge_capture_init(uint32_t unit_us)
{
    gpio_config_t input_cfg = {
        .pin_bit_mask = (1ULL << EDGE_CAPTURE_PIN_IN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    (void)gpio_config(&input_cfg);
    (void)gpio_intr_disable(EDGE_CAPTURE_PIN_IN);

    s_unit_us = unit_us == 0U ? 1U : unit_us;

    if (!s_initialized) {
        gptimer_config_t config = {
            .clk_src = GPTIMER_CLK_SRC_DEFAULT,
            .direction = GPTIMER_COUNT_UP,
            .resolution_hz = EDGE_CAPTURE_RES_HZ,
            .intr_priority = 3,
        };

        if (gptimer_new_timer(&config, &s_timer) != ESP_OK ||
            gptimer_enable(s_timer) != ESP_OK) {
            s_timer = NULL;
            s_initialized = false;
            s_overrun = true;
            return;
        }
        s_initialized = true;
    }

    if (!s_isr_installed) {
        const esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            s_isr_installed = true;
        }
    }

    if (s_isr_installed) {
        (void)gpio_isr_handler_remove(EDGE_CAPTURE_PIN_IN);
        if (gpio_isr_handler_add(EDGE_CAPTURE_PIN_IN,
                                 edge_capture_gpio_isr,
                                 NULL) != ESP_OK) {
            s_overrun = true;
            return;
        }
    } else {
        s_overrun = true;
        return;
    }

    __atomic_store_n(&s_head, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tail, 0U, __ATOMIC_RELAXED);
    s_running = false;
    s_paused = false;
    s_overrun = false;

    if (!s_timer_started) {
        if (gptimer_start(s_timer) != ESP_OK) {
            s_overrun = true;
            return;
        }
        s_timer_started = true;
    }
}

void edge_capture_start(void)
{
    uint64_t now = 0U;

    if (!s_initialized || s_timer == NULL || !s_isr_installed) {
        s_overrun = true;
        return;
    }

    (void)gpio_intr_disable(EDGE_CAPTURE_PIN_IN);
    edge_capture_wait_isr_idle();

    __atomic_store_n(&s_head, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tail, 0U, __ATOMIC_RELAXED);

    (void)gptimer_get_raw_count(s_timer, &now);
    s_last_count = now;
    s_active_level = edge_capture_pin_level();
    s_overrun = false;
    s_paused = false;
    s_running = true;

    (void)gpio_intr_enable(EDGE_CAPTURE_PIN_IN);
}

void edge_capture_stop(void)
{
    (void)gpio_intr_disable(EDGE_CAPTURE_PIN_IN);
    edge_capture_wait_isr_idle();
    s_running = false;
    s_paused = false;
    s_overrun = false;
}

void edge_capture_pause(void)
{
    if (!s_initialized || !s_running || s_paused) {
        return;
    }

    /* No sample is accepted after this point; preserve the exact pre-pause tail. */
    (void)gpio_intr_disable(EDGE_CAPTURE_PIN_IN);
    edge_capture_wait_isr_idle();
    edge_capture_close_interval(false, s_active_level);
    s_paused = true;
}

void edge_capture_resume(void)
{
    uint64_t now = 0U;

    if (!s_initialized || !s_running || !s_paused || s_overrun) {
        return;
    }

    (void)gptimer_get_raw_count(s_timer, &now);
    s_last_count = now;
    s_active_level = edge_capture_pin_level();
    s_paused = false;
    (void)gpio_intr_enable(EDGE_CAPTURE_PIN_IN);
}

size_t edge_capture_read(edge_capture_event_t *dst, size_t max_count)
{
    uint32_t tail;
    const uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    size_t count = 0U;

    if (!s_initialized || dst == NULL || max_count == 0U) {
        return 0U;
    }

    tail = __atomic_load_n(&s_tail, __ATOMIC_RELAXED);
    while (count < max_count && tail != head) {
        dst[count++] = s_fifo[tail];
        tail = (tail + 1U) & EDGE_CAPTURE_FIFO_MASK;
    }

    if (count != 0U) {
        __atomic_store_n(&s_tail, tail, __ATOMIC_RELEASE);
    }
    return count;
}

size_t edge_capture_free(void)
{
    const uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    return (EDGE_CAPTURE_FIFO_SIZE - 1U) - fifo_level_snapshot(head, tail);
}

size_t edge_capture_level(void)
{
    const uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    return fifo_level_snapshot(head, tail);
}

bool edge_capture_is_running(void)
{
    return s_running;
}

bool edge_capture_is_overrun(void)
{
    return s_overrun;
}

void edge_capture_clear_overrun(void)
{
    s_overrun = false;
}
