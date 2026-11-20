#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio/sharp_stream.h"
#include "formats/sidecar.h"
#include "formats/mz_tape_profiles.h"
#include "play/mzf_loader.h"
#include "fixtures/loader_golden.h"
#include "fixtures/loader_high_golden.h"

static uint8_t original[128],payload[]={0,0x80,1,255};
static uint64_t total_ticks;
static const uint16_t timings[13][4]={
 {3807,4087,7506,7786},{1818,2228,3753,4164},{1407,1994,2815,3577},
 {3840,4224,7424,7904},{1280,1280,2560,2560},{1232,1877,2522,2874},
 {1407,1994,2815,3577},{1818,2228,3753,4164},{1687,1673,3360,3360},
 {2269,2255,4524,4524},{1232,1877,2522,2874},{3753,4222,7506,7917},{4015,4000,8015,8015}};
static unsigned hex(char c) { return c>='a' ? (unsigned)(c-'a'+10) : (unsigned)(c-'0');
}
static void decode(const char *s,uint8_t *dst,size_t n) {
 assert(strlen(s)==n*2);
 for(size_t i=0;i<n;++i) dst[i]=(uint8_t)((hex(s[i*2])<<4)|hex(s[i*2+1]));
}
static void write_file(const char *name,const uint8_t *bytes,size_t n) {
 FILE *f=fopen(name,"wb");
 assert(f);
 assert(fwrite(bytes,1,n,f)==n);
 assert(!fclose(f));
}
static void write_text(const char *name,const char *text) { write_file(name,(const uint8_t *)text,strlen(text));
}
static void fixture(const char *name,const uint8_t *h) {
 FILE *f=fopen(name,"wb");
 assert(f);
 assert(fwrite(h,1,128,f)==128);
 assert(fwrite(payload,1,sizeof(payload),f)==sizeof(payload) && !fclose(f));
}
static void interval(uint8_t level,uint16_t ticks,bool count) {
 uint8_t l=99;
 uint32_t t=0;
 bool c=!count;
 assert(sharp_stream_next_pulse(&l,&t,&c));
 assert(l==level && t==ticks && c==count);
 total_ticks+=t;
}
static void cycle(unsigned profile,bool context,unsigned ul_leader,bool long_bit,unsigned region,bool next) {
 uint16_t high=timings[profile][long_bit ? 2:0],low=timings[profile][long_bit ? 3:1];
 if(region==0 && ul_leader) { high=ul_leader==2 ? 3840:3807;
 low=ul_leader==2 ? 4224:4141;
 }
 else if(context) {
  if(region==0) low=4141;
  else if(region==1) low=long_bit ? 7795:4096;
  else low=long_bit ? (next ? 7741:7786) : (next ? 4042:4087);
 }
 interval(0,high,false);
 interval(1,low,true);
}
static void stage(const uint8_t *bytes,size_t n,unsigned profile,bool context,bool header,unsigned ul_leader,bool tc) {
 for(unsigned i=0;i<(header && !ul_leader ? 11000U:5500U);++i) cycle(profile,context,ul_leader,false,0,false);
 for(unsigned i=0;i<(header ? 40U:20U);++i) cycle(profile,context,ul_leader,true,1,false);
 for(unsigned i=0;i<(header ? 40U:20U);++i) cycle(profile,context,ul_leader,false,1,false);
 for(unsigned i=0;i<2;++i) cycle(profile,context,ul_leader,true,1,false);
 uint16_t checksum=0;
 for(size_t i=0;i<n;++i) for(unsigned j=0;j<8;++j) if(bytes[i]&(0x80U>>j)) ++checksum;
 for(size_t i=0;i<n+2;++i) {
  uint8_t b=i<n ? bytes[i] : i==n ? (uint8_t)(checksum>>8):(uint8_t)checksum;
  for(unsigned bit=0;bit<8;++bit) cycle(profile,context,ul_leader,(b&(0x80U>>bit))!=0,2,
      bit==7 ? true : (b&(0x40U>>bit))!=0);
  bool next=i+1<n ? (bytes[i+1]&0x80)!=0 : i+1==n ? (checksum&0x8000)!=0 : i==n ? (checksum&0x80)!=0 : !tc;
  cycle(profile,context,ul_leader,true,2,next);
 }
 for(unsigned i=0;i<(tc ? 98U:2U);++i) cycle(profile,context,ul_leader,!tc,2,!tc && i==0);
 assert(sharp_stream_at_boundary());
}
static unsigned mode_profile(int mode) {
 switch(mode) {
 case 11:return 1;
 case 12:return 2;
 case 13:return 3;
 case 14:return 10;
 case 5:return 4;
 case 6:return 5;
 case 7:return 6;
 case 8:return 7;
 case 9:return 8;
 case 10:return 9;
 case 15:return 11;
 case 16:return 12;
 default:return 0;
 }
}
static void handshake(int mode) {
 uint64_t now=10;
 assert(sharp_stream_ul_ready() && sharp_stream_begin_ul(now));
 assert(sharp_stream_bus_sense()==1);
 if(mode==4) {
  assert(sharp_stream_bus_read()==1);
  assert(sharp_stream_service_ul(++now,1));
  assert(sharp_stream_bus_read()==0);
 } else {
  assert(sharp_stream_bus_read()==(mode==3 ? 0:1));
  now+=mode==3 ? 700000:100000;
  assert(sharp_stream_service_ul(now,1));
  if(mode==3) { assert(sharp_stream_bus_read()==1);
  now+=1000;
  assert(sharp_stream_service_ul(now,1));
  assert(sharp_stream_service_ul(++now,0));
  }
 }
 uint8_t held_read=sharp_stream_bus_read(),held_sense=sharp_stream_bus_sense();
 sharp_stream_pause_ul(now,true);
 assert(sharp_stream_service_ul(now+10000000,1));
 assert(sharp_stream_bus_read()==held_read && sharp_stream_bus_sense()==held_sense);
 sharp_stream_pause_ul(now+10000000,false);
 now+=10000000;
 for(size_t i=0;i<sizeof(payload);++i) {
  uint8_t value=(uint8_t)~payload[i];
  value=(uint8_t)((value<<2)|(value>>6));
  for(unsigned pair=0;pair<4;++pair) {
   assert(sharp_stream_service_ul(++now,0));
   assert(sharp_stream_bus_sense()==0 && sharp_stream_bus_read()==((value&0x80)!=0));
   assert(sharp_stream_service_ul(++now,1));
   assert(sharp_stream_bus_sense()==1 && sharp_stream_bus_read()==((value&0x40)!=0));
   value<<=2;
  }
  assert(sharp_stream_ul_bytes()==i+1);
 }
 assert(sharp_stream_service_ul(++now,0));
 assert(sharp_stream_is_eof() && sharp_stream_bus_read()==0 && sharp_stream_bus_sense()==1);
}
static void all_modes(const char *path) {
 if(!strcmp(path,"test.mzf")) fixture(path,original);
 for(int mode=0;mode<17;++mode) {
  const struct loader_golden *g=&loader_golden[mode];
  uint8_t h[128],ldr[128];
  memcpy(h,original,128);
  bool active=mzf_loader_prepare(FILE_FORMAT_MZF,(loader_mode_t)mode,h,132,128);
  assert(active==(g->active!=0) && (int)mzf_loader_get_variant()==g->variant);
  if(active) {
   assert(mzf_loader_build_loader(ldr,sizeof(ldr))==g->size);
   uint8_t wanted[128];
   decode(g->loader_hex,wanted,g->size);
   assert(!memcmp(ldr,wanted,g->size));
   assert(mzf_loader_patch_loader_header(h));
  }
  uint8_t expected_header[128];
  decode(g->header_hex,expected_header,128);
  assert(!memcmp(h,expected_header,128));
  sharp_stream_info_t info;
  assert(sharp_stream_open(path,1,(loader_mode_t)mode,&info));
  assert(info.record==1 && info.records==1 && !strcmp(info.title,"TEST"));
  assert((int)info.effective==(mode==1 ? 0:mode) && !info.fallback && info.ultrafast==(mode>=2 && mode<=4));
  unsigned profile=mode_profile(mode),base=active ? (mode==5 ? 3:0):profile;
  unsigned ul=mode>=2 && mode<=4 ? (mode==4 ? 2:1):0;
  total_ticks=0;
  stage(expected_header,128,base,base==0,true,ul,false);
  if(mode==3 || mode==4) { assert(total_ticks==info.total_ticks);
  handshake(mode);
  }
  else {
   assert(sharp_stream_advance());
   if(mode==2 || mode==9 || mode==10 || mode==16) {
    stage(ldr,g->size,base,base==0,false,ul,mode!=2);
    if(mode==2) { assert(total_ticks==info.total_ticks);
    handshake(mode);
    sharp_stream_close();
    continue;
    }
    assert(sharp_stream_advance());
   }
   stage(payload,4,active ? profile:base,active ? false:base==0,false,0,mode==9 || mode==10 || mode==16);
   assert(total_ticks==info.total_ticks);
   assert(sharp_stream_advance());
   assert(sharp_stream_is_eof());
  }
  assert(!sharp_stream_has_error());
  sharp_stream_close();
 }
 assert(!remove(path));
 puts("PASS Sharp all 17 modes: reference loader bytes, exact per-region waveform, checksum and physical polarity; UL handshakes");
}
static void high_loaders(void) {
 uint8_t h[128],expected[128],bytes[128],loader_expected[128];
 for(int mode=0;mode<17;++mode) {
  memcpy(h,original,128);
  h[0x14]=h[0x16]=0x10;
  h[0x15]=h[0x17]=0x11;
  const struct loader_golden *g=&loader_high_golden[mode];
  bool active=mzf_loader_prepare(FILE_FORMAT_MZF,(loader_mode_t)mode,h,132,128);
  assert(active==(g->active!=0) && (int)mzf_loader_get_variant()==g->variant);
  if(active) {
   assert(mzf_loader_build_loader(bytes,sizeof(bytes))==g->size);
   decode(g->loader_hex,loader_expected,g->size);
   assert(!memcmp(bytes,loader_expected,g->size));
   assert(mzf_loader_patch_loader_header(h));
  }
  decode(g->header_hex,expected,128);
  assert(!memcmp(h,expected,128));
 }
 puts("PASS reference HIGH-placement loader vectors: saved stack, header relocators, MZ700 runtime and unsafe IC refusal");
}
static void metadata(void) {
 assert(!sidecar_exists(NULL) && !sidecar_exists("name.mzf") && !sidecar_exists("name.mzt"));
 write_text("invalid.MFI","BROKEN SIDECAR");
 loader_mode_t rejected;assert(sidecar_exists("invalid.mzf") && !sidecar_read("invalid.mzf",1,&rejected));
 assert(!sidecar_exists("invalid.qd") && !sidecar_exists("invalid.wav"));assert(!remove("invalid.MFI"));
 write_text("lower.m2i","TYPE=NORMAL\nSPEED=1:1\n");assert(sidecar_exists("lower.m12"));assert(!remove("lower.m2i"));
 loader_mode_t mode=LOADER_MODE_UL;
 bool used=true;
 assert(sidecar_resolve("name.mzf",1,LOADER_MODE_AUTO,&used)==LOADER_MODE_NORMAL_1_1 && !used);
 write_text("name.MFI","TYPE=ic\r\nSPEED=1:3\r\n");
 assert(sidecar_exists("name.mzf"));
 assert(sidecar_read("name.mzf",1,&mode) && mode==LOADER_MODE_IC_1_3);
 assert(sidecar_resolve("name.mzf",1,LOADER_MODE_TC_1_2,&used)==LOADER_MODE_TC_1_2 && !used);
 assert(!sidecar_read("name.m12",1,&mode));
 write_text("name.M2I","TYPE=UL_MZ700\n");
 assert(sidecar_exists("name.m12"));
 assert(sidecar_read("name.m12",1,&mode) && mode==LOADER_MODE_UL_MZ700);
 write_text("name.MTI","RECORD=1\nTYPE=NORMAL\nSPEED=1:4\nRECORD=2\nTYPE=TC\nSPEED=1:4\nRECORD=3\nTYPE=UL_MZ800");
 assert(sidecar_exists("name.mzt"));
 assert(sidecar_read("name.mzt",1,&mode) && mode==LOADER_MODE_NORMAL_1_4);
 assert(sidecar_resolve("name.mzt",2,LOADER_MODE_AUTO,&used)==LOADER_MODE_NORMAL_1_1 && !used);
 assert(sidecar_read("name.mzt",3,&mode) && mode==LOADER_MODE_UL_MZ800);
 write_text("name.MTI","RECORD=1\nTYPE=IC\nRECORD=12345678901234567890123456789\nSPEED=1:3\n");
 assert(!sidecar_read("name.mzt",1,&mode));
 assert(!sidecar_read("name.mzt",0,&mode));
 assert(!remove("name.MFI") && !remove("name.M2I") && !remove("name.MTI"));
 assert(!sidecar_exists("name.mzf") && !sidecar_exists("name.m12") && !sidecar_exists("name.mzt"));
 write_text("name.MZI","TYPE=UL\n");
 assert(!sidecar_read("name.mzf",1,&mode));
 assert(!remove("name.MZI"));
 for(int written=0;written<17;++written) {
  if(written>=1 && written<=5)continue;
  assert(sidecar_write_mzf("out.mzf",(loader_mode_t)written));
  assert(sidecar_read("out.mzf",1,&mode) && (int)mode==written);
  assert(!sidecar_write_mzf("out.mzf",LOADER_MODE_NORMAL_1_1));
  assert(sidecar_read("out.mzf",1,&mode) && (int)mode==written);
  assert(!remove("out.MFI"));
 }
 puts("PASS MFI/M2I/MTI manual priority, case-insensitive TYPE, per-record fallback and fail-closed boundaries; legacy MZI ignored");
}
static void bounds(void) {
 uint8_t bad[128];
 memcpy(bad,original,128);
 bad[0x14]=0x10;
 bad[0x15]=0x11;
 fixture("overlap.mzf",bad);
 sharp_stream_info_t info;
 assert(sharp_stream_open("overlap.mzf",1,LOADER_MODE_IC_1_4,&info));
 assert(info.fallback && info.effective==LOADER_MODE_NORMAL_1_1);
 uint8_t level;
 uint32_t ticks;
 bool count;
 assert(sharp_stream_next_pulse(&level,&ticks,&count) && !level && ticks==3807);
 assert(sharp_stream_next_pulse(&level,&ticks,&count) && level && ticks==4141);
 sharp_stream_close();
 assert(!remove("overlap.mzf"));
 FILE *f=fopen("records.mzt","wb");
 assert(f);
 assert(fwrite(original,1,128,f)==128 && fwrite(payload,1,4,f)==4);
 uint8_t second[128];
 memcpy(second,original,128);
 memcpy(second+1,"SECOND\r",7);
 assert(fwrite(second,1,128,f)==128 && fwrite(payload,1,4,f)==4 && !fclose(f));
 write_text("records.MTI","RECORD=1\nTYPE=NORMAL\nSPEED=1:2\nRECORD=2\nTYPE=IC\nSPEED=1:1\n");
 assert(sharp_stream_open("records.mzt",2,LOADER_MODE_AUTO,&info));
 assert(info.records==2 && info.record==2 && info.effective==LOADER_MODE_IC_1_1 && info.sidecar && !strcmp(info.title,"SECOND"));
 sharp_stream_close();
 assert(!sharp_stream_open("records.mzt",3,LOADER_MODE_AUTO,&info));
 sharp_stream_close();
 assert(!remove("records.mzt") && !remove("records.MTI"));
 fixture("timeout.mzf",original);
 assert(sharp_stream_open("timeout.mzf",1,LOADER_MODE_UL,&info));
 for(;;) {while(sharp_stream_next_pulse(&level,&ticks,&count)){} if(sharp_stream_ul_ready())break;
 assert(sharp_stream_advance());
 }
 assert(sharp_stream_begin_ul(0));
 assert(sharp_stream_service_ul(100000,1));
 assert(!sharp_stream_service_ul(600001,1));
 assert(sharp_stream_has_error() && !sharp_stream_bus_read() && sharp_stream_bus_sense());
 const char *saved_error=sharp_stream_error();
 assert(!strcmp(saved_error,"UL TIMEOUT"));
 sharp_stream_close();
 assert(!strcmp(saved_error,"UL TIMEOUT"));
 assert(!remove("timeout.mzf"));
 write_file("truncated.mzf",original,127);
 assert(!sharp_stream_open("truncated.mzf",1,LOADER_MODE_NORMAL_1_1,&info));
 sharp_stream_close();
 assert(!remove("truncated.mzf"));
 assert(!mzf_loader_prepare(FILE_FORMAT_MZF,LOADER_MODE_UL,original,UINT32_MAX,UINT32_MAX-1));
 assert(!mzf_loader_prepare(FILE_FORMAT_MZF,LOADER_MODE_COUNT,original,132,128));
 puts("PASS loader overlap recovery resets native timing, MZT per-record selection and MTI, truncated files, UL timeout and bounds overflow");
}
static void qd_fixture(void);
static bool cancel_analysis(void *context) { return *(bool *)context;
}
static void cancellation(void) {
 bool cancelled=true;
 sharp_stream_info_t info;
 qd_fixture();
 sharp_stream_set_analysis_control(cancel_analysis,&cancelled);
 assert(!sharp_stream_open("test.mzq",1,LOADER_MODE_NORMAL_1_1,&info));
 assert(sharp_stream_analysis_cancelled());
 cancelled=false;
 assert(sharp_stream_open("test.mzq",1,LOADER_MODE_NORMAL_1_1,&info));
 assert(!sharp_stream_analysis_cancelled());
 sharp_stream_close();
 assert(!remove("test.mzq"));
 fixture("test.mzf",original);
 cancelled=true;
 assert(!sharp_stream_open("test.mzf",1,LOADER_MODE_NORMAL_1_1,&info));
 assert(sharp_stream_analysis_cancelled());
 sharp_stream_close();
 assert(!remove("test.mzf"));
 sharp_stream_set_analysis_control(NULL,NULL);
 puts("PASS cancellable MZQ scan and Sharp duration analysis; clean retry without stale cancellation");
}
static void qd_fixture(void) {
 uint8_t h[64]={0};
 memcpy(h,original,18);
 h[20]=4;
 memcpy(h+22,original+20,42);
 FILE *f=fopen("test.mzq","wb");
 assert(f);
 const uint8_t count[]={0,0x16,0x16,0xA5,2,'C','R','C'};
 const uint8_t head[]={0,0x16,0x16,0xA5,0,64,0};
 const uint8_t body[]={0,0x16,0x16,0xA5,1,4,0};
 assert(fwrite(count,1,sizeof(count),f)==sizeof(count));
 assert(fwrite(head,1,sizeof(head),f)==sizeof(head));
 assert(fwrite(h,1,64,f)==64);
 assert(fwrite("CRC",1,3,f)==3);
 assert(fwrite(body,1,sizeof(body),f)==sizeof(body));
 assert(fwrite(payload,1,4,f)==4);
 assert(fwrite("CRC",1,3,f)==3);
 assert(!fclose(f));
}
static void ul800_startup_waits(void) {
 fixture("ul800-start.mzf",original);
 for(unsigned missing_ack=0;missing_ack<2;++missing_ack){
  sharp_stream_info_t info;uint8_t level;uint32_t ticks;bool count;
  assert(sharp_stream_open("ul800-start.mzf",1,LOADER_MODE_UL_MZ800,&info));
  while(sharp_stream_next_pulse(&level,&ticks,&count)){}
  assert(sharp_stream_ul_ready() && sharp_stream_begin_ul(0));
  assert(sharp_stream_bus_read()==0 && sharp_stream_bus_sense()==1);
  assert(sharp_stream_service_ul(699999,1) && sharp_stream_bus_read()==0);
  assert(sharp_stream_service_ul(700000,1) && sharp_stream_bus_read()==1);
  assert(sharp_stream_service_ul(700999,1));
  assert(!strcmp(mzf_loader_bus_debug().phase,"READY_DELAY"));
  assert(sharp_stream_service_ul(701000,1));
  mzf_loader_bus_debug_t debug=mzf_loader_bus_debug();
  assert(!strcmp(debug.phase,"INITIAL_WRITE_LOW") && debug.expected_write==0 && debug.pair==0);
  uint64_t deadline=5701001;
  if(missing_ack){
   assert(sharp_stream_service_ul(701001,0));
   assert(sharp_stream_service_ul(701002,0) && sharp_stream_bus_sense()==0);
   debug=mzf_loader_bus_debug();
   assert(!strcmp(debug.phase,"PAIR_WRITE_HIGH") && debug.expected_write==1 && debug.pair==0);
   deadline=1201003;
  }
  assert(!sharp_stream_service_ul(deadline,missing_ack ? 0:1));
  mzf_loader_bus_debug_t failed=mzf_loader_bus_debug();
  assert(!strcmp(failed.phase,debug.phase) && failed.expected_write==debug.expected_write && failed.pair==debug.pair);
  assert(!strcmp(sharp_stream_error(),"UL TIMEOUT") && sharp_stream_ul_bytes()==0);
  assert(sharp_stream_bus_read()==0 && sharp_stream_bus_sense()==1);
  sharp_stream_close();
 }
 assert(!remove("ul800-start.mzf"));
 puts("PASS UL800 700ms startup, 1ms ready, missing first WRITE LOW versus missing first data ACK HIGH; phase diagnostics and safe failure");
}


