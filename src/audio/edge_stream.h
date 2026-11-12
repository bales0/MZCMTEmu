#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_rate;
    uint32_t total_seconds;
    uint32_t total_samples;
    uint32_t pulses_per_second;
} edge_stream_info_t;

bool edge_stream_open(const char *path, edge_stream_info_t *info);
void edge_stream_close(void);
size_t edge_stream_read(uint8_t *dst, size_t max_len);
bool edge_stream_is_eof(void);
bool edge_stream_has_error(void);
uint32_t edge_stream_get_total_bytes(void);
uint32_t edge_stream_get_consumed_bytes(void);
bool edge_stream_is_edge_path(const char *path);
bool edge_stream_next_pulse(uint8_t *level, uint32_t *duration_us, bool *count_pulse);

#ifdef __cplusplus
}
#endif
