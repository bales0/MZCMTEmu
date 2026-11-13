"""Compare fixture validation/payload with unmodified reference QD kernels.
GPL-3.0-only. Reference source is only read; host files stay under .tmp.
The harness replaces SD access and UI cancellation, not decoder/CRC algorithms.
Physical/QDF orchestration checks count/frames as the reference cold scan does;
AVR duration caching and playback are deliberately outside this parser test.
"""
import argparse, os, re, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--reference',type=Path,default=ROOT.parent/'MZ-SD2CMT2-Reborn')
p.add_argument('--cc',default='gcc')
p.add_argument('--cxx',default='g++')
p.add_argument('fixtures',type=Path,nargs='+')
a=p.parse_args();ref=a.reference.resolve()
head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ref,text=True).strip()
assert head=='600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb',head
subprocess.run(['git','diff','--quiet','HEAD','--','src'],cwd=ref,check=True)
source=(ref/'src/play/mzf_playback.cpp').read_text(encoding='utf8')
def function(name):
 m=re.search(r'^static [^;{}\n]*\b'+re.escape(name)+r'\([^;{]*\)\s*\{',source,re.M)
 if not m:raise ValueError(name)
 start=m.start();pos=m.end();depth=1
 while depth:
  if source[pos]=='{':depth+=1
  elif source[pos]=='}':depth-=1
  pos+=1
 return source[start:pos]+'\n'
names=['mzf_read_le32','mzf_popcount8','mzf_mzq_crc_byte','mzf_mzq_raw_byte',
 'mzf_mzq_decode_byte','mzf_mzq_build_tape_header','mzf_mzq_configure_physical',
 'mzf_mzq_scan_read','mzf_mzq_scan_find_sync','mzf_mzq_find_physical_count',
 'mzf_mzq_read_physical_frame','mzf_qdf_scan_read','mzf_qdf_find_sync','mzf_qdf_read_frame',
 'mzf_mzq_read_logical','mzf_mzq_find_frame','mzf_locate_logical_mzq_record']
