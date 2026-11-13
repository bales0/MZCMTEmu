/* SPDX-License-Identifier: GPL-3.0-only */
#include "audio/edge_guard.h"
#if defined(CONFIG_MZ_RX_PCNT_GUARD) && CONFIG_MZ_RX_PCNT_GUARD
#include "driver/pulse_cnt.h"
#include <string.h>
static pcnt_unit_handle_t unit;
static pcnt_channel_handle_t channel;
static edge_guard_status_t status;
static uint32_t first,last,before,base,previous;
static bool active,sticky_suspected;
void edge_guard_abort(void){if(unit && active){if(pcnt_unit_stop(unit)!=ESP_OK)status.unreliable=true;
active=false;
}}
void edge_guard_reset(void){edge_guard_abort();
memset(&status,0,sizeof(status));
sticky_suspected=false;
}
static bool count(uint32_t *value){
 int n;
 if(pcnt_unit_get_count(unit,&n)!=ESP_OK || n<0 || (uint32_t)n<previous || (uint32_t)n>=0x20000000U){status.unreliable=true;
 return false;
 }
 previous=(uint32_t)n;
 *value=(uint32_t)n;
 return true;
}
bool edge_guard_init(void){
 if(unit)return true;
 pcnt_unit_config_t cfg={.low_limit=-30000,.high_limit=30000,.intr_priority=3,.flags.accum_count=1};
 pcnt_chan_config_t input={.edge_gpio_num=5,.level_gpio_num=-1};
 if(pcnt_new_unit(&cfg,&unit)!=ESP_OK)return false;
 if(pcnt_new_channel(unit,&input,&channel)!=ESP_OK ||
    pcnt_channel_set_edge_action(channel,PCNT_CHANNEL_EDGE_ACTION_INCREASE,PCNT_CHANNEL_EDGE_ACTION_INCREASE)!=ESP_OK ||
    pcnt_channel_set_level_action(channel,PCNT_CHANNEL_LEVEL_ACTION_KEEP,PCNT_CHANNEL_LEVEL_ACTION_KEEP)!=ESP_OK ||
    pcnt_unit_set_glitch_filter(unit,NULL)!=ESP_OK || pcnt_unit_add_watch_point(unit,30000)!=ESP_OK ||
    pcnt_unit_add_watch_point(unit,-30000)!=ESP_OK || pcnt_unit_enable(unit)!=ESP_OK){
  if(channel){pcnt_del_channel(channel);
  channel=NULL;
  }pcnt_del_unit(unit);
  unit=NULL;
  return false;
 }
 return true;
}
bool edge_guard_begin(uint32_t enqueued){
 memset(&status,0,sizeof(status));
 status.enabled=unit!=NULL;
 status.suspected=sticky_suspected;
 first=last=before=0;
 base=enqueued;
 previous=0;
 active=false;
 if(!unit)return true;
 if(pcnt_unit_clear_count(unit)!=ESP_OK || pcnt_unit_start(unit)!=ESP_OK){status.unreliable=true;
 return false;
 }
 active=true;
 return count(&first);
}
void edge_guard_started(void){if(unit && active)(void)count(&last);
}
void edge_guard_stopping(void){if(unit && active)(void)count(&before);
}
void edge_guard_poll(void){uint32_t unused;
if(unit && active)(void)count(&unused);
}
void edge_guard_end(uint32_t enqueued){
 if(!unit || !active)return;
 uint32_t after=0;
 bool ok=count(&after);
 if(pcnt_unit_stop(unit)!=ESP_OK)ok=false;
 active=false;
 if(!ok || status.unreliable){status.unreliable=true;
 return;
 }
 edge_guard_status_t result=edge_guard_compare(first,last,before,after,enqueued-base);
 result.suspected|=status.suspected;
 result.confirmed|=status.confirmed;
 sticky_suspected=result.suspected;
 status=result;
}
void edge_guard_snapshot(edge_guard_status_t *out){*out=status;
}

#else
#include <string.h>
bool edge_guard_init(void){return true;
}
bool edge_guard_begin(uint32_t n){(void)n;
return true;
}
void edge_guard_started(void){}
void edge_guard_reset(void){}
void edge_guard_abort(void){}
void edge_guard_stopping(void){}
void edge_guard_end(uint32_t n){(void)n;
}
void edge_guard_poll(void){}
void edge_guard_snapshot(edge_guard_status_t *s){memset(s,0,sizeof(*s));
}
#endif
