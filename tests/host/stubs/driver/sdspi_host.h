#pragma once
#include "sdmmc_cmd.h"
typedef struct {int gpio_cs,host_id;} sdspi_device_config_t;
#define SDSPI_DEVICE_CONFIG_DEFAULT() ((sdspi_device_config_t){0})
#define SDSPI_HOST_DEFAULT() ((sdmmc_host_t){0})
