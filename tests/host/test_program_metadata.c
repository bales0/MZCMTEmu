#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "formats/program_metadata.h"
#include "formats/tape_metadata.h"
#include "record/record_autoname.h"
#include "audio/tap_stream.h"
#include "audio/edge_writer.h"
#include "audio/edge_stream.h"
#include "audio/wav_writer.h"
#include "audio/wav_reader.h"
#include "audio/wav_pulse_stream.h"
#include "fixtures/loader_golden.h"
static unsigned hex(char c){return c>='a' ? (unsigned)(c-'a'+10):(unsigned)(c-'0');
}
static void unhex(const char *s,uint8_t *p,size_t n){for(size_t i=0;i<n;++i)p[i]=(uint8_t)((hex(s[2*i])<<4)|hex(s[2*i+1]));
}
static uint8_t native[128];
static void structured(void){
 for(unsigned f=0;f<4;++f)for(unsigned mode=0;mode<17;++mode){
  file_format_t format=f==0 ? FILE_FORMAT_MZF:f==1 ? FILE_FORMAT_MZT:f==2 ? FILE_FORMAT_M12:FILE_FORMAT_MZQ;
  uint8_t h[128],loader[128];
  unhex(loader_golden[mode].header_hex,h,128);
  unhex(loader_golden[mode].loader_hex,loader,loader_golden[mode].size);
  program_metadata_t m;
  program_metadata_reset(&m,format);
  bool tc=mode==9 || mode==10 || mode==16;
  if(tc){assert(!program_metadata_source_sharp(&m,h,NULL,0));
  assert(m.confidence==PROGRAM_LOADER_ONLY && !(m.valid&(PROGRAM_SIZE|PROGRAM_LOAD|PROGRAM_EXEC)));
  }
  if(mode==2 || mode==3 || mode==4 || mode==5)continue;
  // generated transport-only headers are tested at original source boundary
  assert(program_metadata_source_sharp(&m,h,loader,loader_golden[mode].size));
  assert(m.format==format && m.protocol==PROGRAM_SHARP && (m.valid&PROGRAM_EXEC));
  if(mode==6 || mode==7 || mode==8 || mode==9 || mode==10 || mode>=14)assert(m.declared_size==4 && m.load==0x1200 && m.exec==0x1200);
  if(tc){loader[0]^=1;
  program_metadata_reset(&m,format);
  assert(!program_metadata_source_sharp(&m,h,loader,90) && m.confidence==PROGRAM_LOADER_ONLY);
  }
 }
 program_metadata_t m,out;
 program_metadata_store_t store={0};
 program_metadata_reset(&m,FILE_FORMAT_MZF);
 program_metadata_sharp(&m,native,PROGRAM_MZF_HEADER);
 program_metadata_publish(&store,&m);
 program_metadata_snapshot(&store,&out);
 assert(!memcmp(&m,&out,sizeof(m)) && sizeof(m)<=128);
}
static const uint16_t timings[4][4]={{3807,4087,7506,7786},{1232,1877,2522,2874},{2269,2255,4524,4524},{3753,4222,7506,7917}};
static bool passive;
static file_format_t fmt;
static bool writing_file;
static uint64_t file_us,file_samples;
static void duration(unsigned level,unsigned us){
 if(writing_file){
  if(fmt==FILE_FORMAT_WAV){
   file_us+=us;
   uint64_t end=(file_us*44100+500000)/1000000;
   uint8_t samples[512];
   memset(samples,level ? 255:0,sizeof(samples));
   while(file_samples<end){size_t n=end-file_samples>sizeof(samples) ? sizeof(samples):(size_t)(end-file_samples);
   assert(wav_writer_write(samples,n)==n);
   file_samples+=n;
   }
  }
else{unsigned unit=fmt==FILE_FORMAT_LEP ? 50:16;
  assert(edge_writer_write_interval_units((uint8_t)level,(us+unit/2)/unit));
  }
  return;
 }
 if(fmt==FILE_FORMAT_LEP)us=((us+25)/50)*50;
 else if(fmt==FILE_FORMAT_L16)us=((us+8)/16)*16;
 if(passive){ // split same-level source chunks; the analyzer must reunite them
  unsigned half=us/2;
  tape_metadata_feed((uint8_t)level,half,false);
  tape_metadata_feed((uint8_t)level,us-half,false);
 }
else record_autoname_feed_level_interval((uint16_t)((us+8)/16),(uint8_t)level);
}
static void pulse(unsigned profile,bool bit){unsigned p=bit ? 2:0;
duration(0,(timings[profile][p]+8)/16);
duration(1,(timings[profile][p+1]+8)/16);
}
static void block(unsigned profile,const uint8_t *p,size_t n,bool header,bool bad){
 for(unsigned i=0;i<(header ? 11000U:5500U);++i)pulse(profile,false);
 for(unsigned i=0;i<(header ? 40U:20U);++i)pulse(profile,true);
 for(unsigned i=0;i<(header ? 40U:20U);++i)pulse(profile,false);
 pulse(profile,true);
 pulse(profile,true);
 uint16_t sum=0;
 for(size_t i=0;i<n;++i)for(unsigned b=0;b<8;++b)if(p[i]&(1U<<b))++sum;
 if(bad)sum^=1;
 for(size_t i=0;i<n+2;++i){uint8_t v=i<n ? p[i]:i==n ? (uint8_t)(sum>>8):(uint8_t)sum;
 for(unsigned b=0;b<8;++b)pulse(profile,(v&(0x80U>>b))!=0);
 pulse(profile,true);
 }
}
static void passive_cases(void){
 static const unsigned modes[]={0,6,9};
 uint8_t data[]={0,128,1,255};
 for(unsigned f=0;f<3;++f)for(unsigned k=0;k<3;++k){
  fmt=f==0 ? FILE_FORMAT_WAV:f==1 ? FILE_FORMAT_LEP:FILE_FORMAT_L16;
  unsigned mode=modes[k];
  uint8_t h[128],loader[128];
  unhex(loader_golden[mode].header_hex,h,128);
  unhex(loader_golden[mode].loader_hex,loader,loader_golden[mode].size);
  program_metadata_t observed[2];
  for(unsigned side=0;side<2;++side){passive=side!=0;
  if(passive){tape_metadata_begin(fmt);
  tape_metadata_feed(0,300000,true);
  }
else record_autoname_begin(true,FILE_FORMAT_L16,0);
   block(0,h,128,true,false);
   if(mode==9){program_metadata_t pending;
   if(passive)tape_metadata_snapshot(&pending);
   else record_autoname_metadata(&pending);
   assert(pending.confidence==PROGRAM_LOADER_ONLY && !(pending.valid&PROGRAM_SIZE));
   block(0,loader,90,false,false);
   }
   block(k,data,4,false,false);
   if(passive)tape_metadata_end();
   if(passive)tape_metadata_snapshot(&observed[side]);
   else record_autoname_metadata(&observed[side]);
   assert(observed[side].confidence==PROGRAM_PAYLOAD_VALID && observed[side].declared_size==4 && observed[side].load==0x1200 && observed[side].exec==0x1200);
  }
  assert(observed[0].valid==observed[1].valid && !strcmp(observed[0].title,observed[1].title));
 }
 fmt=FILE_FORMAT_L16;
 passive=true;
 tape_metadata_begin(fmt);
 tape_metadata_feed(0,300000,true);
 tape_metadata_end();
 program_metadata_t m;
 tape_metadata_snapshot(&m);
 assert(!m.valid && m.confidence==PROGRAM_UNKNOWN);
}

