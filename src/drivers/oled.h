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

void oled_canvas_begin(uint8_t page,uint8_t bytes[128]);
void oled_canvas_end(void);
void oled_force_refresh(void);
uint8_t oled_dirty_pages(void);
const uint8_t *oled_framebuffer(void);
void oled_pixel(int x,int y,bool white);
void oled_rect(int x,int y,int width,int height,bool filled,bool white);
void oled_text_xy(int x,int y,const char *,int size,bool white);
void oled_bar(int x,int y,int width,int height,unsigned percent);
