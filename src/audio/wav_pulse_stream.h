#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Shared by PLAY and the independent analyzer cursor. State belongs to the
 * caller; values in the deadband retain the previous physical level. */
static inline uint8_t wav_pulse_hysteresis(uint8_t previous,uint8_t sample) {
    if(!previous && sample>=0xC0U)return 1;
    if(previous && sample<=0x40U)return 0;
    return previous;
}
static inline uint64_t wav_pulse_time(uint64_t sample,uint32_t rate,uint32_t frequency) {
    return (sample*frequency+rate/2U)/rate;
}
/* Consumes the already-open wav_reader. No second file or whole-wave buffer. */
bool wav_pulse_stream_begin(uint32_t sample_rate);
bool wav_pulse_stream_next(uint8_t *level,uint32_t *duration_us);
bool wav_pulse_stream_eof(void);
bool wav_pulse_stream_error(void);
uint64_t wav_pulse_stream_samples(void);
uint64_t wav_pulse_stream_queued_us(void);

bool wav_pulse_stream_next_ticks16(uint8_t *level,uint32_t *ticks);
