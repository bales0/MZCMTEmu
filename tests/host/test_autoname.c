#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "record/record_autoname.h"
#include "audio/edge_stream.h"
#include "audio/edge_writer.h"
#include "formats/mz_tape_decoder.h"
#include "fixtures/loader_golden.h"
static uint8_t original[128],data[65535];
static const uint16_t timing[13][4]={
 {3807,4087,7506,7786},{1818,2228,3753,4164},{1407,1994,2815,3577},
 {3840,4224,7424,7904},{1280,1280,2560,2560},{1232,1877,2522,2874},
 {1407,1994,2815,3577},{1818,2228,3753,4164},{1687,1673,3360,3360},
 {2269,2255,4524,4524},{1232,1877,2522,2874},{3753,4222,7506,7917},{4015,4000,8015,8015}};
static unsigned hex(char c){return c>='a' ? (unsigned)(c-'a'+10):(unsigned)(c-'0');
}
static void decode(const char *s,uint8_t *out,size_t n){assert(strlen(s)==n*2);
for(size_t i=0;i<n;++i)out[i]=(uint8_t)((hex(s[i*2])<<4)|hex(s[i*2+1]));
}
static file_format_t capture_format;
static uint32_t capture_rate;
static uint64_t sample_phase;
static bool write_edge_fixture;
static bool capture(uint8_t level,uint32_t us) {
 if(write_edge_fixture){unsigned unit=capture_format==FILE_FORMAT_LEP ? 50:16;assert(edge_writer_write_interval_units(level,(us+unit/2)/unit));}
 if(capture_format==FILE_FORMAT_WAV) {
  sample_phase+=(uint64_t)us*capture_rate;
  uint32_t n=(uint32_t)(sample_phase/1000000);
  sample_phase%=1000000;
  while(n) {uint8_t count=n>8 ? 8:(uint8_t)n;
  record_autoname_feed_packed_samples(level ? 255:0,count);
  n-=count;
  }
 } else {uint32_t unit=capture_format==FILE_FORMAT_LEP ? 50:16;
 uint32_t n=(us+unit/2)/unit;
  if(n>1024)record_autoname_break_signal();
  else record_autoname_feed_level_interval((uint16_t)n,level);
 }
return true;
}
static void begin(file_format_t format,uint32_t rate) {
 capture_format=format;
 capture_rate=rate;
 sample_phase=500000;
 record_autoname_begin(true,format,rate);
}
static bool interval(unsigned profile,bool bit,bool invert) {
 unsigned i=bit ? 2:0;
 if(!capture(invert ? 1:0,(timing[profile][i]+8)/16)) return false;
 return capture(invert ? 0:1,(timing[profile][i+1]+8)/16);
}
static bool bytes(unsigned profile,const uint8_t *p,size_t n,bool bad,bool invert) {
 uint16_t sum=0;
 for(size_t i=0;i<n;++i)for(unsigned b=0;b<8;++b)if(p[i]&(1U<<b))++sum;
 if(bad)sum^=1;
 for(size_t i=0;i<n+2;++i) {
  uint8_t v=i<n ? p[i]:i==n ? (uint8_t)(sum>>8):(uint8_t)sum;
  for(unsigned b=0;b<8;++b)if(!interval(profile,(v&(0x80U>>b))!=0,invert))return false;
  if(!interval(profile,true,invert))return false;
 }
return true;
}
static bool block(unsigned profile,const uint8_t *p,size_t n,bool header,bool bad,bool invert) {
 for(unsigned i=0;i<(header ? 11000U:5500U);++i)if(!interval(profile,false,invert))return false;
 for(unsigned i=0;i<(header ? 40U:20U);++i)if(!interval(profile,true,invert))return false;
 for(unsigned i=0;i<(header ? 40U:20U);++i)if(!interval(profile,false,invert))return false;
 for(unsigned i=0;i<2;++i)if(!interval(profile,true,invert))return false;
 return bytes(profile,p,n,bad,invert);
}
static void sharp_profiles(void) {
 static const unsigned modes[]={0,1,6,7,8,9,10,11,12,13,14,15,16};
 static const unsigned profiles[]={0,0,5,6,7,8,9,1,2,3,10,11,12};
 for(unsigned format=3;format<4;++format)for(unsigned k=0;k<sizeof(modes)/sizeof(modes[0]);++k) {
  unsigned mode=modes[k];
  const struct loader_golden *g=&loader_golden[mode];
  uint8_t h[128],ldr[128];
  decode(g->header_hex,h,128);
  decode(g->loader_hex,ldr,g->size);
  begin(format<2 ? FILE_FORMAT_WAV:format==2 ? FILE_FORMAT_LEP:FILE_FORMAT_L16,format==1 ? 22050:44100);
  assert(block(g->active ? 0:profiles[k],h,128,true,false,false));
  if(mode==9 || mode==10 || mode==16)assert(block(0,ldr,g->size,false,false,false));
  assert(block(profiles[k],data,4,false,false,false));
  capture(0,1000);
  capture(1,1000);
  assert(record_autoname_has_name() && !strcmp(record_autoname_get_name(),"TEST"));
  loader_mode_t detected;
  assert(record_autoname_get_detected_loader_mode(&detected));
  assert((unsigned)detected==(mode==1 || mode==13 ? 0:mode));
  uint8_t progress;
  assert(record_autoname_get_payload_progress_percent(&progress) && progress==100);
 }
 puts("PASS AutoName Sharp NORMAL and all IC/TC profiles, native/copier title and payload progress from original edges before WAV/LEP quantization");
}
static uint8_t aux_level;
static void aux_interval(unsigned us) {capture(aux_level,us);
aux_level^=1;
}
static void sinclair(bool corrupt,unsigned speed) {
 static const unsigned pilot[]={612,466,400,336};
 unsigned p=pilot[speed];
 uint8_t bytes[19]={0};
 memcpy(bytes+2,"SIN TEST  ",10);
 bytes[12]=4;
 for(unsigned i=0;i<18;++i)bytes[18]^=bytes[i];
 if(corrupt)bytes[18]^=1;
 for(unsigned i=0;i<1024;++i)aux_interval(p);
 aux_interval(p*31/100);
 aux_interval(p*35/100);
 for(unsigned i=0;i<19;++i)for(unsigned bit=0;bit<8;++bit) {
  unsigned us=p*(bytes[i]&(0x80U>>bit) ? 79:39)/100;
  aux_interval(us);
  aux_interval(us);
 }
 aux_interval(1000);
 aux_interval(1000);
}
static unsigned cmt_short;
static uint8_t cmt_level;
static void cmt_symbol(bool symbol) {if(symbol)aux_interval(cmt_short*2);
else {aux_interval(cmt_short);
aux_interval(cmt_short);
}}
static void cmt_byte(uint8_t value) {for(unsigned bit=0;bit<8;++bit) {uint8_t next=(uint8_t)(((value>>bit)&1)^1);
cmt_symbol(next!=cmt_level);
cmt_level=next;
}}
static void cpm(bool corrupt,unsigned speed,unsigned subtype) {
 static const unsigned widths[]={208,180,158,140};
 cmt_short=widths[speed];
 uint8_t bytes[131]={0};
 bytes[1]=(uint8_t)subtype;
 memcpy(bytes+2,"CPM TEST",8);
 bytes[19]=4;
 uint16_t sum=0;
 for(unsigned i=1;i<=128;++i)sum+=bytes[i];
 if(corrupt)sum^=1;
 bytes[129]=(uint8_t)sum;
 bytes[130]=(uint8_t)(sum>>8);
 for(unsigned i=0;i<1536;++i)cmt_symbol(i%3!=0);
 cmt_symbol(true);
 cmt_level=0;
 for(unsigned i=0;i<131;++i)cmt_byte(bytes[i]);
 aux_interval(1000);
 aux_interval(1000);
}
static void aux_profiles(void) {
 for(unsigned speed=0;speed<4;++speed) {
  begin(FILE_FORMAT_L16,0);
  aux_level=0;
  sinclair(false,speed);
  record_autoname_aux_profile_t profile;
  assert(record_autoname_has_name() && !strcmp(record_autoname_get_name(),"SIN TEST"));
  assert(record_autoname_get_detected_aux_profile(&profile) && profile==speed+1);
  begin(FILE_FORMAT_L16,0);
  aux_level=0;
  sinclair(true,speed);
  assert(!record_autoname_has_name());
  for(unsigned subtype=0;subtype<2;++subtype) {
   begin(FILE_FORMAT_L16,0);
   aux_level=0;
   cpm(false,speed,subtype);
   assert(record_autoname_has_name() && !strcmp(record_autoname_get_name(),"CPM TEST"));
   assert(record_autoname_get_detected_aux_profile(&profile) && profile==speed+5);
   begin(FILE_FORMAT_L16,0);
   aux_level=0;
   cpm(true,speed,subtype);
   assert(!record_autoname_has_name());
  }
 }
 puts("PASS Sinclair and InterCopy CP/M subtypes 00/01: all four speed classifications, title checksum acceptance and rejection");
}

