#include "audio/raw_capture.h"

#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PIN_IN 5

#define RAW_CAPTURE_RES_HZ 40000000UL   // 40 MHz

// Musí být mocnina 2. FIFO ukládá 8 logických sample do jednoho bajtu.
#define RAW_CAPTURE_FIFO_SIZE 65536U
#define RAW_CAPTURE_FIFO_MASK (RAW_CAPTURE_FIFO_SIZE - 1U)

// mapování logických úrovní do 8bit WAV
#define RAW_CAPTURE_LEVEL_LOW   20U
#define RAW_CAPTURE_LEVEL_HIGH 235U

static uint8_t s_fifo[RAW_CAPTURE_FIFO_SIZE];

// SPSC ring buffer:
// - producer (ISR) zapisuje pouze s_head
// - consumer (task) zapisuje pouze s_tail
static volatile uint32_t s_head = 0;
static volatile uint32_t s_tail = 0;

static gptimer_handle_t s_timer = NULL;
static bool s_initialized = false;
static bool s_timer_started = false;
static int s_sample_rate = 0;

static volatile bool s_running = false;
static volatile bool s_paused = false;
static volatile bool s_overrun = false;

static volatile uint8_t s_pack_byte = 0;
static volatile uint8_t s_pack_bits = 0;
static uint8_t s_read_byte = 0;
static uint8_t s_read_bits = 0;

// ========================================================
// rychlé čtení GPIO5 přímo přes registry
// ========================================================
static inline int IRAM_ATTR pin_in_read_raw(void)
{
    uint32_t in = REG_READ(GPIO_IN_REG);
    return (int)((in >> PIN_IN) & 1U);
}

// ========================================================
// interní helper: mapování 0/1 -> 8bit WAV level
// 0 -> 20
// 1 -> 235
// ========================================================
static inline uint8_t IRAM_ATTR map_logic_to_wav_sample(int level)
{
    return level ? (uint8_t)RAW_CAPTURE_LEVEL_HIGH
                 : (uint8_t)RAW_CAPTURE_LEVEL_LOW;
}

// ========================================================
// interní helper: aktuální počet sample ve FIFO
// ========================================================
static inline uint32_t fifo_level_snapshot(uint32_t head, uint32_t tail)
{
    return (head - tail) & RAW_CAPTURE_FIFO_MASK;
}

static inline bool fifo_has_free_byte(uint32_t head, uint32_t tail)
{
    return (((head + 1U) & RAW_CAPTURE_FIFO_MASK) != tail);
}

// ========================================================
// interní helper: nastavení sample rate
// ========================================================
static void raw_capture_apply_rate(int sample_rate)
{
    if (sample_rate <= 0) {
        sample_rate = 1;
    }

    uint32_t period_ticks = (uint32_t)(
        (RAW_CAPTURE_RES_HZ + ((uint32_t)sample_rate / 2U)) / (uint32_t)sample_rate
    );

    if (period_ticks == 0) {
        period_ticks = 1;
    }

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = period_ticks,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };

    gptimer_set_alarm_action(s_timer, &alarm_config);
    s_sample_rate = sample_rate;
}

