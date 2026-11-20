#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio/edge_stream.h"
static unsigned action,threshold,calls,last;
static edge_scan_action_t control(uint32_t done,uint32_t total,void *context){
 assert(context==&calls && total && done<=total && done>=last);last=done;++calls;
 if((uint64_t)done*100>=((uint64_t)total*threshold))return action==1 ? EDGE_SCAN_SKIP:action==2 ? EDGE_SCAN_CANCEL:EDGE_SCAN_CONTINUE;
 return EDGE_SCAN_CONTINUE;
}
static uint64_t drain(void){uint64_t us=0;uint8_t level;uint32_t span;bool count;
 while(edge_stream_next_pulse(&level,&span,&count))us+=span;
 assert(edge_stream_is_eof() && !edge_stream_has_error());return us;
}
int main(void){
 for(unsigned format=0;format<2;++format){
  const char *path=format ? "timing.l16":"timing.lep";FILE *f=fopen(path,"wb");assert(f);uint64_t units=0;
  for(unsigned i=0;i<10240;++i){unsigned byte=i%256;int value=(int8_t)byte;units+=value==0 ? 127:value<0 ? -value:value;assert(fputc(byte,f)!=EOF);}assert(!fclose(f));
  uint64_t expected=800000+units*(format ? 16:50);
  for(unsigned choice=0;choice<5;++choice){
   action=choice==1 || choice==2 ? 1:choice>=3 ? 2:0;threshold=choice==2 || choice==4 ? 90:0;calls=last=0;
   edge_stream_set_scan_control(control,&calls);edge_stream_info_t info;memset(&info,0xff,sizeof(info));
   bool ok=edge_stream_open(path,&info);assert(edge_stream_analysis_cancelled()==(action==2));
   if(action==2){assert(!ok && !info.total_us && !edge_stream_get_total_bytes());continue;}
   assert(ok && !edge_stream_get_consumed_bytes());
   assert(info.total_us==(action==1 ? 0:expected));assert(info.total_seconds==info.total_us/1000000);
   unsigned before=calls;assert(drain()==expected && calls==before);edge_stream_close();
   if(!action)assert(last==10240 && calls==11);
   else if(threshold==90)assert(last==9216);
  }
  assert(!remove(path));
 }
 edge_stream_set_scan_control(NULL,NULL);
 FILE *f=fopen("long.lep","wb");assert(f);uint8_t zero[1024]={0};for(unsigned i=0;i<1024;++i)assert(fwrite(zero,1,sizeof(zero),f)==sizeof(zero));assert(!fclose(f));
 edge_stream_info_t info;assert(edge_stream_open("long.lep",&info));assert(info.total_us==800000+1024ULL*1024*127*50 && info.total_us>UINT32_MAX);edge_stream_close();assert(!remove("long.lep"));
 puts("PASS LEP/L16 exact block duration versus emitted waveform, all signed bytes incl zero/-128, initial/90% SKIP and CANCEL, monotonic 0..100 scan progress, rewind, clean retry and 64-bit duration");
 return 0;
}
