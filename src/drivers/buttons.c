#include "buttons.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_BTN_PLAY  8
#define PIN_BTN_STOP  9
#define PIN_BTN_NEXT  10
#define PIN_BTN_PREV  11
#define PIN_BTN_REC   42

#define BUTTONS_POLL_MS        20
#define BUTTON_REPEAT_DELAY_MS 400
#define BUTTON_REPEAT_RATE_MS  120
#define BUTTON_STOP_LONG_MS    800

static uint32_t last_time = 0;

// stav tlačítek (1 = puštěno, 0 = stisknuto)
static uint8_t last_play = 1;
static uint8_t last_stop = 1;
static uint8_t last_next = 1;
static uint8_t last_prev = 1;
static uint8_t last_rec  = 1;

// hold/repeat stav
static TickType_t next_press_time = 0;
static TickType_t next_repeat_time = 0;

static TickType_t prev_press_time = 0;
static TickType_t prev_repeat_time = 0;

static TickType_t stop_press_time = 0;
static bool stop_long_sent = false;
static bool s_last_event_repeat = false;

void buttons_init(void)
{
    gpio_config_t cfg = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pin_bit_mask =
            (1ULL << PIN_BTN_PLAY) |
            (1ULL << PIN_BTN_STOP) |
            (1ULL << PIN_BTN_NEXT) |
            (1ULL << PIN_BTN_PREV) |
            (1ULL << PIN_BTN_REC)
    };
    gpio_config(&cfg);
}

btn_event_t buttons_poll(void)
{
    TickType_t now = xTaskGetTickCount();

    if ((now - last_time) < pdMS_TO_TICKS(BUTTONS_POLL_MS)) {
        return BTN_NONE;
    }
    last_time = now;

    uint8_t play = gpio_get_level(PIN_BTN_PLAY);
    uint8_t stop = gpio_get_level(PIN_BTN_STOP);
    uint8_t next = gpio_get_level(PIN_BTN_NEXT);
    uint8_t prev = gpio_get_level(PIN_BTN_PREV);
    uint8_t rec  = gpio_get_level(PIN_BTN_REC);

    btn_event_t event = BTN_NONE;
    s_last_event_repeat = false;

    // ----------------------------------------------------
    // PLAY - jen edge na stisk
    // ----------------------------------------------------
    if (last_play == 1 && play == 0) {
        event = BTN_PLAY;
    }

    // ----------------------------------------------------
    // STOP - short až na release, long při držení
    // ----------------------------------------------------
    if (event == BTN_NONE) {
        // nový stisk STOP
        if (last_stop == 1 && stop == 0) {
            stop_press_time = now;
            stop_long_sent = false;
        }
        // držení STOP -> long press
        else if (last_stop == 0 && stop == 0 && !stop_long_sent) {
            if ((now - stop_press_time) >= pdMS_TO_TICKS(BUTTON_STOP_LONG_MS)) {
                stop_long_sent = true;
                event = BTN_STOP_LONG;
            }
        }
        // release STOP -> short press jen pokud neproběhl long
        else if (last_stop == 0 && stop == 1) {
            if (!stop_long_sent) {
                event = BTN_STOP;
            }
            stop_long_sent = false;
        }
    }

    // ----------------------------------------------------
    // NEXT - edge + autorepeat
    // ----------------------------------------------------
    if (event == BTN_NONE) {
        if (last_next == 1 && next == 0) {
            next_press_time = now;
            next_repeat_time = now + pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS);
            event = BTN_NEXT;
        } else if (last_next == 0 && next == 0) {
            if ((now - next_press_time) >= pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS) &&
                now >= next_repeat_time) {
                next_repeat_time = now + pdMS_TO_TICKS(BUTTON_REPEAT_RATE_MS);
                event = BTN_NEXT;
                s_last_event_repeat = true;
            }
        }
    }

    // ----------------------------------------------------
    // PREV - edge + autorepeat
    // ----------------------------------------------------
    if (event == BTN_NONE) {
        if (last_prev == 1 && prev == 0) {
            prev_press_time = now;
            prev_repeat_time = now + pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS);
            event = BTN_PREV;
        } else if (last_prev == 0 && prev == 0) {
            if ((now - prev_press_time) >= pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS) &&
                now >= prev_repeat_time) {
                prev_repeat_time = now + pdMS_TO_TICKS(BUTTON_REPEAT_RATE_MS);
                event = BTN_PREV;
                s_last_event_repeat = true;
            }
        }
    }

    // ----------------------------------------------------
    // REC - jen edge na stisk
    // ----------------------------------------------------
    if (event == BTN_NONE) {
        if (last_rec == 1 && rec == 0) {
            event = BTN_REC;
        }
    }

    last_play = play;
    last_stop = stop;
    last_next = next;
    last_prev = prev;
    last_rec  = rec;

    return event;
}

bool buttons_last_event_was_repeat(void)
{
    return s_last_event_repeat;
}

void buttons_delay_repeat(btn_event_t button, uint32_t delay_ms)
{
    TickType_t until = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);

    if (button == BTN_NEXT) {
        next_repeat_time = until;
    } else if (button == BTN_PREV) {
        prev_repeat_time = until;
    }
}