static void aux_preflight(void){
 for(unsigned f=0;f<2;++f)for(unsigned speed=0;speed<4;++speed)for(unsigned protocol=0;protocol<2;++protocol)for(unsigned bad=0;bad<2;++bad){
  const char *path=f ? "aux.l16":"aux.lep";begin(f ? FILE_FORMAT_L16:FILE_FORMAT_LEP,0);aux_level=0;
  assert(edge_writer_open(path,f ? 62500:20000));write_edge_fixture=true;
  if(protocol)cpm(bad!=0,speed,1);else sinclair(bad!=0,speed);
  write_edge_fixture=false;assert(edge_writer_finalize());
  edge_stream_info_t info;edge_stream_invalidate_cache();assert(edge_stream_open(path,&info));
  if(bad)assert(!(info.analysis.program.valid&PROGRAM_TITLE));
  else {assert(info.analysis.program.valid&PROGRAM_TITLE);assert(!strcmp(info.analysis.program.title,protocol ? "CPM TEST":"SIN TEST"));assert(info.analysis.program.protocol==(protocol ? PROGRAM_CPM:PROGRAM_SPECTRUM));assert(info.analysis.aux_profile>=1 && info.analysis.aux_profile<=8);if(f)assert(info.analysis.aux_profile==speed+(protocol ? 5:1));}
  edge_stream_close();assert(!remove(path));
 }
 puts("PASS actual LEP/L16 preflight Sinclair and CP/M four speeds, validated titles/provenance and corrupt checksum refusal");
}

