#include <assert.h>
#include <stdio.h>
#include "app/route_switch.h"
#include "app/cmt_bus_policy.h"
int main(void) {
 cmt_bus_outputs_t o;
 for(int p=PLAYER_STATE_IDLE;p<=PLAYER_STATE_READY;++p) {
  o=cmt_bus_outputs(true,true,true,p,RECORDER_STATE_RECORDING,true);
  assert(o.sense_high && o.monitor_enabled && !o.ul_owns_sense);
 }
 for(int r=RECORDER_STATE_IDLE;r<=RECORDER_STATE_ERROR;++r){
  o=cmt_bus_outputs(true,true,true,PLAYER_STATE_PLAYING,(recorder_state_t)r,true);
  assert(o.sense_high && o.monitor_enabled==(r==RECORDER_STATE_RECORDING) && !o.ul_owns_sense);
 }
 o=cmt_bus_outputs(false,true,false,PLAYER_STATE_PLAYING,RECORDER_STATE_IDLE,true);
 assert(o.ul_owns_sense && !o.monitor_enabled);
 o=cmt_bus_outputs(false,false,false,PLAYER_STATE_READY,RECORDER_STATE_IDLE,true);
 assert(o.sense_high && !o.monitor_enabled);
 o=cmt_bus_outputs(false,false,true,PLAYER_STATE_READY,RECORDER_STATE_IDLE,true);
 assert(!o.sense_high && !o.monitor_enabled);
 o=cmt_bus_outputs(false,false,false,PLAYER_STATE_PAUSED,RECORDER_STATE_IDLE,true);
 assert(!o.sense_high && !o.monitor_enabled);
 for(int audible=0;audible<=1;++audible) {
  o=cmt_bus_outputs(false,false,false,PLAYER_STATE_PLAYING,RECORDER_STATE_IDLE,audible);
  assert(!o.sense_high && o.monitor_enabled==audible);
 }
 recorder_state_t states[]={RECORDER_STATE_WAIT_MOTOR,RECORDER_STATE_PAUSED_USER,RECORDER_STATE_PAUSED_MOTOR,RECORDER_STATE_RECORDING};
 for(unsigned i=0;i<sizeof(states)/sizeof(states[0]);++i) {
  o=cmt_bus_outputs(false,false,false,PLAYER_STATE_IDLE,states[i],false);
  assert(!o.sense_high && o.monitor_enabled==(states[i]==RECORDER_STATE_RECORDING));
 }
 o=cmt_bus_outputs(false,false,false,PLAYER_STATE_PLAYING,RECORDER_STATE_FINISHED,true);
 assert(o.sense_high && !o.monitor_enabled);
 assert(!ROUTE_FORCE_INTERNAL && !route_switch_is_external(true) && route_switch_is_external(false));
 route_switch_t s={0};
 assert(route_switch_update(&s,true,0)==ROUTE_INTERNAL && !s.external);
 assert(route_switch_update(&s,false,1)==ROUTE_UNCHANGED && !s.external);
 assert(route_switch_update(&s,false,40000)==ROUTE_UNCHANGED && !s.external);
 assert(route_switch_update(&s,false,40001)==ROUTE_EXTERNAL && s.external);
 assert(route_switch_update(&s,true,50000)==ROUTE_UNCHANGED && s.external);
 assert(route_switch_update(&s,false,60000)==ROUTE_UNCHANGED && s.external);
 assert(route_switch_update(&s,true,70000)==ROUTE_UNCHANGED && s.external);
 assert(route_switch_update(&s,true,109999)==ROUTE_UNCHANGED && s.external);
 assert(route_switch_update(&s,true,110000)==ROUTE_INTERNAL && !s.external);
 route_switch_t low_at_boot={0};
 assert(route_switch_update(&low_at_boot,false,0)==ROUTE_EXTERNAL && low_at_boot.external);
 assert(route_switch_update(NULL,true,0)==ROUTE_UNCHANGED);
 puts("PASS physical S1 GPIO21: LOW external/HIGH internal, 40ms debounce and bounce restart, both boot states; external blocks TX only; recording monitor and capture remain available");
 return 0;
}
