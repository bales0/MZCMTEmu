#include "i2c.h"

#include <stdbool.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_check.h"

#define I2C_PORT I2C_NUM_0
#define SDA 17
#define SCL 18
#define I2C_FREQ_HZ 400000
#define I2C_GLITCH_IGNORE_CNT 7

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_dev_cache[128] = {0};
static bool s_i2c_initialized = false;

static i2c_master_dev_handle_t i2c_get_device_handle(uint8_t addr)
{
    if (addr >= 128) {
        return NULL;
    }

    if (s_dev_cache[addr] != NULL) {
        return s_dev_cache[addr];
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &dev) != ESP_OK) {
        return NULL;
    }

    s_dev_cache[addr] = dev;
    return dev;
}

void i2c_init(void)
{
    if (s_i2c_initialized) {
        return;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = SDA,
        .scl_io_num = SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = I2C_GLITCH_IGNORE_CNT,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    s_i2c_initialized = true;
}

esp_err_t i2c_write_to_device(uint8_t addr, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (!s_i2c_initialized || s_i2c_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t dev = i2c_get_device_handle(addr);
    if (dev == NULL) {
        return ESP_FAIL;
    }

    return i2c_master_transmit(dev, data, len, (int)timeout_ms);
}
