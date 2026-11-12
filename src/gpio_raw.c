#include "gpio_raw.h"
#include "driver/gpio.h"
#include <stdbool.h>

#define PIN_READ_OUT 4
#define PIN_WRITE_IN 5

static bool s_read_invert = false;

static inline int apply_read_invert(int level)
{
    return s_read_invert ? !level : level;
}

void gpio_raw_init(void)
{
    gpio_set_direction(PIN_READ_OUT, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_WRITE_IN, GPIO_MODE_INPUT);

    // default idle level = raw LOW, přes finální invert helper
    gpio_set_level(PIN_READ_OUT, apply_read_invert(0));
}

void gpio_raw_set_read_invert(bool invert)
{
    s_read_invert = invert;

    // po změně nastavení okamžitě srovnej idle/output level
    // (bezpečný default = raw LOW)
    gpio_set_level(PIN_READ_OUT, apply_read_invert(0));
}

bool gpio_raw_get_read_invert(void)
{
    return s_read_invert;
}

void gpio_raw_write(uint8_t sample)
{
    int raw_level = sample ? 1 : 0;
    gpio_set_level(PIN_READ_OUT, apply_read_invert(raw_level));
}

uint8_t gpio_raw_read(void)
{
    return gpio_get_level(PIN_WRITE_IN);
}