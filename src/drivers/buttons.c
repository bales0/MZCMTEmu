#include "buttons.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#define DEBOUNCE_MS 30U
#define REPEAT_DELAY_MS 400U
#define REPEAT_RATE_MS 120U
#define STOP_LONG_MS 800U
#define MENU_LONG_MS 700U
typedef struct { bool raw,down,armed,long_sent; TickType_t changed,pressed,repeated,delay; } key_t;
static const int pins[]={8,9,10,11,42};
static key_t keys[5];
static bool last_repeat;
void buttons_init(void) {
    gpio_config_t cfg={.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,
        .pull_down_en=GPIO_PULLDOWN_DISABLE,.pin_bit_mask=(1ULL<<8)|(1ULL<<9)|(1ULL<<10)|(1ULL<<11)|(1ULL<<42)};
    gpio_config(&cfg);memset(keys,0,sizeof(keys));last_repeat=false;
    TickType_t now=xTaskGetTickCount();
    for(unsigned i=0;i<5;++i) {
        keys[i].down=keys[i].raw=gpio_get_level(pins[i])==0;
        keys[i].armed=!keys[i].down;keys[i].changed=now;
    }
}
btn_event_t buttons_poll(void) {
    TickType_t now=xTaskGetTickCount();bool pressed[5]={0},released[5]={0};last_repeat=false;
    for(unsigned i=0;i<5;++i) {
        key_t *k=&keys[i];bool raw=gpio_get_level(pins[i])==0;
        if(raw!=k->raw){k->raw=raw;k->changed=now;}
        if(raw!=k->down && (TickType_t)(now-k->changed)>=pdMS_TO_TICKS(DEBOUNCE_MS)) {
            k->down=raw;
            if(raw) {pressed[i]=k->armed;k->pressed=k->repeated=now;k->delay=pdMS_TO_TICKS(REPEAT_DELAY_MS);k->long_sent=false;}
            else {released[i]=k->armed;k->armed=true;}
        }
    }
    /* STOP owns the chord until release: a held navigation key cannot leak
     * a repeat into the next screen. A key held at startup must be released. */
    key_t *stop=&keys[1];
    if(stop->down || released[1]) {
        for(unsigned i=0;i<5;++i)if(i!=1 && keys[i].down)keys[i].armed=false;
        if(stop->down && stop->armed && !stop->long_sent && (TickType_t)(now-stop->pressed)>=pdMS_TO_TICKS(STOP_LONG_MS)) {
            stop->long_sent=true;return BTN_STOP_LONG;
        }
        if(released[1] && !stop->long_sent)return BTN_STOP;
        return BTN_NONE;
    }
    /* Like reference SELECT/RIGHT: short fires on release, so a hold cannot
     * start transport before opening its menu. Startup-held/chord keys stay inert. */
    const unsigned dual[]={0,4};
    for(unsigned n=0;n<2;++n){
        unsigned i=dual[n];key_t *k=&keys[i];
        if(k->down && k->armed && !k->long_sent && (TickType_t)(now-k->pressed)>=pdMS_TO_TICKS(MENU_LONG_MS)){
            k->long_sent=true;
            for(unsigned j=0;j<5;++j)if(j!=i && keys[j].down)keys[j].armed=false;
            return i==0 ? BTN_PLAY_LONG:BTN_REC_LONG;
        }
        if(released[i] && !k->long_sent)return i==0 ? BTN_PLAY:BTN_REC;
    }
    for(unsigned i=2;i<=3;++i) {
        key_t *k=&keys[i];
        if(pressed[i])return i==2 ? BTN_NEXT:BTN_PREV;
        if(k->down && k->armed && (TickType_t)(now-k->repeated)>=k->delay) {
            k->repeated=now;k->delay=pdMS_TO_TICKS(REPEAT_RATE_MS);last_repeat=true;
            return i==2 ? BTN_NEXT:BTN_PREV;
        }
    }
    return BTN_NONE;
}
bool buttons_last_event_was_repeat(void) {return last_repeat;}
void buttons_delay_repeat(btn_event_t button,uint32_t delay_ms) {
    if(button==BTN_NEXT || button==BTN_PREV) {
        key_t *k=&keys[button==BTN_NEXT ? 2:3];k->repeated=xTaskGetTickCount();k->delay=pdMS_TO_TICKS(delay_ms);
    }
}
