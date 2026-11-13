#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio/mzf_writer.h"
#include "fixtures/loader_golden.h"
static uint8_t original[128],data[65535];
static const uint16_t timing[13][4]={
 {3807,4087,7506,7786},{1818,2228,3753,4164},{1407,1994,2815,3577},
 {3840,4224,7424,7904},{1280,1280,2560,2560},{1232,1877,2522,2874},
 {1407,1994,2815,3577},{1818,2228,3753,4164},{1687,1673,3360,3360},
 {2269,2255,4524,4524},{1232,1877,2522,2874},{3753,4222,7506,7917},{4015,4000,8015,8015}};
static unsigned hex(char c){return c>='a' ? (unsigned)(c-'a'+10):(unsigned)(c-'0');}
static void decode(const char *s,uint8_t *out,size_t n){assert(strlen(s)==n*2);for(size_t i=0;i<n;++i)out[i]=(uint8_t)((hex(s[i*2])<<4)|hex(s[i*2+1]));}
static bool interval(unsigned profile,bool bit,bool invert) {
 unsigned i=bit ? 2:0;
 if(!mzf_writer_feed(invert ? 1:0,(timing[profile][i]+8)/16)) return false;
 return mzf_writer_feed(invert ? 0:1,(timing[profile][i+1]+8)/16);
}
static bool bytes(unsigned profile,const uint8_t *p,size_t n,bool bad,bool invert) {
 uint16_t sum=0;for(size_t i=0;i<n;++i)for(unsigned b=0;b<8;++b)if(p[i]&(1U<<b))++sum;
 if(bad)sum^=1;
 for(size_t i=0;i<n+2;++i) {
  uint8_t v=i<n ? p[i]:i==n ? (uint8_t)(sum>>8):(uint8_t)sum;
  for(unsigned b=0;b<8;++b)if(!interval(profile,(v&(0x80U>>b))!=0,invert))return false;
  if(!interval(profile,true,invert))return false;
 }return true;
}
static bool block(unsigned profile,const uint8_t *p,size_t n,bool header,bool bad,bool invert) {
 for(unsigned i=0;i<(header ? 11000U:5500U);++i)if(!interval(profile,false,invert))return false;
 for(unsigned i=0;i<(header ? 40U:20U);++i)if(!interval(profile,true,invert))return false;
 for(unsigned i=0;i<(header ? 40U:20U);++i)if(!interval(profile,false,invert))return false;
 for(unsigned i=0;i<2;++i)if(!interval(profile,true,invert))return false;
 return bytes(profile,p,n,bad,invert);
}
static void check_file(const uint8_t *h,size_t n) {
 assert(mzf_writer_complete() && mzf_writer_progress()==100 && mzf_writer_finalize());
 FILE *f=fopen("test.MZF","rb");assert(f);uint8_t actual[128];assert(fread(actual,1,128,f)==128);
 if(memcmp(actual,h,128)){for(unsigned i=0;i<128;++i)if(actual[i]!=h[i])printf("header %u: %u != %u\n",i,actual[i],h[i]);}
 assert(!memcmp(actual,h,128));
 for(size_t i=0;i<n;++i)assert(fgetc(f)==data[i]);assert(fgetc(f)==EOF && !ferror(f));assert(!fclose(f));assert(!remove("test.MZF"));
}
static void all_profiles(void) {
 static const unsigned modes[]={0,1,6,7,8,9,10,11,12,13,14,15,16};
 static const unsigned profiles[]={0,0,5,6,7,8,9,1,2,3,10,11,12};
 for(unsigned k=0;k<sizeof(modes)/sizeof(modes[0]);++k) {
  unsigned mode=modes[k];const struct loader_golden *g=&loader_golden[mode];uint8_t h[128],ldr[128];
  decode(g->header_hex,h,128);decode(g->loader_hex,ldr,g->size);
  assert(mzf_writer_open("test.MZF"));assert(block(g->active ? 0:profiles[k],h,128,true,false,false));
  if(mode==9 || mode==10 || mode==16)assert(block(0,ldr,g->size,false,false,false));
  assert(block(profiles[k],data,4,false,false,false));
  check_file(original,4);
  loader_mode_t detected;assert(mzf_writer_profile(&detected));assert((unsigned)detected==(mode==1 || mode==13 ? 0:mode));
 }
 puts("PASS MZF recording NORMAL 1:1..1:4, MZ700, all IC/TC: canonical header and payload, reference loader normalization and detected profile");
}
static void recovery(void) {
 assert(mzf_writer_open("test.MZF"));assert(block(0,original,128,true,false,false));assert(block(0,data,4,false,true,false));
 assert(!mzf_writer_complete());for(unsigned i=0;i<256;++i)assert(interval(0,false,false));assert(bytes(0,data,4,false,false));check_file(original,4);
 assert(mzf_writer_open("test.MZF"));assert(block(0,original,128,true,true,false));for(unsigned i=0;i<256;++i)assert(interval(0,false,false));
 assert(bytes(0,original,128,false,false));assert(block(0,data,4,false,false,false));check_file(original,4);
 assert(mzf_writer_open("test.MZF"));assert(block(0,original,128,true,false,false));assert(block(0,data,4,false,true,false));
 assert(mzf_writer_feed(0,1000000));assert(block(0,data,4,false,false,false));check_file(original,4);
 puts("PASS MZF duplicate header/data recovery and reacquisition after signal break");
}
static void invalid(void) {
 assert(mzf_writer_open("test.MZF"));assert(block(0,original,128,true,false,false));assert(block(0,data,4,false,true,false));
 for(unsigned i=0;i<256;++i)assert(interval(0,false,false));assert(!bytes(0,data,4,true,false));assert(!strcmp(mzf_writer_error(),"MZF CHECKSUM"));
 mzf_writer_abort_delete();assert(!fopen("test.MZF","rb"));
 assert(mzf_writer_open("test.MZF"));assert(!mzf_writer_finalize());assert(!fopen("test.MZF","rb"));
 FILE *f=fopen("test.MZF","wb");assert(f);assert(fputs("preserve",f)>=0 && !fclose(f));assert(!mzf_writer_open("test.MZF"));
 f=fopen("test.MZF","rb");assert(f && fgetc(f)=='p');fclose(f);assert(!remove("test.MZF"));
 assert(mzf_writer_open("test.MZF"));assert(mzf_writer_feed(0,UINT32_MAX));assert(block(0,original,128,true,false,false));
 assert(!mzf_writer_finalize());assert(!fopen("test.MZF","rb"));
 puts("PASS MZF incomplete/checksum errors delete provisional output; exclusive create preserves collisions; long silence cannot wrap");
}
static void boundaries(void) {
 uint8_t h[128];memcpy(h,original,128);
 for(unsigned pass=0;pass<2;++pass) {
  size_t n=pass ? 65535:0;h[18]=(uint8_t)n;h[19]=(uint8_t)(n>>8);
  for(size_t i=0;i<n;++i)data[i]=(uint8_t)(i*37);
  assert(mzf_writer_open("test.MZF"));assert(block(0,h,128,true,false,false));assert(block(0,data,n,false,false,false));check_file(h,n);
 }
 puts("PASS MZF zero-length and maximum 65535-byte payload, bounded workspace and exact output size");
}
int main(void) {
 mzf_writer_init(); original[0]=1;memcpy(original+1,"TEST\r",5);original[18]=4;original[21]=original[23]=0x12;
 data[0]=0;data[1]=128;data[2]=1;data[3]=255;
 all_profiles();recovery();invalid();boundaries();return 0;
}