static void source_file_cases(void){
 static const unsigned modes[]={0,6,9};
 uint8_t data[]={0,128,1,255};
 for(unsigned f=0;f<3;++f)for(unsigned k=0;k<3;++k){
  fmt=f==0 ? FILE_FORMAT_WAV:f==1 ? FILE_FORMAT_LEP:FILE_FORMAT_L16;
  const char *path=f==0 ? "signal.wav":f==1 ? "signal.lep":"signal.l16";
  uint8_t h[128],loader[128];
  unsigned mode=modes[k];
  unhex(loader_golden[mode].header_hex,h,128);
  unhex(loader_golden[mode].loader_hex,loader,loader_golden[mode].size);
  if(f==0)assert(wav_writer_open(path,44100,8,1));
  else assert(edge_writer_open(path,f==1 ? 20000:62500));
  writing_file=true;
  file_us=file_samples=0;
  block(0,h,128,true,false);
  if(mode==9)block(0,loader,90,false,false);
  block(k,data,4,false,false);
  duration(0,100000);
  writing_file=false;
  if(f==0)assert(wav_writer_finalize());
  else assert(edge_writer_finalize());
  tape_metadata_begin(fmt);
  uint8_t level;
  uint32_t width;
  bool count;
  if(f==0){
   wav_info_t info;
   assert(wav_reader_open(path,&info) && wav_pulse_stream_begin(info.sample_rate));
   while(wav_pulse_stream_next_ticks16(&level,&width))tape_metadata_feed(level,(width+8)/16,false);
   assert(wav_pulse_stream_eof() && !wav_pulse_stream_error());
   wav_reader_close();
  }
else{
   edge_stream_info_t info;
   assert(edge_stream_open(path,&info));
   while(edge_stream_next_pulse(&level,&width,&count))tape_metadata_feed(level,width,edge_stream_monitor_muted());
   assert(!edge_stream_has_error());
   edge_stream_close();
  }
  tape_metadata_end();
  program_metadata_t m;
  tape_metadata_snapshot(&m);
  // IC sampled at 44.1 kHz validates its embedded header; do not assert
  // a payload checksum the quantized fast waveform cannot establish.
  assert(m.confidence==(f==0 && k==1 ? PROGRAM_HEADER_VALID:PROGRAM_PAYLOAD_VALID));
  assert(m.declared_size==4 && m.load==0x1200 && m.exec==0x1200);
  assert(!remove(path));
 }
 puts("PASS actual generated WAV44k/LEP/L16 files through PLAY source readers: native, IC and TC first payload; source quantization and synthetic tail exclusion");
}

