#pragma once

/*
 * Backward-compatible name only. This project revision does not instantiate
 * any RMT TX/RX channel. Calls are forwarded to edge_capture/MCPWM.
 */
#include "audio/edge_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

void edge_capture_rmt_init(uint32_t unit_us);
bool edge_capture_rmt_start(void);
void edge_capture_rmt_stop(void);
void edge_capture_rmt_pause(void);
bool edge_capture_rmt_resume(void);

size_t edge_capture_rmt_read(edge_capture_event_t *dst, size_t max_count);
size_t edge_capture_rmt_free(void);
size_t edge_capture_rmt_level(void);

bool edge_capture_rmt_is_running(void);
bool edge_capture_rmt_is_overrun(void);
void edge_capture_rmt_clear_overrun(void);

#ifdef __cplusplus
}
#endif
