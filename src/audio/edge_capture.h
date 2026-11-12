#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t level;   /* level that has just ended */
    uint8_t rising;  /* this interval ended with LOW -> HIGH */
    uint32_t units;  /* duration in configured unit_us */
} edge_capture_event_t;

void edge_capture_init(uint32_t unit_us);
void edge_capture_start(void);
void edge_capture_stop(void);
void edge_capture_pause(void);
void edge_capture_resume(void);

size_t edge_capture_read(edge_capture_event_t *dst, size_t max_count);
size_t edge_capture_free(void);
size_t edge_capture_level(void);

bool edge_capture_is_running(void);
bool edge_capture_is_overrun(void);
void edge_capture_clear_overrun(void);

#ifdef __cplusplus
}
#endif
