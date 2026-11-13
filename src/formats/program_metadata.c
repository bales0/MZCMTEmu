/* SPDX-License-Identifier: GPL-3.0-only */
#include "formats/program_metadata.h"
#include "formats/mz_loader_profiles.h"
#include "formats/mz_title.h"
#include <string.h>
static uint16_t le16(const uint8_t *p){return (uint16_t)p[0]|(uint16_t)((uint16_t)p[1]<<8);
}
void program_metadata_reset(program_metadata_t *m,file_format_t f){memset(m,0,sizeof(*m));
m->format=f;
m->confidence=PROGRAM_WAITING;
}
void program_metadata_publish(program_metadata_store_t *s,const program_metadata_t *m){
 uint32_t seq=__atomic_load_n(&s->seq,__ATOMIC_RELAXED);
 __atomic_store_n(&s->seq,seq+1,__ATOMIC_SEQ_CST);
 for(size_t i=0;i<sizeof(s->words)/4;++i){uint32_t word=0;
 size_t off=i*4,n=sizeof(*m)-off;
 if(n>4)n=4;
 memcpy(&word,(const uint8_t *)m+off,n);
 __atomic_store_n(&s->words[i],word,__ATOMIC_RELAXED);
 }
 __atomic_store_n(&s->seq,seq+2,__ATOMIC_RELEASE);
}
void program_metadata_snapshot(const program_metadata_store_t *s,program_metadata_t *m){
 for(;;){uint32_t seq=__atomic_load_n(&s->seq,__ATOMIC_ACQUIRE);
 if(seq&1)continue;
 for(size_t i=0;i<sizeof(s->words)/4;++i){uint32_t word=__atomic_load_n(&s->words[i],__ATOMIC_RELAXED);
 size_t off=i*4,n=sizeof(*m)-off;
 if(n>4)n=4;
 memcpy((uint8_t *)m+off,&word,n);
 }
 if(seq==__atomic_load_n(&s->seq,__ATOMIC_ACQUIRE))return;
 }
}
loader_mode_t program_metadata_copier_mode(unsigned p){
 switch(p){case MZ_COPIER_IC_1_1:return LOADER_MODE_IC_1_1;
 case MZ_COPIER_IC_1_2:return LOADER_MODE_IC_1_2;
 case MZ_COPIER_IC_1_3:return LOADER_MODE_IC_1_3;
 case MZ_COPIER_IC_1_4:return LOADER_MODE_IC_1_4;
 case MZ_COPIER_TC_1_1:return LOADER_MODE_TC_1_1;
 case MZ_COPIER_TC_1_2:return LOADER_MODE_TC_1_2;
 case MZ_COPIER_TC_1_3:return LOADER_MODE_TC_1_3;
 default:return LOADER_MODE_NORMAL_1_1;
 }
}
void program_metadata_sharp(program_metadata_t *m,const uint8_t h[128],program_source_t source){
 if(!h)return;
 m->protocol=PROGRAM_SHARP;
 m->source=source;
 m->confidence=PROGRAM_HEADER_VALID;
 m->chain_stage=0;
 m->valid&=PROGRAM_TRANSFER_MODE|PROGRAM_RECORDED_MODE;
 m->valid|=PROGRAM_TYPE|PROGRAM_SIZE|PROGRAM_LOAD|PROGRAM_EXEC;
 m->file_type=h[0];
 m->declared_size=le16(h+18);
 m->load=le16(h+20);
 m->exec=le16(h+22);
 m->received_bytes=0;
 if(mz_title_decode_display(h+1,17,m->title,sizeof(m->title)))m->valid|=PROGRAM_TITLE;
}
bool program_metadata_source_sharp(program_metadata_t *m,const uint8_t h[128],const uint8_t *data,size_t n){
 uint8_t canonical[128];
 mz_copier_profile_t profile;
 if(mz_loader_profile_recognize_ic(h,canonical,&profile)){
  program_metadata_sharp(m,canonical,PROGRAM_LOADER_EMBEDDED);
  m->recorded_loader_mode=program_metadata_copier_mode(profile);
  m->valid|=PROGRAM_RECORDED_MODE;
  return true;
 }
 if(mz_loader_profile_recognize_tc_header(h)){
  if(n>=MZ_TC_LOADER_BYTES && mz_loader_profile_normalize_tc(h,data,canonical,&profile)){
   program_metadata_sharp(m,canonical,PROGRAM_LOADER_EMBEDDED);
   m->recorded_loader_mode=program_metadata_copier_mode(profile);
   m->valid|=PROGRAM_RECORDED_MODE;
   return true;
  }
  m->protocol=PROGRAM_SHARP;
  m->source=PROGRAM_LOADER_EMBEDDED;
  m->confidence=PROGRAM_LOADER_ONLY;
  m->chain_stage=1;
  m->valid&=PROGRAM_TRANSFER_MODE;
  m->title[0]=0;
  return false;
 }
 program_metadata_sharp(m,h,PROGRAM_MZF_HEADER);
 return true;
}
bool program_metadata_zx(program_metadata_t *m,const uint8_t h[19]){
 uint8_t sum=0;
 for(unsigned i=0;i<19;++i)sum^=h[i];
 if(h[0] || h[1]>3 || sum)return false;
 file_format_t f=m->format;
 program_metadata_reset(m,f);
 m->protocol=PROGRAM_SPECTRUM;
 m->source=PROGRAM_TAP_HEADER;
 m->confidence=PROGRAM_HEADER_VALID;
 m->valid=PROGRAM_TYPE|PROGRAM_SIZE;
 m->file_type=h[1];
 m->declared_size=le16(h+12);
 for(unsigned i=0;i<10;++i)m->title[i]=h[i+2]>=32 && h[i+2]<127 ? (char)h[i+2]:' ';
 m->title[10]=0;
 for(int i=9;i>=0 && m->title[i]==' ';--i)m->title[i]=0;
 if(m->title[0])m->valid|=PROGRAM_TITLE;
 if(h[1]==3){m->load=le16(h+14);
 m->valid|=PROGRAM_LOAD;
 }
 else if(!h[1] && le16(h+14)<=9999){m->autostart=le16(h+14);
 m->valid|=PROGRAM_AUTOSTART;
 }
 return true;
}
const char *program_metadata_type(const program_metadata_t *m){
 if(!(m->valid&PROGRAM_TYPE))return "--";
 if(m->protocol==PROGRAM_SPECTRUM){static const char *const types[]={"ZX BASIC","ZX NUM ARRAY","ZX CHAR ARRAY","ZX CODE"};
 return m->file_type<4 ? types[m->file_type]:"ZX ?";
 }
 if(m->protocol==PROGRAM_SHARP){if(m->file_type==1)return "OBJ";
 
 return "TYPE";
 }
 return "--";
}
