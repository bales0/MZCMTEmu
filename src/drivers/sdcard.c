#include "storage/fs.h"
#include "sdcard.h"

#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "esp_err.h"

#include <stdio.h>
#include <stdbool.h>

#define PIN_CS   13
#define PIN_MOSI 15
#define PIN_MISO 16
#define PIN_CLK  14

#define SD_MOUNT_POINT "/sd"

static sdmmc_card_t *s_card = NULL;
static bool s_bus_initialized = false;
static bool s_mounted = false;

static void sdcard_gpio_prepare(void)
{
    gpio_set_direction(PIN_MISO, GPIO_MODE_INPUT);
    gpio_set_direction(PIN_MOSI, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_CLK,  GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_CS,   GPIO_MODE_OUTPUT);
}

static bool sdcard_bus_free_internal(void)
{
    if(!s_bus_initialized)return true;
    esp_err_t result=spi_bus_free(SPI2_HOST);
    if(result!=ESP_OK) {printf("SPI bus free FAIL (%d)\n",result);return false;}
    s_bus_initialized=false;return true;
}

static void sdcard_unmount_native(void)
{
    s_mounted=false;
    if(s_card) {
        esp_err_t result=esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT,s_card);
        if(result!=ESP_OK) {printf("SD unmount FAIL (%d)\n",result);return;}
        s_card=NULL;
    }
    (void)sdcard_bus_free_internal();
}

static void sdcard_init_native(void)
{
    if (s_mounted) {
        // už je připravená, nedělej nic
        return;
    }

    /* Failed unmount/free keeps its resource until a retry actually succeeds. */
    if(s_card) {sdcard_unmount_native();if(s_card)return;}
    if(s_bus_initialized && !sdcard_bus_free_internal())return;

    sdcard_gpio_prepare();

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        printf("SPI bus init FAIL (%d)\n", ret);
        s_card = NULL;
        s_mounted = false;
        s_bus_initialized = false;
        return;
    }

    s_bus_initialized = true;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_CS;
    slot.host_id = SPI2_HOST;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    // zachováno: nižší frekvence pro stabilitu
    host.max_freq_khz = 10000;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
    };

    ret = esp_vfs_fat_sdspi_mount(
        SD_MOUNT_POINT,
        &host,
        &slot,
        &mount_config,
        &s_card
    );

    if (ret != ESP_OK) {
        printf("SD mount FAIL (%d)\n", ret);

        s_card = NULL;
        s_mounted = false;

        // po failu uvolni bus, aby šel další insert/mount zkusit znovu čistě
        sdcard_bus_free_internal();
        return;
    }

    s_mounted = true;
    printf("SD mount OK\n");
}

bool sdcard_is_mounted(void)
{
    return s_mounted;
}

int sdcard_test(void)
{
    // kompatibilní "readiness" test bez zápisu do souboru
    return s_mounted ? 1 : 0;
}
static void mount_on_worker(void *context) {(void)context;sdcard_init_native();}
static void unmount_on_worker(void *context) {(void)context;sdcard_unmount_native();}
void sdcard_init(void) {if(!cmt_storage_execute(mount_on_worker,NULL))s_mounted=false;}
void sdcard_unmount(void) {if(!cmt_storage_execute(unmount_on_worker,NULL))s_mounted=false;}
