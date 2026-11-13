#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "audio/wav_writer.h"
#include "audio/edge_writer.h"
#include "audio/mzf_writer.h"
#include "formats/sidecar.h"
extern int io_failure;
static void missing(const char *name) {FILE *f=fopen(name,"rb");assert(!f);}
static void mzf_pulse(bool bit) {assert(mzf_writer_feed(0,bit ? 469:238));}
static void mzf_block(const uint8_t *bytes,size_t count,bool header) {
 for(unsigned i=0;i<(header ? 11000U:5500U);++i)mzf_pulse(false);
 for(unsigned i=0;i<(header ? 40U:20U);++i)mzf_pulse(true);
 for(unsigned i=0;i<(header ? 40U:20U);++i)mzf_pulse(false);mzf_pulse(true);mzf_pulse(true);
 uint16_t sum=0;for(size_t i=0;i<count;++i)for(unsigned b=0;b<8;++b)if(bytes[i]&(1U<<b))++sum;
 for(size_t i=0;i<count+2;++i) {uint8_t v=i<count ? bytes[i]:i==count ? (uint8_t)(sum>>8):(uint8_t)sum;
  for(unsigned bit=0;bit<8;++bit)mzf_pulse((v&(0x80U>>bit))!=0);mzf_pulse(true);}
}
static void mzf_ready(void) {
 uint8_t header[128]={1},data=0xA5;memcpy(header+1,"IO TEST\r",8);header[18]=1;header[21]=header[23]=0x12;
 assert(mzf_writer_open("failure.MZF"));mzf_block(header,128,true);mzf_block(&data,1,false);assert(mzf_writer_complete());
}
int main(void) {
 uint8_t byte=128;mzf_writer_init();
 for(int fault=1;fault<=4;++fault) {
  io_failure=0;assert(wav_writer_open("failure.WAV",44100,8,1));assert(wav_writer_write(&byte,1)==1);io_failure=fault;
  assert(!wav_writer_finalize());io_failure=0;missing("failure.WAV");assert(!wav_writer_is_open());
  assert(edge_writer_open("failure.L16",62500));assert(edge_writer_write_interval_units(0,128));io_failure=fault;
  assert(!edge_writer_finalize());io_failure=0;missing("failure.L16");
  if(fault!=2) {mzf_ready();io_failure=fault;assert(!mzf_writer_finalize());io_failure=0;missing("failure.MZF");}
 }
 io_failure=1;assert(!wav_writer_open("failure.WAV",44100,8,1));io_failure=0;missing("failure.WAV");
 assert(wav_writer_open("limit.WAV",44100,8,1));assert(!wav_writer_write(&byte,SIZE_MAX));assert(wav_writer_finalize());assert(!remove("limit.WAV"));
 const char *names[]={"keep.WAV","keep.L16","keep.MZF"};
 for(unsigned i=0;i<3;++i) {FILE *f=fopen(names[i],"wb");assert(f && fputs("preserve",f)>=0 && !fclose(f));}
 assert(!wav_writer_open(names[0],44100,8,1));assert(!edge_writer_open(names[1],62500));assert(!mzf_writer_open(names[2]));
 for(unsigned i=0;i<3;++i) {FILE *f=fopen(names[i],"rb");assert(f && fgetc(f)=='p');fclose(f);assert(!remove(names[i]));}
 io_failure=3;assert(!sidecar_write_mzf("metadata.MZF",LOADER_MODE_IC_1_3));io_failure=0;missing("metadata.MFI");
 puts("PASS injected write/seek/flush/close failures: WAV/LEP-L16/MZF provisional files removed, no stale handles; MFI failure isolated");
 puts("PASS exclusive creation preserves all recording collisions and oversized WAV write cannot wrap RIFF/FAT32 length");return 0;
}
