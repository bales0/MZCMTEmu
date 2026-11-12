#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { bool known, external, candidate; uint64_t changed_at; } route_switch_t;
typedef enum { ROUTE_UNCHANGED, ROUTE_INTERNAL, ROUTE_EXTERNAL } route_event_t;
/* Pure observation of the physical switch. Never controls a nonexistent mux. */
route_event_t route_switch_update(route_switch_t *state,bool high,uint64_t now_us);
