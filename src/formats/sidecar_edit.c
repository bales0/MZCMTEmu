/* SPDX-License-Identifier: GPL-3.0-only */
#include "formats/sidecar_edit.h"
#include "formats/sidecar.h"
#include "formats/file_format.h"
#include "storage/fs.h"
#include "storage/record_file.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <errno.h>
/* Single storage worker owns scratch, including recovery; never an ISR path. */
static sc_document_t check;
static char pending[1024],backup[1024],journal[1024],canonical[1024];
static const uint8_t modes[][4]={
 {0,11,12,14},{15,8,7,6},{16,10,9,SC_NONE},{13,SC_NONE,5,SC_NONE},
 {2,SC_NONE,SC_NONE,SC_NONE},{3,SC_NONE,SC_NONE,SC_NONE},{4,SC_NONE,SC_NONE,SC_NONE}};
static const char *const families[]={"NORMAL","IC","TC","MZ700","UL","UL_MZ800","UL_MZ700"};
static const char *const speeds[]={"1:1","1:2","1:3","1:4"};
uint8_t sc_make_mode(unsigned f,unsigned s){return f<7 && s<4 ? modes[f][s]:SC_NONE;}
bool sc_mode_fields(uint8_t mode,const char **family,const char **speed){
 for(unsigned f=0;f<7;++f)for(unsigned s=0;s<4;++s)if(modes[f][s]!=SC_NONE && modes[f][s]==mode){
  if(family)*family=families[f];
  if(speed)*speed=f>=4 ? "N/A":speeds[s];
  return true;
 }
 return false;
}
static bool error(sc_document_t *d,const char *text){d->error=text;return false;}
static uint32_t hash_byte(uint32_t hash,unsigned char c){return (hash^c)*16777619U;}
static bool file_hash(const char *path,uint32_t *hash,uint32_t *size){
 cmt_file_t *f=cmt_fopen(path,"rb");if(!f)return false;
 uint8_t bytes[128];uint32_t h=2166136261U,n=0;size_t got;
 while((got=cmt_fread(bytes,1,sizeof(bytes),f))!=0){n+=(uint32_t)got;for(size_t i=0;i<got;++i)h=hash_byte(h,bytes[i]);}
 bool ok=!cmt_ferror(f);if(cmt_fclose(f))ok=false;
 if(ok){*hash=h;*size=n;}return ok;
}
static sc_entry_t *find(sc_document_t *d,uint16_t r){for(unsigned i=0;i<d->count;++i)if(d->entries[i].record==r)return &d->entries[i];return NULL;}
uint8_t sc_profile(const sc_document_t *d,uint16_t r){for(unsigned i=0;i<d->count;++i)if(d->entries[i].record==r)return d->entries[i].current;return SC_NONE;}
unsigned sc_changes(const sc_document_t *d){unsigned n=0;for(unsigned i=0;i<d->count;++i)n+=d->entries[i].original!=d->entries[i].current;return n;}
bool sc_accept(sc_document_t *d,uint16_t r,uint8_t mode){
 if(!r || r>d->records || !sc_mode_fields(mode,NULL,NULL) || d->error || d->recovery)return false;
 sc_entry_t *e=find(d,r);
 if(!e){if(d->count==SC_MAX_PROFILES)return error(d,"TOO MANY PROFILES");e=&d->entries[d->count++];*e=(sc_entry_t){r,SC_NONE,SC_NONE};}
 e->current=mode;return true;
}
static bool finish_section(sc_document_t *d,unsigned rec,const char *type,const char *speed){
 if(!rec || rec>d->records || !type[0] || find(d,(uint16_t)rec))return error(d,"INVALID / DUP RECORD");
 uint8_t mode=SC_NONE;
 for(unsigned f=0;f<7;++f)if(!strcasecmp(type,families[f])){
  if(f>=4){if(!speed[0] || !strcmp(speed,"1:1"))mode=modes[f][0];}
  else for(unsigned s=0;s<4;++s)if(!strcmp(speed,speeds[s]))mode=modes[f][s];
 }
 if(mode==SC_NONE)return error(d,"INVALID PROFILE");
 if(d->count==SC_MAX_PROFILES)return error(d,"TOO MANY PROFILES");
 d->entries[d->count++]=(sc_entry_t){(uint16_t)rec,mode,mode};return true;
}
static bool parse(sc_document_t *d,const char *path,bool multi){
 cmt_file_t *f=cmt_fopen(path,"rb");if(!f)return error(d,"SIDECAR OPEN ERROR");
 char line[64],type[12]={0},speed[5]={0};unsigned length=0,rec=multi ? 0:1;
 uint32_t hash=2166136261U,size=0;bool ok=true,seen=false;
 for(;;){int c=cmt_fgetc(f);
  if(c!=EOF){hash=hash_byte(hash,(unsigned char)c);if(++size>SC_MAX_BYTES){ok=error(d,"SIDECAR TOO LONG");break;}}
  if(c==EOF || c=='\n' || c=='\r'){
   line[length]=0;
   if(length){
    if(!strncmp(line,"RECORD=",7) && multi){
     if(rec && !finish_section(d,rec,type,speed)){ok=false;break;}
     char *end;unsigned long n=strtoul(line+7,&end,10);
     if(!line[7] || line[7]<'0' || line[7]>'9' || *end || !n || n>65535){ok=error(d,"INVALID RECORD");break;}
     rec=(unsigned)n;type[0]=speed[0]=0;
    }else if(!strncmp(line,"TYPE=",5) && rec && !type[0] && line[5] && strlen(line+5)<sizeof(type))strcpy(type,line+5);
    else if(!strncmp(line,"SPEED=",6) && rec && !speed[0] && line[6] && strlen(line+6)<sizeof(speed))strcpy(speed,line+6);
    else {ok=error(d,"UNSUPPORTED SIDECAR");break;}
    seen=true;
   }
   length=0;if(c==EOF)break;
  }else if(c<32 || c>126 || length==sizeof(line)-1){ok=error(d,"INVALID SIDECAR LINE");break;}
  else line[length++]=(char)c;
 }
 if(cmt_ferror(f))ok=error(d,"SIDECAR READ ERROR");
 if(cmt_fclose(f))ok=error(d,"SIDECAR CLOSE ERROR");
 if(ok && (!seen || !finish_section(d,rec,type,speed)))ok=error(d,"INVALID PROFILE");
 if(ok){d->original_hash=hash;d->original_size=size;}return ok;
}
static bool auxiliary(const char *target){
 size_t n=strlen(target);const char *leaf=strrchr(target,'/');leaf=leaf ? leaf+1:target;
 if(n+4>=sizeof(pending) || strlen(leaf)+4>255)return false;
 snprintf(pending,sizeof(pending),"%s.SCN",target);snprintf(backup,sizeof(backup),"%s.SCB",target);snprintf(journal,sizeof(journal),"%s.SCJ",target);return true;
}
static bool exists(const char *p){struct stat st;return !cmt_stat(p,&st);}
/* Detect transaction names case-insensitively, including an absent target. */
static bool transaction_name(sc_document_t *d){
 char directory[1024];const char *leaf=strrchr(canonical,'/');size_t prefix=leaf ? (size_t)(leaf-canonical)+1:0;
 if(prefix){memcpy(directory,canonical,prefix);directory[prefix]=0;}else strcpy(directory,".");
 DIR *dir=cmt_opendir(directory);if(!dir)return error(d,"DIRECTORY ERROR");
 struct dirent *entry;unsigned found=0;bool orphan=false;
 while((entry=cmt_readdir(dir))!=NULL){size_t n=strlen(entry->d_name),base=strlen(canonical+prefix);
  if(n==base+4 && !strncasecmp(entry->d_name,canonical+prefix,base)){
   if(!strcasecmp(entry->d_name+base,".SCJ")){
    if(prefix+base>=sizeof(d->target)){cmt_closedir(dir);return error(d,"PATH TOO LONG");}
    memcpy(d->target,canonical,prefix);memcpy(d->target+prefix,entry->d_name,base);d->target[prefix+base]=0;++found;
   }else if(!strcasecmp(entry->d_name+base,".SCN") || !strcasecmp(entry->d_name+base,".SCB"))orphan=true;
  }
 }
 if(cmt_closedir(dir) || found>1)return error(d,"AMBIGUOUS RECOVERY");
 if(found){d->recovery=true;return error(d,"RECOVERY REQUIRED");}
 if(orphan)return error(d,"ORPHAN TX FILES");
 return true;
}
bool sc_load(sc_document_t *d,const char *source,uint16_t records){
 memset(d,0,sizeof(*d));
 if(!records || !source || strlen(source)>=sizeof(d->source) || !sidecar_build_path(source,canonical,sizeof(canonical)))return error(d,"UNSUPPORTED / PATH");
 strcpy(d->source,source);d->records=records;
 struct stat st;if(cmt_stat(source,&st) || !S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size>UINT32_MAX)return error(d,"SOURCE NOT AVAILABLE");
 d->source_size=(uint32_t)st.st_size;d->source_mtime=st.st_mtime;
 if(!sidecar_find(source,d->target,sizeof(d->target),&d->existed))return error(d,"AMBIGUOUS SIDECAR");
 if(!auxiliary(d->target))return error(d,"TX PATH TOO LONG");
 if(!transaction_name(d))return false;
 if(!d->existed)return true;
 return parse(d,d->target,file_format_is_record_container(file_format_detect_from_name(source)));
}
static bool equivalent(const sc_document_t *a,const sc_document_t *b){
 unsigned n=0;for(unsigned i=0;i<a->count;++i)if(a->entries[i].current!=SC_NONE){++n;if(sc_profile(b,a->entries[i].record)!=a->entries[i].current)return false;}
 return n==b->count;
}
static bool read_check(sc_document_t *d,const char *path){
 memset(&check,0,sizeof(check));check.records=d->records;
 return parse(&check,path,file_format_is_record_container(file_format_detect_from_name(d->source))) && equivalent(d,&check);
}
static bool serialize(const sc_document_t *d,cmt_file_t *f,uint32_t *hash,uint32_t *size){
 uint32_t h=2166136261U,n=0;char text[96];
 bool multi=file_format_is_record_container(file_format_detect_from_name(d->source));
 /* Table is small; deterministic ascending record order without allocation. */
 for(unsigned r=1;r<=d->records;++r){uint8_t mode=sc_profile(d,(uint16_t)r);if(mode==SC_NONE)continue;
  const char *family,*speed;if(!sc_mode_fields(mode,&family,&speed))return false;
  int used=multi ? snprintf(text,sizeof(text),"RECORD=%u\nTYPE=%s\n",r,family):snprintf(text,sizeof(text),"TYPE=%s\n",family);
  if(strcmp(speed,"N/A"))used+=snprintf(text+used,sizeof(text)-(size_t)used,"SPEED=%s\n",speed);
  if(used<=0 || (size_t)used>=sizeof(text))return false;
  if(f && cmt_fwrite(text,1,(size_t)used,f)!=(size_t)used)return false;
  for(int i=0;i<used;++i)h=hash_byte(h,(unsigned char)text[i]);
  n+=(uint32_t)used;
 }
 *hash=h;*size=n;return n!=0 && n<=SC_MAX_BYTES;
}
static bool write_file(const char *path,const char *text){
 cmt_file_t *f=record_file_create(path,1024);if(!f)return false;
 size_t n=strlen(text);bool ok=cmt_fwrite(text,1,n,f)==n;
 if(cmt_fflush(f))ok=false;
 if(cmt_fclose(f))ok=false;
 return ok;
}
static bool marker_read(char text[100]){
 cmt_file_t *f=cmt_fopen(journal,"rb");if(!f)return false;
 size_t n=cmt_fread(text,1,99,f);int more=cmt_fgetc(f);bool ok=n && n<99 && more==EOF && !cmt_ferror(f);
 if(cmt_fclose(f))ok=false;
 text[n]=0;return ok;
}
/* Recover only files identified by a flushed, strictly verified journal. */
bool sc_recover(sc_document_t *d){
 struct stat source;
 if(cmt_stat(d->source,&source) || !S_ISREG(source.st_mode) || source.st_size<0 || (uint64_t)source.st_size!=d->source_size || source.st_mtime!=d->source_mtime)return error(d,"SOURCE CHANGED");
 if(!auxiliary(d->target))return error(d,"TX PATH TOO LONG");
 char marker[100],tail;unsigned old;unsigned long oh,on,nh,nn;
 if(!marker_read(marker) || sscanf(marker,"MZSC1 %u %lu %lu %lu %lu %c",&old,&oh,&on,&nh,&nn,&tail)!=5 || old>1 || on>SC_MAX_BYTES || !nn || nn>SC_MAX_BYTES)return error(d,"INVALID TX JOURNAL");
 uint32_t h,n;bool target=exists(d->target),bak=exists(backup);
 if(target){
  if(!file_hash(d->target,&h,&n))return error(d,"RECOVERY READ ERROR");
  if(!((h==(uint32_t)nh && n==(uint32_t)nn) || (old && h==(uint32_t)oh && n==(uint32_t)on)))return error(d,"RECOVERY CONFLICT");
 }else if(old){
  if(!bak || !file_hash(backup,&h,&n) || h!=(uint32_t)oh || n!=(uint32_t)on)return error(d,"BACKUP INVALID");
  if(cmt_rename(backup,d->target))return error(d,"RESTORE FAILED");
  bak=false;
 }
 if(bak){if(!file_hash(backup,&h,&n) || h!=(uint32_t)oh || n!=(uint32_t)on)return error(d,"BACKUP CONFLICT");if(cmt_remove(backup))return error(d,"BACKUP CLEANUP ERROR");}
 if(exists(pending) && cmt_remove(pending))return error(d,"TEMP CLEANUP ERROR");
 if(cmt_remove(journal))return error(d,"JOURNAL CLEANUP ERROR");
 d->recovery=false;d->error=NULL;return true;
}
#ifdef SIDECAR_TX_TEST
extern bool sc_test_cut(unsigned stage);
#define CUT(stage) do{if(sc_test_cut(stage))return error(d,"SIMULATED POWER CUT");}while(0)
#else
#define CUT(stage) ((void)0)
#endif
bool sc_save(sc_document_t *d){
 d->error=NULL;
 if(d->recovery || !sc_changes(d))return error(d,"NO CHANGES / RECOVERY");
 struct stat st;if(cmt_stat(d->source,&st) || !S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size!=d->source_size || st.st_mtime!=d->source_mtime)return error(d,"SOURCE CHANGED");
 bool present;if(!sidecar_find(d->source,canonical,sizeof(canonical),&present) || present!=d->existed || strcmp(canonical,d->target))return error(d,"SIDECAR CHANGED");
 uint32_t hash,size;if(present && (!file_hash(d->target,&hash,&size) || hash!=d->original_hash || size!=d->original_size))return error(d,"SIDECAR CHANGED");
 if(!auxiliary(d->target))return error(d,"TX PATH TOO LONG");
 if(!transaction_name(d))return false;
 if(exists(journal) || exists(pending) || exists(backup))return error(d,"TX FILE CONFLICT");
 uint32_t new_hash,new_size;if(!serialize(d,NULL,&new_hash,&new_size))return error(d,"SERIALIZE ERROR");
 char marker[100],verify[100];snprintf(marker,sizeof(marker),"MZSC1 %u %lu %lu %lu %lu\n",present ? 1U:0U,(unsigned long)d->original_hash,(unsigned long)d->original_size,(unsigned long)new_hash,(unsigned long)new_size);
 if(!write_file(journal,marker) || !marker_read(verify) || strcmp(marker,verify))return error(d,"JOURNAL WRITE ERROR");
 CUT(1);
 cmt_file_t *f=record_file_create(pending,sizeof(pending));if(!f)goto failed;
 bool ok=serialize(d,f,&hash,&size);if(cmt_fflush(f))ok=false;if(cmt_fclose(f))ok=false;
 if(!ok || hash!=new_hash || size!=new_size || !read_check(d,pending))goto failed;
 CUT(2);
 if(present && cmt_rename(d->target,backup))goto failed;
 CUT(3);
 if(cmt_rename(pending,d->target))goto failed;
 CUT(4);
 if(!read_check(d,d->target))goto failed;
 for(unsigned i=0;i<d->count;++i)if(d->entries[i].original!=d->entries[i].current){loader_mode_t mode;
  if(!sidecar_read(d->source,d->entries[i].record,&mode) || mode!=d->entries[i].current)goto failed;
 }
 /* Verified target is committed. Retain journal if cleanup needs recovery. */
 if(present && cmt_remove(backup)){d->recovery=true;return error(d,"SAVED / CLEANUP ERROR");}
 CUT(5);
 if(cmt_remove(journal)){d->recovery=true;return error(d,"SAVED / CLEANUP ERROR");}
 for(unsigned i=0;i<d->count;++i)d->entries[i].original=d->entries[i].current;
 d->original_hash=new_hash;d->original_size=new_size;d->existed=true;return true;
failed:
 /* If installing new failed, keep the old recoverable rather than delete data. */
 if(!exists(d->target) && exists(backup))(void)cmt_rename(backup,d->target);
 d->recovery=true;return error(d,"SAVE FAILED / RECOVER");
}
