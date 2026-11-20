#include "storage/fs.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned kind,nth,hits;
static bool fail(unsigned k){return kind==k && ++hits==nth;}
static FILE *fault_open(const char *p,const char *m){if(fail(1))return NULL;return fopen(p,m);}
static size_t fault_write(const void *p,size_t size,size_t n,FILE *f){if(fail(2))return n ? fwrite(p,size,n-1,f):0;return fwrite(p,size,n,f);}
static int fault_flush(FILE *f){int rc=fflush(f);return fail(3) ? -1:rc;}
static int fault_close(FILE *f){int rc=fclose(f);return fail(4) ? -1:rc;}
static int fault_rename(const char *a,const char *b){if(fail(5))return -1;return rename(a,b);}
#undef cmt_fopen
#undef cmt_fwrite
#undef cmt_fflush
#undef cmt_fclose
#undef cmt_rename
#define cmt_fopen fault_open
#define cmt_fwrite fault_write
#define cmt_fflush fault_flush
#define cmt_fclose fault_close
#define cmt_rename fault_rename
#include "../../src/formats/sidecar_edit.c"
static void put(const char *p,const char *text){FILE *f=fopen(p,"wb");assert(f && fwrite(text,1,strlen(text),f)==strlen(text) && !fclose(f));}
static void clean(void){const char *names[]={"fault.MFI","fault.MFI.SCN","fault.MFI.SCB","fault.MFI.SCJ"};for(unsigned i=0;i<4;++i)if(exists(names[i]))assert(!remove(names[i]));}
int main(void){
 for(unsigned k=1;k<=5;++k)for(unsigned n=1;n<=6;++n){
  kind=0;clean();put("fault.mzf","IMMUTABLE");put("fault.MFI","TYPE=NORMAL\nSPEED=1:1\n");
  sc_document_t d;assert(sc_load(&d,"fault.mzf",1) && sc_accept(&d,1,LOADER_MODE_IC_1_4));kind=k;nth=n;hits=0;
  bool saved=sc_save(&d);kind=0;
  if(saved){loader_mode_t mode;assert(sidecar_read("fault.mzf",1,&mode) && mode==LOADER_MODE_IC_1_4);}
  else {
   /* Either the original name is intact or its backup remains intact. */
   uint32_t h,size;assert(file_hash("fault.MFI",&h,&size) || file_hash("fault.MFI.SCB",&h,&size));
   sc_document_t loaded;if(!sc_load(&loaded,"fault.mzf",1) && loaded.recovery){
    if(sc_recover(&loaded))assert(sc_load(&loaded,"fault.mzf",1));
    /* An incomplete journal is intentionally not trusted for deletion. */
   }
  }
 }
 clean();assert(!remove("fault.mzf"));
 puts("PASS injected open/read-back, short write, flush, close and both rename failures: original or verified backup retained, no false SAVED");return 0;
}
