#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_NONE = 0,
    BTN_PLAY,
    BTN_STOP,
    BTN_STOP_LONG,
    BTN_NEXT,
    BTN_PREV,
    BTN_REC,
    BTN_REC_LONG,
    BTN_PLAY_LONG
} btn_event_t;

void buttons_init(void);
btn_event_t buttons_poll(void);
bool buttons_last_event_was_repeat(void);
void buttons_delay_repeat(btn_event_t button, uint32_t delay_ms);

#ifdef __cplusplus
}
#endif
