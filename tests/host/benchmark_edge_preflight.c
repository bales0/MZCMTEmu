#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "audio/edge_stream.h"
bool baseline_edge_stream_open(const char *,edge_stream_info_t *);
void baseline_edge_stream_close(void);
static uint64_t now(void){struct timespec t;
#ifdef _WIN32
 timespec_get(&t,TIME_UTC);
#else
 clock_gettime(CLOCK_MONOTONIC,&t);
#endif
return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static uint64_t median(uint64_t x[3]){if(x[0]>x[1]){uint64_t t=x[0];x[0]=x[1];x[1]=t;}if(x[1]>x[2]){uint64_t t=x[1];x[1]=x[2];x[2]=t;}return x[0]>x[1] ? x[0]:x[1];}
int main(void){static uint8_t block[16384];
 for(unsigned shape=0;shape<3;++shape){unsigned mb=shape ? 10:1;const char *path="benchmark.lep";
  for(unsigned i=0;i<sizeof(block);++i)block[i]=shape==2 ? (i&1 ? 248:8):0;
  FILE *f=fopen(path,"wb");assert(f);for(unsigned n=0;n<mb*64;++n)assert(fwrite(block,1,sizeof(block),f)==sizeof(block));assert(!fclose(f));
  uint64_t base[3],fresh[3],warm[3];
  for(unsigned r=0;r<3;++r){edge_stream_info_t a,b,c;uint64_t t=now();assert(baseline_edge_stream_open(path,&a));base[r]=now()-t;baseline_edge_stream_close();
   edge_stream_invalidate_cache();t=now();assert(edge_stream_open(path,&b));fresh[r]=now()-t;assert(!b.analysis.cache_hit && a.total_us==b.total_us);edge_stream_close();
   t=now();assert(edge_stream_open(path,&c));warm[r]=now()-t;assert(c.analysis.cache_hit && c.total_us==b.total_us);edge_stream_close();
  }
  printf("HOST_COMPARE bytes=%u shape=%s baseline_us=%llu metadata_scan_us=%llu cache_hit_us=%llu repeats=3 statistic=median compiler=GCC_O2_no_sanitizer\n",mb*1024*1024,shape==2 ? "edges":"silence",(unsigned long long)median(base),(unsigned long long)median(fresh),(unsigned long long)median(warm));assert(!remove(path));
 }
 return 0;
}
