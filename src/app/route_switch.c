#include "app/route_switch.h"
route_event_t route_switch_update(route_switch_t *s,bool high,uint64_t now_us)
{
    if(!s) return ROUTE_UNCHANGED;
    bool external=!high;
    if(!s->known) {
        s->known=true;s->external=s->candidate=external;s->changed_at=now_us;
        return external ? ROUTE_EXTERNAL:ROUTE_INTERNAL;
    }
    if(external!=s->candidate) { s->candidate=external;s->changed_at=now_us; }
    if(s->candidate!=s->external && now_us>=s->changed_at && now_us-s->changed_at>=40000) {
        s->external=s->candidate;
        return s->external ? ROUTE_EXTERNAL:ROUTE_INTERNAL;
    }
    return ROUTE_UNCHANGED;
}
