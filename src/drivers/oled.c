#include "oled.h"
#include "i2c.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include "font6x8.h"
#include "font3x5.h"
#include "ui/key_icons.h"

/* Five button glyphs share the readable 6x8 cell and its spacing. */
static const uint8_t key_glyphs[5][6]={
 {0xfe,0x7c,0x38,0x38,0x10,0}, /* PLAY */
 {0x7c,0x7c,0x7c,0x7c,0x7c,0}, /* STOP */
 {0x38,0x7c,0x7c,0x7c,0x38,0}, /* RECORD */
 {0xfe,0x10,0x38,0x7c,0xfe,0}, /* PREVIOUS */
 {0xfe,0x7c,0x38,0x10,0xfe,0}  /* NEXT */
};
static bool key_glyph(unsigned char c){return c>=UI_KEY_PLAY_CODE && c<=UI_KEY_NEXT_CODE;}
static const uint8_t *text_glyph(unsigned char c){
 if(key_glyph(c))return key_glyphs[c-UI_KEY_PLAY_CODE];
 if(c<32 || c>127)c=' ';
 return font6x8[c-32];
}

#define ADDR 0x3C
#define OLED_I2C_TIMEOUT_MS 10U

static uint8_t fb[128 * 8];
static uint8_t dirty_pages,canvas_page;
static uint8_t *canvas;

static void cmd(uint8_t c)
{
    uint8_t d[2] = {0x00, c};
    i2c_write_to_device(ADDR, d, 2, OLED_I2C_TIMEOUT_MS);
}

static bool oled_update_page(int page)
{
    if(page<0 || page>7)return false;
    /* SSD1306 page mode: one command packet then one 128-byte page. */
    uint8_t address[]={0x00,(uint8_t)(0xB0|page),0x00,0x10};
    if(i2c_write_to_device(ADDR,address,sizeof(address),OLED_I2C_TIMEOUT_MS)!=ESP_OK)return false;
    uint8_t bytes[129];
    bytes[0]=0x40;
    memcpy(bytes+1,fb+page*128,128);
    return i2c_write_to_device(ADDR,bytes,sizeof(bytes),OLED_I2C_TIMEOUT_MS)==ESP_OK;
}

void oled_reset(void)
{
    vTaskDelay(pdMS_TO_TICKS(100));

    cmd(0xAE);
    // display OFF
    cmd(0xD5);
    cmd(0x80);
    cmd(0xA8);
    cmd(0x3F);
    cmd(0xD3);
    cmd(0x00);
    cmd(0x40);
    cmd(0x8D);
    cmd(0x14);
    cmd(0x20);
    cmd(0x02);
    // page addressing matches B0/column update commands
    cmd(0xA1);
    cmd(0xC8);
    cmd(0xDA);
    cmd(0x12);
    cmd(0x81);
    cmd(0xCF);
    cmd(0xD9);
    cmd(0xF1);
    cmd(0xDB);
    cmd(0x40);
    cmd(0xA4);
    cmd(0xA6);
    cmd(0xAF);
    // display ON

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

uint8_t oled_update_pages(uint8_t page_mask)
{
    dirty_pages|=page_mask;
    uint8_t completed=0;
    for(int page=0;page<8;++page)if(page_mask & (1U<<page)) {
        if(!oled_update_page(page))break;
        // missing OLED costs one timeout, not eight pages
        completed|=(uint8_t)(1U<<page);
        dirty_pages&=(uint8_t)~(1U<<page);
    }
    return completed;
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
        unsigned char c = (unsigned char)*text++;
        if (!key_glyph(c) && (c < 32 || c > 127)) {
            c = ' ';
        }

        const uint8_t *glyph = text_glyph(c);
        for (int i = 0; i < 6; i++) {
            fb[page * 128 + x] = glyph[i];
            x++;
        }
    }
}
bool oled_set_brightness(uint8_t percent) {
    if(percent<10 || percent>100)return false;
    uint8_t data[]={0x00,0x81,(uint8_t)(((unsigned)percent*255+50)/100)};
    return i2c_write_to_device(ADDR,data,sizeof(data),OLED_I2C_TIMEOUT_MS)==ESP_OK;
}

void oled_canvas_begin(uint8_t page,uint8_t bytes[128]){canvas_page=page;
canvas=bytes;
memset(bytes,0,128);
}
void oled_canvas_end(void){
 if(canvas && memcmp(canvas,fb+canvas_page*128,128)){memcpy(fb+canvas_page*128,canvas,128);
 dirty_pages|=(uint8_t)(1U<<canvas_page);
 }
 canvas=NULL;
}
void oled_force_refresh(void){dirty_pages=255;
}
uint8_t oled_dirty_pages(void){return dirty_pages;
}
const uint8_t *oled_framebuffer(void){return fb;
}
void oled_pixel(int x,int y,bool white){
 if(x<0 || x>=128 || y<0 || y>=64 || (canvas && y/8!=canvas_page))return;
 uint8_t *p=canvas ? canvas+x:fb+(y/8)*128+x;
 uint8_t bit=(uint8_t)(1U<<(y%8));
 if(white)*p|=bit;
 else *p&=(uint8_t)~bit;
}
void oled_rect(int x,int y,int width,int height,bool filled,bool white){
 if(width<=0 || height<=0)return;
 for(int j=0;j<height;++j)for(int i=0;i<width;++i)
  if(filled || !i || !j || i==width-1 || j==height-1)oled_pixel(x+i,y+j,white);
}
void oled_text_xy(int x,int y,const char *text,int size,bool white){
 if(!text)return;
 int width=size==0 ? 4:size==2 ? 12:6,height=size==0 ? 6:size==2 ? 16:8;
 for(;*text && x+width<=128;text++,x+=width){
  unsigned char c=(unsigned char)*text;
  bool button=key_glyph(c);
  if(!button && (c<32 || c>127))c=' ';
  const uint8_t *glyph=text_glyph(c);
  for(int j=0;j<height;++j)for(int i=0;i<width;++i){
   if(size==0 && !button){
    unsigned char upper=c>='a' && c<='z' ? (unsigned char)(c-'a'+'A'):c;
    if(i<3 && j<5 && (mini_font[upper-32] & (1U<<(j*3+i))))oled_pixel(x+i,y+j,white);
   }else {
    int gx=button ? i*6/width:size==2 ? i/2:i,gy=button ? j*8/height:size==2 ? j/2:j;
    if(glyph[gx] & (1U<<gy))oled_pixel(x+i,y+j,white);
   }
  }
 }
}
void oled_bar(int x,int y,int width,int height,unsigned percent){
 if(percent>100)percent=100;
 oled_rect(x,y,width,height,false,true);
 if(width>2 && height>2)oled_rect(x+1,y+1,(int)((width-2)*percent/100),height-2,true,true);
}
