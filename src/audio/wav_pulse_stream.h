#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Consumes the already-open wav_reader. No second file or whole-wave buffer. */
bool wav_pulse_stream_begin(uint32_t sample_rate);
bool wav_pulse_stream_next(uint8_t *level,uint32_t *duration_us);
bool wav_pulse_stream_eof(void);
bool wav_pulse_stream_error(void);
uint64_t wav_pulse_stream_samples(void);
uint64_t wav_pulse_stream_queued_us(void);

bool wav_pulse_stream_next_ticks16(uint8_t *level,uint32_t *ticks);
