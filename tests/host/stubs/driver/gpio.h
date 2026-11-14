#pragma once
#include <stdint.h>
#include "esp_err.h"
#define GPIO_NUM_4 4
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
typedef struct { uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type; } gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *cfg);

#define GPIO_NUM_5 5
#define GPIO_NUM_7 7
#define GPIO_NUM_41 41
#define GPIO_MODE_INPUT 0
#define GPIO_PULLUP_ENABLE 1
esp_err_t gpio_set_direction(int gpio,int mode);
esp_err_t gpio_set_level(int gpio,int level);
int gpio_get_level(int gpio);

#define GPIO_PULLDOWN_ENABLE 1

esp_err_t gpio_reset_pin(int gpio);

#define GPIO_NUM_6 6
#define GPIO_MODE_INPUT_OUTPUT 3
