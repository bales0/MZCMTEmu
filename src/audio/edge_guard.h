/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {bool enabled,suspected,confirmed,unreliable;uint32_t lower,upper,enqueued;} edge_guard_status_t;
/* Epoch boundary uncertainty is explicit. Only counts outside the bracket
 * confirm a mismatch. No timestamps can be recovered from this counter. */
static inline edge_guard_status_t edge_guard_compare(uint32_t start_before,uint32_t start_after,
        uint32_t stop_before,uint32_t stop_after,uint32_t enqueued){
 edge_guard_status_t s={.enabled=true,.enqueued=enqueued};
 if(start_after<start_before || stop_before<start_after || stop_after<stop_before || stop_after>=0x20000000U){s.unreliable=true;return s;}
 s.lower=stop_before-start_after;s.upper=stop_after-start_before;
 if(s.upper-s.lower>64U){s.unreliable=true;return s;}
 s.confirmed=enqueued<s.lower || enqueued>s.upper;
 s.suspected=!s.confirmed && s.lower!=s.upper;
 return s;
}
bool edge_guard_init(void);
bool edge_guard_begin(uint32_t enqueued);
void edge_guard_started(void);
void edge_guard_stopping(void);
void edge_guard_end(uint32_t enqueued);
void edge_guard_poll(void);
void edge_guard_snapshot(edge_guard_status_t *);

void edge_guard_reset(void);
void edge_guard_abort(void);
