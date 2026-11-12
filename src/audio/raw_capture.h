#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void raw_capture_init(int sample_rate);
void raw_capture_start(void);
void raw_capture_stop(void);
void raw_capture_pause(void);
void raw_capture_resume(void);
void raw_capture_flush_partial(void);

size_t raw_capture_read(uint8_t *dst, size_t max_len);
size_t raw_capture_free(void);
size_t raw_capture_level(void);

bool raw_capture_is_running(void);
bool raw_capture_is_overrun(void);
void raw_capture_clear_overrun(void);

#ifdef __cplusplus
}
#endif
