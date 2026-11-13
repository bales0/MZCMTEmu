#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAW_PULSE_COUNT 1U
#define RAW_PULSE_MONITOR_MUTE 2U

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
 * Idle-only enable gate for the RMT backend. The diagnostic GPTimer backend
 * is selected at build time with CONFIG_MZ_TX_GPTIMER_FALLBACK.
 */
void raw_pulse_set_rmt_enabled(bool enabled);

void raw_pulse_set_read_invert(bool invert);
bool raw_pulse_get_read_invert(void);

/*
 * Legacy fixed-sample API retained for compatibility with older diagnostics.
 * All PLAY formats now use the pulse backend; no fixed-sample API is used.
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

/* Optional native 16 MHz source widths. Levels/count flags come from events;
 * ticks[i] replaces duration_us without widening/copying the source FIFO. */
bool raw_pulse_native_ticks16(void);
size_t raw_pulse_write_ticks16(const raw_pulse_t *events,const uint32_t *ticks,size_t count);

/* Audible current interval, independent of queued future data. */
bool raw_pulse_monitor_active(void);
