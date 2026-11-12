/*
 * Compatibility facade only.
 *
 * The current recorder and older callers still use the edge_capture_rmt API,
 * but transport capture intentionally uses GPIO5 edge timestamps from
 * edge_capture.c.  No RMT RX/TX channel and no GPIO12 clock are created.
 */
#include "audio/edge_capture_rmt.h"

void edge_capture_rmt_init(uint32_t unit_us)
{
    edge_capture_init(unit_us);
}

bool edge_capture_rmt_start(void)
{
    edge_capture_start();
    return edge_capture_is_running() && !edge_capture_is_overrun();
}

void edge_capture_rmt_stop(void)
{
    edge_capture_stop();
}

void edge_capture_rmt_pause(void)
{
    edge_capture_pause();
}

bool edge_capture_rmt_resume(void)
{
    edge_capture_resume();
    return edge_capture_is_running() && !edge_capture_is_overrun();
}

size_t edge_capture_rmt_read(edge_capture_event_t *dst, size_t max_count)
{
    return edge_capture_read(dst, max_count);
}

size_t edge_capture_rmt_free(void)
{
    return edge_capture_free();
}

size_t edge_capture_rmt_level(void)
{
    return edge_capture_level();
}

bool edge_capture_rmt_is_running(void)
{
    return edge_capture_is_running();
}

bool edge_capture_rmt_is_overrun(void)
{
    return edge_capture_is_overrun();
}

void edge_capture_rmt_clear_overrun(void)
{
    edge_capture_clear_overrun();
}
