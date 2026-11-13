#pragma once
#include "esp_err.h"
#define SPI2_HOST 2
#define SPI_DMA_CH_AUTO 3
typedef struct {int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,max_transfer_sz;} spi_bus_config_t;
esp_err_t spi_bus_initialize(int,const spi_bus_config_t *,int);
esp_err_t spi_bus_free(int);
