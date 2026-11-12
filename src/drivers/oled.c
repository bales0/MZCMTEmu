#include "oled.h"
#include "i2c.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include "font6x8.h"

#define ADDR 0x3C

static uint8_t fb[128 * 8];

static void cmd(uint8_t c)
{
    uint8_t d[2] = {0x00, c};
    i2c_write_to_device(ADDR, d, 2, 100);
}

static void oled_update_page(int page)
{
    if (page < 0 || page > 7) {
        return;
    }

    cmd(0xB0 | page);
    cmd(0x00);
    cmd(0x10);

    uint8_t buf[129];
    buf[0] = 0x40;
    memcpy(&buf[1], &fb[page * 128], 128);
    i2c_write_to_device(ADDR, buf, 129, 100);
}

void oled_reset(void)
{
    vTaskDelay(pdMS_TO_TICKS(100));

    cmd(0xAE); // display OFF
    cmd(0xD5); cmd(0x80);
    cmd(0xA8); cmd(0x3F);
    cmd(0xD3); cmd(0x00);
    cmd(0x40);
    cmd(0x8D); cmd(0x14);
    cmd(0x20); cmd(0x00);
    cmd(0xA1);
    cmd(0xC8);
    cmd(0xDA); cmd(0x12);
    cmd(0x81); cmd(0xCF);
    cmd(0xD9); cmd(0xF1);
    cmd(0xDB); cmd(0x40);
    cmd(0xA4);
    cmd(0xA6);
    cmd(0xAF); // display ON

    vTaskDelay(pdMS_TO_TICKS(50));
}

void oled_init(void)
{
    oled_reset();
    oled_clear();
    oled_update();
}

void oled_clear(void)
{
    memset(fb, 0, sizeof(fb));
}

void oled_update_pages(uint8_t page_mask)
{
    for (int page = 0; page < 8; page++) {
        if (page_mask & (1u << page)) {
            oled_update_page(page);
        }
    }
}

void oled_update(void)
{
    oled_update_pages(0xFF);
}

void oled_draw_text(int row, const char *text)
{
    int x = 0;
    int page = row;

    if (page < 0 || page > 7) {
        return;
    }

    while (*text && x < 122) {
        char c = *text++;
        if (c < 32 || c > 127) {
            c = ' ';
        }

        const uint8_t *glyph = font6x8[c - 32];
        for (int i = 0; i < 6; i++) {
            fb[page * 128 + x] = glyph[i];
            x++;
        }
    }
}