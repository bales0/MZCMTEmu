#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void raw_timer_init(int sample_rate);
bool raw_timer_start(void);
void raw_timer_stop(void);
void raw_timer_pause(void);
void raw_timer_resume(void);

size_t raw_timer_write(const uint8_t *data, size_t len);
void raw_timer_finish(void);
size_t raw_timer_free(void);
size_t raw_timer_level(void);

bool raw_timer_is_running(void);
bool raw_timer_is_underrun(void);
void raw_timer_clear_underrun(void);
void raw_timer_set_output_enabled(bool enabled);
void raw_timer_set_read_invert(bool invert);
bool raw_timer_get_read_invert(void);

#ifdef __cplusplus
}
#endif