static void sequence_and_recovery(void){
 uint8_t h[128],loader[128],data[]={0,128,1,255};
 program_metadata_t m;
 fmt=FILE_FORMAT_L16;
 passive=false;
 record_autoname_begin(true,FILE_FORMAT_L16,0);
 memcpy(h,native,128);
 memset(h+1,0,17);
 memcpy(h+1,"FIRST",5);
 block(0,h,128,true,false);
 block(0,data,4,false,false);
 record_autoname_break_signal();
 memset(h+1,0,17);
 memcpy(h+1,"SECOND",6);
 h[20]=0;
 h[21]=0x13;
 h[22]=0;
 h[23]=0x24;
 block(0,h,128,true,false);
 block(0,data,4,false,false);
 record_autoname_metadata(&m);
 assert(m.confidence==PROGRAM_PAYLOAD_VALID && m.record==2 && !strcmp(m.title,"SECOND") && m.load==0x1300 && m.exec==0x2400);
 assert(!strcmp(record_autoname_get_name(),"FIRST"));
 record_autoname_begin(true,FILE_FORMAT_L16,0);
 unhex(loader_golden[9].header_hex,h,128);
 unhex(loader_golden[9].loader_hex,loader,90);
 block(0,h,128,true,false);
 block(0,loader,90,false,true);
 record_autoname_metadata(&m);
 assert(m.confidence==PROGRAM_INVALID && !(m.valid&PROGRAM_SIZE) && !record_autoname_has_name());
 block(0,native,128,true,false);
 block(0,data,4,false,false);
 record_autoname_metadata(&m);
 assert(m.confidence==PROGRAM_PAYLOAD_VALID);
 record_autoname_begin(true,FILE_FORMAT_L16,0);
 block(0,native,128,true,false);
 block(0,data,4,false,true);
 block(0,data,4,false,false);
 record_autoname_metadata(&m);
 assert(m.confidence==PROGRAM_PAYLOAD_VALID);
 puts("PASS multiple logical programs update metadata while keeping first filename; bad TC loader cannot publish target fields; native checksum second-copy recovery");
}
static void tap_header(uint8_t h[19],unsigned type,const char *title,unsigned param){memset(h,0,19);
h[1]=(uint8_t)type;
memcpy(h+2,title,10);
h[12]=4;
h[14]=(uint8_t)param;
h[15]=(uint8_t)(param>>8);
for(unsigned i=0;i<18;++i)h[18]^=h[i];
}
static void zx_cases(void){
 uint8_t h[19];
 program_metadata_t m;
 for(unsigned type=0;type<4;++type){tap_header(h,type,"ZX TEST   ",type ? 32768:10);
 program_metadata_reset(&m,FILE_FORMAT_TAP);
 assert(program_metadata_zx(&m,h));
 assert(m.protocol==PROGRAM_SPECTRUM && !(m.valid&PROGRAM_EXEC));
 assert((m.valid&PROGRAM_AUTOSTART)!=0 ? type==0:true);
 if(type==3)assert((m.valid&PROGRAM_LOAD) && m.load==32768);
 h[18]^=1;
 assert(!program_metadata_zx(&m,h));
 }
 FILE *f=fopen("metadata.tap","wb");
 assert(f);
 tap_header(h,3,"FIRST     ",32768);
 uint8_t len[2]={19,0};
 assert(fwrite(len,1,2,f)==2 && fwrite(h,1,19,f)==19);
 uint8_t d[]={6,0,255,0,128,1,255,129};
 // XOR payload+flag+checksum=0
 d[7]=255^0^128^1^255;
 assert(fwrite(d,1,sizeof(d),f)==sizeof(d));
 tap_header(h,0,"SECOND    ",123);
 assert(fwrite(len,1,2,f)==2 && fwrite(h,1,19,f)==19 && !fclose(f));
 assert(tap_stream_open("metadata.tap",TAP_SPEED_1800,NULL));
 tap_stream_metadata(&m);
 assert(!strcmp(m.title,"FIRST") && m.record==1);
 uint8_t level;
 uint32_t ticks;
 bool count;
 unsigned seen=1;
 while(tap_stream_next_pulse(&level,&ticks,&count)){tap_stream_metadata(&m);
 if(m.record==2){seen|=2;
 assert(!strcmp(m.title,"FIRST"));
 }
if(m.record==3){seen|=4;
 assert(!strcmp(m.title,"SECOND") && (m.valid&PROGRAM_AUTOSTART) && m.autostart==123);
 }}
 assert(seen==7);
 tap_stream_close();
 assert(!remove("metadata.tap"));
}
int main(void){unhex(loader_golden[0].header_hex,native,128);
structured();
passive_cases();
source_file_cases();
sequence_and_recovery();
zx_cases();
puts("PASS shared metadata snapshots, original Sharp/IC/TC first payload, passive PLAY/RECORD parity for WAV/LEP/L16 and strict multi-block ZX BASIC/CODE/ARRAY/XOR without invented EXEC");
return 0;
}
