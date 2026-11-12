#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed-rate WAV sample output. Implemented exclusively by raw_timer/GPTimer. */
void raw_out_init(int sample_rate);
bool raw_out_start(void);
void raw_out_stop(void);
void raw_out_pause(void);
void raw_out_resume(void);

size_t raw_out_write(const uint8_t *data, size_t len);
void raw_out_finish(void);
size_t raw_out_free(void);
size_t raw_out_level(void);

bool raw_out_is_running(void);
bool raw_out_is_underrun(void);
void raw_out_clear_underrun(void);
void raw_out_set_read_invert(bool invert);
bool raw_out_get_read_invert(void);
void raw_out_set_sample_output_enabled(bool enabled);

#ifdef __cplusplus
}
#endif
