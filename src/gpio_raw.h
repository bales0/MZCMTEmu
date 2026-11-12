#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gpio_raw_init(void);
void gpio_raw_set_read_invert(bool invert);
bool gpio_raw_get_read_invert(void);
void gpio_raw_write(uint8_t sample);
uint8_t gpio_raw_read(void);

#ifdef __cplusplus
}
#endif