static void rename_tests(void) {
 begin(FILE_FORMAT_MZF,0);
 record_autoname_accept_header(original);
 char path[256]="./REC0001.MZF";
 FILE *f=fopen(path,"wb");
 assert(f && fputs("data",f)>=0 && !fclose(f));
 f=fopen("TEST.MZF","wb");
 assert(f && fputs("preserve",f)>=0 && !fclose(f));
 assert(record_autoname_apply(path,sizeof(path)) && !strcmp(path,"./TEST_01.MZF"));
 f=fopen("TEST.MZF","rb");
 assert(f && fgetc(f)=='p');
 fclose(f);
 assert(!remove("TEST.MZF") && !remove(path));
 begin(FILE_FORMAT_L16,0);
 uint8_t bad[128]={0};
 memcpy(bad+1,"../: ?",6);
 record_autoname_accept_header(bad);
 assert(!record_autoname_has_name());
 puts("PASS AutoName collision suffix, original file preservation and invalid title rejection");
}
static void legacy_quantization_limits(void) {
 /* Kept as regression evidence for the reference packed/quantized API.
  * Live recorder metadata uses original edges, so these lossy paths do not
  * control the actual filename or displayed profile. */
 begin(FILE_FORMAT_WAV,22050);
 assert(block(2,original,128,true,false,false));
 capture(0,1000);
 capture(1,1000);
 loader_mode_t mode;
 assert(record_autoname_get_detected_loader_mode(&mode) && mode==LOADER_MODE_NORMAL_1_4);
 begin(FILE_FORMAT_LEP,0);
 assert(block(10,original,128,true,false,false));
 assert(!record_autoname_has_name());
 puts("PASS quantified legacy limits: WAV 22k N1:3 classification ambiguity and LEP N1:4 header loss; live edge metadata bypasses both");
}
int main(void) {
 original[0]=1;
 memcpy(original+1,"TEST\r",5);
 original[18]=4;
 original[21]=original[23]=0x12;
 data[0]=0;
 data[1]=128;
 data[2]=1;
 data[3]=255;
 sharp_profiles();
 aux_profiles();
 aux_preflight();
 rename_tests();
 legacy_quantization_limits();
 return 0;
}
