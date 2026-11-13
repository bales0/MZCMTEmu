#include <assert.h>
#include <stdio.h>
#include "app/route_switch.h"
#include "app/cmt_bus_policy.h"
int main(void) {
 cmt_bus_outputs_t o;
 for(int p=PLAYER_STATE_IDLE;p<=PLAYER_STATE_READY;++p) {
  o=cmt_bus_outputs(true,true,true,p,RECORDER_STATE_RECORDING,true);
  assert(o.sense_high && !o.monitor_enabled && !o.ul_owns_sense);
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
 route_switch_t s={0};assert(route_switch_update(&s,false,0)==ROUTE_EXTERNAL && s.external);
 assert(route_switch_update(&s,true,1)==ROUTE_UNCHANGED && s.external);
 assert(route_switch_update(&s,false,30000)==ROUTE_UNCHANGED && s.external);
 assert(route_switch_update(&s,true,40000)==ROUTE_UNCHANGED && s.external);
 assert(route_switch_update(&s,true,79999)==ROUTE_UNCHANGED && s.external);
 assert(route_switch_update(&s,true,80000)==ROUTE_INTERNAL && !s.external);
 assert(route_switch_update(&s,false,100000)==ROUTE_UNCHANGED);
 assert(route_switch_update(&s,false,139999)==ROUTE_UNCHANGED);
 assert(route_switch_update(&s,false,140000)==ROUTE_EXTERNAL && s.external);
 assert(route_switch_update(&s,false,999999)==ROUTE_UNCHANGED);
 assert(route_switch_update(NULL,true,0)==ROUTE_UNCHANGED);
 puts("PASS physical CMT route: safe initial external state, bounce rejection, 40 ms transitions, no repeated event");return 0;
}
