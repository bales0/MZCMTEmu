#include "raw_stream.h"
#include "gpio_raw.h"
#include "esp_timer.h"

static volatile const uint8_t *tx_buf;
static volatile int tx_len;
static volatile int tx_pos;

static void timer_cb(void *arg)
{
    if (tx_pos < tx_len) {
        gpio_raw_write(tx_buf[tx_pos++]);
    }
}

void raw_stream_start(const uint8_t *data, int len, int samplerate)
{
    tx_buf = data;
    tx_len = len;
    tx_pos = 0;

    const esp_timer_create_args_t t = {
        .callback = timer_cb
    };

    esp_timer_handle_t timer;
    esp_timer_create(&t, &timer);

    int period_us = 1000000 / samplerate;
    esp_timer_start_periodic(timer, period_us);
}
