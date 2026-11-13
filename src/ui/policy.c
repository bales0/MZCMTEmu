/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui/policy.h"
#include <string.h>
uint8_t ui_headroom_update(ui_headroom_t *s,uint8_t actual,uint32_t now){
 if(!s->valid || actual<s->shown){s->valid=true;
 s->shown=actual;
 s->recovered_at=now;
 }
 else if((uint32_t)(now-s->recovered_at)>=500){unsigned n=s->shown+4;
 s->shown=actual<n ? actual:(uint8_t)n;
 s->recovered_at=now;
 }
 return s->shown;
}
ui_buffer_alarm_t ui_buffer_alarm(uint8_t n){return !n ? UI_BUFFER_BAD:n<10 ? UI_BUFFER_CRITICAL:n<25 ? UI_BUFFER_LOW:UI_BUFFER_OK;
}
size_t ui_marquee_offset(ui_marquee_t *s,const char *text,size_t width,uint32_t now,uint32_t context){
 uint32_t hash=2166136261U^context;
 size_t n=strlen(text);
 for(size_t i=0;i<n;++i)hash=(hash^(uint8_t)text[i])*16777619U;
 if(!s->valid || hash!=s->key){s->valid=true;
 s->key=hash;
 s->changed_at=now;
 }
 if(n<=width)return 0;
 uint32_t elapsed=now-s->changed_at,steps=(uint32_t)(n-width),cycle=1400+steps*180;
 uint32_t phase=elapsed%cycle;
 if(phase<700)return 0;
 size_t offset=(phase-700)/180;
 return offset>steps ? steps:offset;
}