PRE=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <vector>
extern "C" {
#include "formats/quickdisk.h"
}
#define PSTR(x) x
typedef const char *PGM_P;
#define MZQ_MAX_FILES 50U
#define MZQ_HEADER_BYTES 64U
#define MZF_HEADER_BYTES 128U
#define MZF_HEADER_DATA_LENGTH_OFFSET 18U
#define MZQ_HEADER_SHIFT_OFFSET 22U
#define MZQ_RAW_BUFFER_BYTES 512U
#define WAV_SAMPLE_STREAM_BUFFER_BYTES 512U
#define MZF_PLAYBACK_IO_ERROR 1
#define MZF_PLAYBACK_BAD_FILE 2
static FILE *source_file;
static uint32_t mzf_file_size,mzf_mzq_track_offset,mzf_mzq_track_length,mzf_mzq_raw_buffer_offset;
static uint32_t mzf_mzq_scan_position,mzf_qdf_scan_position;
static uint16_t mzf_mzq_raw_buffer_length;
static uint8_t mzf_mzq_data_phase,mzf_mzq_preferred_phase,mzf_qd_last_progress;
static uint8_t mzf_mzq_raw_buffer[512],mzf_header[128];
static volatile uint8_t wav_sample_stream_isr_bytes[512];
static bool mzf_qd_analysis_active,mzf_mzq_physical_source,mzf_tape_turbo_payload_prepared;
static bool mzf_qd_analysis_cancelled;
static bool (*mzf_qd_cancel_callback)(void);
static uint16_t mzf_mzt_record_count,mzf_mzt_record_index,mzf_header_offset;
static uint32_t mzf_record_data_length,mzf_record_data_file_start,mzf_record_data_file_end,mzf_record_data_read;
static bool sdcard_file_seek(uint32_t pos){return fseek(source_file,(long)pos,SEEK_SET)==0;}
static int16_t sdcard_file_read(void *out,uint16_t n){return (int16_t)fread(out,1,n,source_file);}
static uint32_t sdcard_file_position(void){return (uint32_t)ftell(source_file);}
static bool mzf_qd_analysis_poll(uint32_t,uint32_t){return false;}
static void mzf_set_error_P(const char *,int){}
static void mzf_fifo_reset(void){}
'''
POST=r'''
struct Record {uint8_t header[128];std::vector<uint8_t> bytes;};
static bool reference_parse(std::vector<Record> &out,const char **reason) {
 uint8_t prefix[40]={0};sdcard_file_seek(0);size_t n=fread(prefix,1,40,source_file);
 mzf_mzq_raw_buffer_length=0;mzf_mzq_scan_position=mzf_qdf_scan_position=0;
 static const uint8_t qdf[16]={'-','Q','D',' ','f','o','r','m','a','t','-',255,255,255,255,255};
 bool physical=!memcmp(prefix,"HXCQDDRV",8) || (prefix[3]=='Q' && prefix[4]=='D');
 bool is_qdf=n>=16 && !memcmp(prefix,qdf,16);
 if(!physical && !is_qdf) {
  *reason="logical header/frames";
  if(!mzf_locate_logical_mzq_record(1))return false;
  unsigned count=mzf_mzt_record_count;
  for(unsigned i=1;i<=count;++i) {
   if(!mzf_locate_logical_mzq_record((uint16_t)i))return false;
   Record r;memcpy(r.header,mzf_header,128);r.bytes.resize(mzf_record_data_length);
   if(fread(r.bytes.data(),1,r.bytes.size(),source_file)!=r.bytes.size())return false;
   out.push_back(r);
  }
  return true;
 }
 uint8_t count=0;
 if(physical) {
  *reason="physical descriptor";if(!mzf_mzq_configure_physical(prefix,!memcmp(prefix,"HXCQDDRV",8)))return false;
  *reason="physical count/CRC";if(!mzf_mzq_find_physical_count(&count))return false;
 } else {
  mzf_qdf_scan_position=16;uint8_t lo,hi;
  *reason="QDF count/CRC";
  if(!mzf_qdf_find_sync() || !mzf_qdf_scan_read(&count) || !mzf_qdf_scan_read(&lo) || !mzf_qdf_scan_read(&hi))return false;
  uint16_t crc=mzf_mzq_crc_byte(mzf_mzq_crc_byte(mzf_mzq_crc_byte(mzf_mzq_crc_byte(0,0xA5),count),lo),hi);
  if(crc)return false;
 }
 *reason=count==0 ? "empty count" : "count bounds";
 if(!count || (count&1) || count>100)return false;
 for(unsigned i=0;i<count/2;++i) {
  uint8_t h[64];uint16_t hn,len;uint32_t hp,bp;
  *reason="frame/CRC/length";
  if(physical) {
   if(!mzf_mzq_read_physical_frame(0,h,&hn,&hp,NULL,NULL) || !mzf_mzq_read_physical_frame(5,NULL,&len,&bp,NULL,NULL))return false;
  } else if(!mzf_qdf_read_frame(0,h,&hn,&hp) || !mzf_qdf_read_frame(5,NULL,&len,&bp))return false;
  if(((uint16_t)h[20]|((uint16_t)h[21]<<8))!=len)return false;
  mzf_mzq_build_tape_header(h,len);Record r;memcpy(r.header,mzf_header,128);r.bytes.resize(len);
  if(physical) {for(unsigned j=0;j<len;++j)if(!mzf_mzq_decode_byte(bp+j,&r.bytes[j]))return false;}
  else {if(!sdcard_file_seek(bp) || fread(r.bytes.data(),1,len,source_file)!=len)return false;}
  out.push_back(r);
 }
 return true;
}
int main(int argc,char **argv) {
 for(int arg=1;arg<argc;++arg) {
  source_file=fopen(argv[arg],"rb");assert(source_file);assert(!fseek(source_file,0,SEEK_END));mzf_file_size=(uint32_t)ftell(source_file);
  std::vector<Record> records;const char *reason="OK";bool original=reference_parse(records,&reason);
  FILE *port=fopen(argv[arg],"rb");assert(port);bool accepted=quickdisk_open(port,mzf_file_size,NULL,NULL);
  if(accepted!=original) {fprintf(stderr,"MISMATCH accept: %s ref=%d port=%d (%s)\n",argv[arg],original,accepted,reason);return 1;}
  if(accepted) {
   assert(quickdisk_count()==records.size());
   for(unsigned i=0;i<records.size();++i) {
    uint8_t header[128];uint32_t length;assert(quickdisk_select((uint16_t)(i+1),header,&length));
    assert(!memcmp(header,records[i].header,128) && length==records[i].bytes.size());
    std::vector<uint8_t> bytes(length);assert(quickdisk_read(bytes.data(),length)==length && bytes==records[i].bytes);
   }
  }
  printf("MATCH %s: %s, %u records%s%s\n",argv[arg],accepted ? "ACCEPT":"REJECT",accepted ? (unsigned)records.size():0,accepted ? "":", reference reason=",accepted ? "":reason);
  quickdisk_close();fclose(port);fclose(source_file);
 }
 return 0;
}
'''
files=[]
for entry in a.fixtures:
 files.extend(sorted(f.resolve() for f in entry.iterdir() if f.suffix.lower() in ('.qd','.qdf','.mzq'))) if entry.is_dir() else files.append(entry.resolve())
env=dict(os.environ);env['PATH']=str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
(ROOT/'.tmp').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='qd-reference-',dir=ROOT/'.tmp') as directory:
 tmp=Path(directory);cpp=tmp/'reference.cpp';cpp.write_text(PRE+'\n'.join(function(n) for n in names)+POST,encoding='utf8')
 obj=tmp/'quickdisk.o';exe=tmp/('compare.exe' if os.name=='nt' else 'compare')
 subprocess.run([a.cc,'-std=c11','-O2','-I',str(ROOT/'src'),'-c',str(ROOT/'src/formats/quickdisk.c'),'-o',str(obj)],env=env,check=True)
 subprocess.run([a.cxx,'-std=c++11','-O2','-I',str(ROOT/'src'),str(cpp),str(obj),'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe),*map(str,files)],env=env,check=True)
print('Reference QD parser kernels match; playback/duration/UI/RTOS are outside this comparison.')
