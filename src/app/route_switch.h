#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { bool known, external, candidate; uint64_t changed_at; } route_switch_t;
/* Physical S1 observation restored: LOW=EXTERNAL, HIGH=INTERNAL. */
#define ROUTE_FORCE_INTERNAL 0
#define ROUTE_EXTERNAL_LEVEL_HIGH 0
static inline bool route_switch_is_external(bool high){return ROUTE_FORCE_INTERNAL ? false:high==(ROUTE_EXTERNAL_LEVEL_HIGH!=0);}

typedef enum { ROUTE_UNCHANGED, ROUTE_INTERNAL, ROUTE_EXTERNAL } route_event_t;
/* Pure observation of the physical switch. Never controls a nonexistent mux. */
route_event_t route_switch_update(route_switch_t *state,bool high,uint64_t now_us);
