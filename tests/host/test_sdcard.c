#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "drivers/sdcard.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "driver/gpio.h"
static bool bus,mounted;
static int init_error,mount_error,unmount_error,free_error;
static unsigned init_calls,mount_calls,free_calls,unmount_calls;
static sdmmc_card_t card;
esp_err_t gpio_set_direction(int gpio,int mode) {assert(gpio>=13 && gpio<=16);assert(mode==(gpio==16 ? GPIO_MODE_INPUT:GPIO_MODE_OUTPUT));return ESP_OK;}
esp_err_t spi_bus_initialize(int host,const spi_bus_config_t *config,int dma) {
 ++init_calls;assert(host==SPI2_HOST && dma==SPI_DMA_CH_AUTO && !bus && config->mosi_io_num==15 && config->miso_io_num==16 && config->sclk_io_num==14);
 if(init_error)return -1;bus=true;return ESP_OK;
}
esp_err_t spi_bus_free(int host) {++free_calls;assert(host==SPI2_HOST && bus && !mounted);if(free_error)return -1;bus=false;return ESP_OK;}
esp_err_t esp_vfs_fat_sdspi_mount(const char *path,const sdmmc_host_t *host,const sdspi_device_config_t *slot,const esp_vfs_fat_mount_config_t *config,sdmmc_card_t **out) {
 ++mount_calls;assert(!strcmp(path,"/sd") && host->slot==SPI2_HOST && host->max_freq_khz==10000 && slot->gpio_cs==13 && !config->format_if_mount_failed && bus && !mounted);
 if(mount_error)return -1;mounted=true;*out=&card;return ESP_OK;
}
esp_err_t esp_vfs_fat_sdcard_unmount(const char *path,sdmmc_card_t *value) {++unmount_calls;assert(!strcmp(path,"/sd") && value==&card && mounted);if(unmount_error)return -1;mounted=false;return ESP_OK;}
int main(void) {
 init_error=1;sdcard_init();assert(!sdcard_is_mounted() && !bus);init_error=0;
 mount_error=1;sdcard_init();assert(!sdcard_is_mounted() && !bus);mount_error=0;
 sdcard_init();assert(sdcard_is_mounted() && bus && mounted && sdcard_test());unsigned count=init_calls;sdcard_init();assert(init_calls==count);
 unmount_error=1;sdcard_unmount();assert(!sdcard_is_mounted() && bus && mounted);count=mount_calls;sdcard_init();assert(mount_calls==count && mounted);
 unmount_error=0;sdcard_init();assert(sdcard_is_mounted() && mounted && bus);
 free_error=1;sdcard_unmount();assert(!sdcard_is_mounted() && !mounted && bus);count=init_calls;sdcard_init();assert(init_calls==count && bus);
 free_error=0;sdcard_init();assert(sdcard_is_mounted() && mounted && bus);sdcard_unmount();assert(!bus && !mounted && !sdcard_is_mounted());
 assert(init_calls>2 && free_calls>2 && unmount_calls>2);
 puts("PASS real SD lifecycle driver: SPI DMA and pinmap preserved, init/mount/unmount/free failures retain resources until successful retry; no format/write probe");return 0;
}
