#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

void i2c_init(void);
esp_err_t i2c_write_to_device(uint8_t addr, const uint8_t *data, size_t len, uint32_t timeout_ms);
