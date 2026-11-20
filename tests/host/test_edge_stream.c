#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio/edge_stream.h"
#include "storage/fs.h"
#include <time.h>
static unsigned action,threshold,calls,last;
static edge_scan_action_t control(uint32_t done,uint32_t total,void *context){
 edge_stream_analysis_note_ui(3,false);edge_stream_analysis_note_ui(2,true);
 assert(context==&calls && total && done<=total && done>=last);last=done;++calls;
 if((uint64_t)done*100>=((uint64_t)total*threshold))return action==1 ? EDGE_SCAN_SKIP:action==2 ? EDGE_SCAN_CANCEL:EDGE_SCAN_CONTINUE;
 return EDGE_SCAN_CONTINUE;
}
static uint64_t drain(void){uint64_t us=0;uint8_t level;uint32_t span;bool count;
 while(edge_stream_next_pulse(&level,&span,&count))us+=span;
 assert(edge_stream_is_eof() && !edge_stream_has_error());return us;
}

static const char *truncate_path;
static edge_scan_action_t truncate_scan(uint32_t done,uint32_t total,void *unused){
 (void)total;(void)unused;
 if(done>=1024 && truncate_path){FILE *f=fopen(truncate_path,"wb");assert(f && !fclose(f));truncate_path=NULL;}
 return EDGE_SCAN_CONTINUE;
}
static uint64_t host_wall_us(void){struct timespec t;timespec_get(&t,TIME_UTC);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static void cache_cases(void){
 static uint8_t block[16384];
 for(unsigned shape=0;shape<3;++shape){
  unsigned mb=shape ? 10:1;const char *path="cache.lep";FILE *f=fopen(path,"wb");assert(f);
  for(unsigned j=0;j<sizeof(block);++j)block[j]=shape==2 ? (j&1 ? 248:8):0;
  for(unsigned n=0;n<mb*64;++n)assert(fwrite(block,1,sizeof(block),f)==sizeof(block));
  assert(!fclose(f));
  edge_stream_invalidate_cache();edge_stream_info_t first,hit;
  uint64_t start=host_wall_us();assert(edge_stream_open(path,&first));uint64_t end=host_wall_us();
  assert(!first.analysis.cache_hit && first.analysis.state==EDGE_ANALYSIS_COMPLETE);edge_stream_close();
  uint64_t warm_start=host_wall_us();assert(edge_stream_open(path,&hit));uint64_t warm_end=host_wall_us();
  assert(hit.analysis.cache_hit && hit.analysis.cache_status==EDGE_CACHE_HIT && hit.total_us==first.total_us);edge_stream_close();
  printf("HOST_EDGE bytes=%u shape=%s first_us=%llu cache_us=%llu first_read_us=%llu decode_us=%llu callback_us=%llu\n",mb*1024*1024,shape==2 ? "edges":"silence",(unsigned long long)(end-start),(unsigned long long)(warm_end-warm_start),(unsigned long long)first.analysis.read_us,(unsigned long long)first.analysis.decode_us,(unsigned long long)first.analysis.callback_us);
  f=fopen(path,"r+b");assert(f && fputc(1,f)!=EOF && !fclose(f));assert(edge_stream_open(path,&hit) && !hit.analysis.cache_hit);edge_stream_close();
  assert(edge_stream_open(path,&hit) && hit.analysis.cache_hit);edge_stream_close();
  cmt_storage_invalidate();assert(edge_stream_open(path,&hit) && !hit.analysis.cache_hit && hit.analysis.cache_status==EDGE_CACHE_INVALID);edge_stream_close();
  assert(edge_stream_open(path,&hit) && hit.analysis.cache_hit);edge_stream_close();
  action=1;threshold=0;calls=last=0;edge_stream_set_scan_control(control,&calls);assert(edge_stream_open(path,&hit) && !hit.total_us && hit.analysis.state==EDGE_ANALYSIS_SKIPPED && !hit.analysis.cache_hit);edge_stream_close();
  edge_stream_set_scan_control(NULL,NULL);assert(edge_stream_open(path,&hit) && !hit.analysis.cache_hit);edge_stream_close();
  action=2;threshold=50;calls=last=0;edge_stream_set_scan_control(control,&calls);assert(!edge_stream_open(path,&hit) && edge_stream_analysis_cancelled());
  edge_stream_set_scan_control(NULL,NULL);assert(edge_stream_open(path,&hit) && !hit.analysis.cache_hit);edge_stream_close();
  edge_stream_invalidate_cache();truncate_path=path;edge_stream_set_scan_control(truncate_scan,NULL);assert(!edge_stream_open(path,&hit) && !edge_stream_analysis_cancelled());
  edge_analysis_t failed;edge_stream_analysis_snapshot(&failed);assert(failed.state==EDGE_ANALYSIS_ERROR && !failed.program.valid);
  edge_stream_set_scan_control(NULL,NULL);assert(!remove(path));
 }
 puts("PASS RAM cache identity/fingerprint, media/write epoch, complete-only entries, SKIP/CANCEL invalidate, read error clears preview, no partial result becomes a hit");
}

int main(void){
 for(unsigned format=0;format<2;++format){
  const char *path=format ? "timing.l16":"timing.lep";FILE *f=fopen(path,"wb");assert(f);uint64_t units=0;
  for(unsigned i=0;i<10240;++i){unsigned byte=i%256;int value=(int8_t)byte;units+=value==0 ? 127:value<0 ? -value:value;assert(fputc(byte,f)!=EOF);}assert(!fclose(f));
  uint64_t expected=800000+units*(format ? 16:50);
  const unsigned thresholds[]={0,50,90,100};
  for(unsigned choice=0;choice<9;++choice){
   action=choice>=5 ? 2:choice>=1 ? 1:0;threshold=choice ? thresholds[(choice-1)%4]:0;calls=last=0;
   edge_stream_set_scan_control(control,&calls);edge_stream_info_t info;memset(&info,0xff,sizeof(info));
   bool ok=edge_stream_open(path,&info);assert(edge_stream_analysis_cancelled()==(action==2));
   if(action==2){assert(!ok && !info.total_us && !edge_stream_get_total_bytes());continue;}
   assert(ok && !edge_stream_get_consumed_bytes());
   assert(info.total_us==(action==1 ? 0:expected));assert(info.total_seconds==info.total_us/1000000);
   unsigned before=calls;assert(drain()==expected && calls==before);edge_stream_close();
   if(!action)assert(last==10240 && calls==11 && info.analysis.ui_us==33 && info.analysis.yield_us==22);
   else if(threshold==90)assert(last==9216);
   else if(threshold==50)assert(last==5120);
   else if(threshold==100)assert(last==10240);
  }
  assert(!remove(path));
 }
 edge_stream_set_scan_control(NULL,NULL);
 FILE *f=fopen("long.lep","wb");assert(f);uint8_t zero[1024]={0};for(unsigned i=0;i<1024;++i)assert(fwrite(zero,1,sizeof(zero),f)==sizeof(zero));assert(!fclose(f));
 edge_stream_info_t info;assert(edge_stream_open("long.lep",&info));assert(info.total_us==800000+1024ULL*1024*127*50 && info.total_us>UINT32_MAX);edge_stream_close();assert(!remove("long.lep"));
 FILE *metric=fopen("metrics.lep","wb");assert(metric && fputs("abc",metric)>=0 && !fclose(metric));
 action=0;threshold=0;calls=last=0;edge_stream_set_scan_control(control,&calls);assert(edge_stream_open("metrics.lep",&info));edge_stream_close();
 calls=last=0;assert(edge_stream_open("metrics.lep",&info) && info.analysis.cache_hit && info.analysis.ui_us==6 && info.analysis.yield_us==4);edge_stream_close();
 edge_stream_set_scan_control(NULL,NULL);assert(!remove("metrics.lep"));
 cache_cases();
 puts("PASS LEP/L16 exact block duration versus emitted waveform, all signed bytes incl zero/-128, 0/50/90/100% SKIP and CANCEL, monotonic 0..100 scan progress, rewind, clean retry and 64-bit duration");
 return 0;
}