static void byte_duration_equivalence(void){
 const unsigned sizes[]={0,1,257,6144,65535};
 const loader_mode_t modes[]={LOADER_MODE_NORMAL_1_1,LOADER_MODE_NORMAL_1_4,LOADER_MODE_IC_1_4,LOADER_MODE_TC_1_3,LOADER_MODE_MZ700_1X,LOADER_MODE_MZ700_3X};
 for(unsigned n=0;n<sizeof(sizes)/sizeof(sizes[0]);++n){
  for(unsigned ext=0;ext<2;++ext){
   const char *path=ext ? "duration.m12":"duration.mzf";FILE *f=fopen(path,"wb");assert(f);
   uint8_t h[128];memcpy(h,original,128);h[18]=(uint8_t)sizes[n];h[19]=(uint8_t)(sizes[n]>>8);
   assert(fwrite(h,1,128,f)==128);
   for(unsigned b=0;b<sizes[n]+17;++b)assert(fputc((uint8_t)(b*37U),f)!=EOF);
   assert(!fclose(f));
  }
  for(unsigned mode=0;mode<sizeof(modes)/sizeof(modes[0]);++mode){
   sharp_stream_info_t fast,wave;
   assert(sharp_stream_open("duration.mzf",1,modes[mode],&fast));sharp_stream_close();
   assert(sharp_stream_open("duration.m12",1,modes[mode],&wave));sharp_stream_close();
   assert(!fast.duration_deferred && fast.total_ticks==wave.total_ticks && fast.effective==wave.effective);
  }
  assert(!remove("duration.mzf") && !remove("duration.m12"));
 }
 puts("PASS byte-summed MZF durations equal waveform FSM for empty/1/257/6144/65535-byte mixed data, checksum overflow, appended data, six timing/loader families and fallback");
}

int main(void) {
 original[0]=1;
 memcpy(original+1,"TEST\r",5);
 original[0x12]=4;
 original[0x15]=original[0x17]=0x12;
 all_modes("test.mzf");
 qd_fixture();
 all_modes("test.mzq");
 ul800_startup_waits();
 byte_duration_equivalence();
 high_loaders();
 metadata();
 bounds();
 cancellation();
 return 0;
}
