#pragma once
#include <stdint.h>
#include <stdbool.h>

void oled_init(void);
void oled_reset(void);
void oled_clear(void);
void oled_draw_text(int row, const char *text);
void oled_update(void);
/* Bit mask of pages successfully transferred; failures stay dirty in UI. */
uint8_t oled_update_pages(uint8_t page_mask);
void oled_draw_test_pattern(void);
bool oled_set_brightness(uint8_t percent);