// ========================================================
// ISR sampler
// ========================================================
static bool IRAM_ATTR on_capture_timer(
    gptimer_handle_t t,
    const gptimer_alarm_event_data_t *edata,
    void *arg)
{
    (void)t;
    (void)edata;
    (void)arg;

    if (!s_running || s_paused) {
        return false;
    }

    uint32_t head = __atomic_load_n(&s_head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    // FIFO full -> overrun. Při rozpracovaném bajtu máme slot rezervovaný
    // z okamžiku prvního bitu, protože consumer může prostor jen uvolňovat.
    if (s_pack_bits == 0U && !fifo_has_free_byte(head, tail)) {
        s_overrun = true;
        s_running = false;
        return false;
    }

    int level = pin_in_read_raw();
    if (level) {
        s_pack_byte |= (uint8_t)(1U << s_pack_bits);
    }

    s_pack_bits++;

    if (s_pack_bits == 8U) {
        s_fifo[head] = s_pack_byte;
        head = (head + 1U) & RAW_CAPTURE_FIFO_MASK;
        s_pack_byte = 0;
        s_pack_bits = 0;
        __atomic_store_n(&s_head, head, __ATOMIC_RELEASE);
    }

    return false;
}

// ========================================================
void raw_capture_init(int sample_rate)
{
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << PIN_IN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   // nově interní pull-down
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    if (!s_initialized) {
        gptimer_config_t config = {
            .clk_src = GPTIMER_CLK_SRC_DEFAULT,
            .direction = GPTIMER_COUNT_UP,
            .resolution_hz = RAW_CAPTURE_RES_HZ,
            .intr_priority = 3,
        };

        gptimer_new_timer(&config, &s_timer);

        gptimer_event_callbacks_t cbs = {
            .on_alarm = on_capture_timer,
        };
        gptimer_register_event_callbacks(s_timer, &cbs, NULL);
        gptimer_enable(s_timer);

        s_initialized = true;
    }

    if (s_sample_rate != sample_rate) {
        raw_capture_apply_rate(sample_rate);
    }

    __atomic_store_n(&s_head, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tail, 0, __ATOMIC_RELAXED);

    s_running = false;
    s_paused = false;
    s_overrun = false;
    s_pack_byte = 0;
    s_pack_bits = 0;
    s_read_byte = 0;
    s_read_bits = 0;

    // Timer může běžet pořád; sampling je aktivní jen při s_running=true
    if (!s_timer_started) {
        gptimer_start(s_timer);
        s_timer_started = true;
    }
}

// ========================================================
void raw_capture_start(void)
{
    if (!s_initialized) {
        return;
    }

    __atomic_store_n(&s_head, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tail, 0, __ATOMIC_RELAXED);

    s_overrun = false;
    s_paused = false;
    s_pack_byte = 0;
    s_pack_bits = 0;
    s_read_byte = 0;
    s_read_bits = 0;
    s_running = true;
}

// ========================================================
void raw_capture_stop(void)
{
    __atomic_store_n(&s_head, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tail, 0, __ATOMIC_RELAXED);

    s_running = false;
    s_paused = false;
    s_overrun = false;
    s_pack_byte = 0;
    s_pack_bits = 0;
    s_read_byte = 0;
    s_read_bits = 0;
}

// ========================================================
void raw_capture_pause(void)
{
    if (!s_initialized) {
        return;
    }

    s_paused = true;
}

// ========================================================
void raw_capture_resume(void)
{
    if (!s_initialized) {
        return;
    }

    if (s_running && !s_overrun) {
        s_paused = false;
    }
}

// ========================================================
void raw_capture_flush_partial(void)
{
    if (!s_initialized || s_pack_bits == 0U) {
        return;
    }

    uint32_t head = __atomic_load_n(&s_head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);

    if (!fifo_has_free_byte(head, tail)) {
        s_overrun = true;
        s_running = false;
        return;
    }

    s_fifo[head] = s_pack_byte;
    head = (head + 1U) & RAW_CAPTURE_FIFO_MASK;
    s_pack_byte = 0;
    s_pack_bits = 0;
    __atomic_store_n(&s_head, head, __ATOMIC_RELEASE);
}

// ========================================================
// consumer read: čtení z FIFO do task kontextu
// ========================================================
size_t raw_capture_read(uint8_t *dst, size_t max_len)
{
    if (!s_initialized || dst == NULL || max_len == 0) {
        return 0;
    }

    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);

    size_t n = 0;

    while (n < max_len) {
        if (s_read_bits == 0U) {
            if (tail == head) {
                break;
            }

            s_read_byte = s_fifo[tail];
            s_read_bits = 8U;
            tail = (tail + 1U) & RAW_CAPTURE_FIFO_MASK;
        }

        dst[n++] = map_logic_to_wav_sample((int)(s_read_byte & 1U));
        s_read_byte >>= 1U;
        s_read_bits--;
    }

    if (n > 0) {
        __atomic_store_n(&s_tail, tail, __ATOMIC_RELEASE);
    }

    return n;
}

// ========================================================
size_t raw_capture_free(void)
{
    uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    uint32_t level = fifo_level_snapshot(head, tail);
    uint32_t free_bytes = (RAW_CAPTURE_FIFO_SIZE - 1U) - level;
    uint32_t free_samples = free_bytes * 8U;

    if (s_pack_bits != 0U && free_samples >= s_pack_bits) {
        free_samples -= s_pack_bits;
    }

    return free_samples;
}

// ========================================================
size_t raw_capture_level(void)
{
    uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    return (fifo_level_snapshot(head, tail) * 8U) + s_pack_bits + s_read_bits;
}

// ========================================================
bool raw_capture_is_running(void)
{
    return s_running;
}

// ========================================================
bool raw_capture_is_overrun(void)
{
    return s_overrun;
}

// ========================================================
void raw_capture_clear_overrun(void)
{
    s_overrun = false;
}
