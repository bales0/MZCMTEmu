#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t level;
    uint8_t count_pulse;
    uint32_t duration_us;
} raw_pulse_t;

void raw_pulse_init(void);
bool raw_pulse_start(void);
bool raw_pulse_service(void);
void raw_pulse_stop(void);
void raw_pulse_pause(void);
void raw_pulse_resume(void);
void raw_pulse_finish(void);

size_t raw_pulse_write(const raw_pulse_t *pulses, size_t count);
size_t raw_pulse_free(void);
size_t raw_pulse_level(void);
uint32_t raw_pulse_get_pulses_per_second(void);

bool raw_pulse_is_running(void);
bool raw_pulse_is_underrun(void);
void raw_pulse_clear_underrun(void);
void raw_pulse_set_output_enabled(bool enabled);

/*
 * Compatibility entry point retained for player.c.  This recovery backend
 * deliberately does not allocate or use an RMT channel.
 */
void raw_pulse_set_rmt_enabled(bool enabled);

void raw_pulse_set_read_invert(bool invert);
bool raw_pulse_get_read_invert(void);

/*
 * Legacy fixed-sample API retained only so the current player.c compiles.
 * WAV playback uses raw_out/raw_timer, not this pulse backend.
 */
void raw_pulse_set_tick_us(uint32_t tick_us);
uint32_t raw_pulse_get_tick_us(void);
size_t raw_pulse_write_samples(const uint8_t *samples, size_t count);
size_t raw_pulse_sample_free(void);
size_t raw_pulse_sample_level(void);

/* Completed physical intervals only; excludes queued and paused wall time. */
uint64_t raw_pulse_get_completed_us(void);

#ifdef __cplusplus
}
#endif
