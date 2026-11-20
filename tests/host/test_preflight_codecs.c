#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio/edge_stream.h"
#include "audio/edge_writer.h"
#include "audio/sharp_stream.h"
static void fixture(void){uint8_t h[128]={1},data[]={0,128,1,255};memcpy(h+1,"REAL GAME\r",10);h[18]=4;h[21]=h[23]=0x12;FILE *f=fopen("source.mzf","wb");assert(f && fwrite(h,1,128,f)==128 && fwrite(data,1,4,f)==4 && !fclose(f));}
int main(void){
 const loader_mode_t modes[]={LOADER_MODE_NORMAL_1_1,LOADER_MODE_NORMAL_1_2,LOADER_MODE_NORMAL_1_3,LOADER_MODE_NORMAL_1_4,LOADER_MODE_IC_1_1,LOADER_MODE_IC_1_2,LOADER_MODE_IC_1_3,LOADER_MODE_IC_1_4,LOADER_MODE_TC_1_1,LOADER_MODE_TC_1_2,LOADER_MODE_TC_1_3};
 fixture();
 for(unsigned f=0;f<2;++f)for(unsigned m=0;m<sizeof(modes)/sizeof(modes[0]);++m){
  const char *path=f ? "recorded.l16":"recorded.lep";unsigned unit=f ? 256:800;
  sharp_stream_info_t original;assert(sharp_stream_open("source.mzf",1,modes[m],&original));assert(!original.fallback);
  assert(edge_writer_open(path,f ? 62500:20000));uint8_t level;uint32_t ticks;bool count;
  while(!sharp_stream_is_eof()){
   while(sharp_stream_next_pulse(&level,&ticks,&count))assert(edge_writer_write_interval_units(level,(ticks+unit/2)/unit));
   if(sharp_stream_is_eof())break;
   uint32_t pause=sharp_stream_boundary_delay_us(false);if(pause)assert(edge_writer_write_interval_units(0,(pause*16+unit/2)/unit));
   assert(sharp_stream_advance());
  }
  sharp_stream_close();assert(edge_writer_write_interval_units(0,1000000/(f ? 16:50)) && edge_writer_finalize());
  edge_stream_info_t info;edge_stream_invalidate_cache();assert(edge_stream_open(path,&info));
  program_metadata_t *p=&info.analysis.program;
  printf("PREFLIGHT format=%s mode=%u confidence=%u detected=%u title=%s\n",f ? "L16":"LEP",(unsigned)modes[m],p->confidence,(unsigned)p->recorded_loader_mode,p->title);
  assert(p->confidence==PROGRAM_PAYLOAD_VALID && p->declared_size==4 && p->load==0x1200 && p->exec==0x1200 && !strcmp(p->title,"REAL GAME"));
  assert(p->recorded_loader_mode==modes[m]);edge_stream_close();assert(!remove(path));
 }
 assert(!remove("source.mzf"));puts("PASS real Sharp waveform -> LEP/L16 -> preflight: every NORMAL/IC speed 1:1..1:4 and TC 1:1..1:3, canonical target title/size/load/exec and detected profile");return 0;
}
