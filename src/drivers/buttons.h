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
    BTN_PLAY_LONG,
    /* Seek navigation events are opt-in; existing event values stay stable. */
    BTN_NEXT_LONG,
    BTN_PREV_LONG,
    BTN_NEXT_REPEAT,
    BTN_PREV_REPEAT,
    BTN_NEXT_RELEASE,
    BTN_PREV_RELEASE
} btn_event_t;

typedef enum {
    BUTTONS_NAV_NORMAL = 0,
    BUTTONS_NAV_SEEK
} buttons_nav_mode_t;

void buttons_init(void);
/* Foreground poll owner only. Default after init is NORMAL. Select SEEK
 * before a gesture begins, not after its normal short event was emitted.
 * Changing mode suppresses held navigation keys until their release;
 * setting the same mode is a no-op. PLAY/STOP/REC are unaffected. */
void buttons_set_nav_mode(buttons_nav_mode_t mode);
btn_event_t buttons_poll(void);
bool buttons_last_event_was_repeat(void);
void buttons_delay_repeat(btn_event_t button, uint32_t delay_ms);

#ifdef __cplusplus
}
#endif
