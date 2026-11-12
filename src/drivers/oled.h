#pragma once
#include <stdint.h>

void oled_init(void);
void oled_reset(void);
void oled_clear(void);
void oled_draw_text(int row, const char *text);
void oled_update(void);
void oled_update_pages(uint8_t page_mask);
void oled_draw_test_pattern(void);