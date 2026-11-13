#include <stdio.h>
#include <stddef.h>
#undef fwrite
#undef fputc
#undef fseek
#undef fflush
#undef fclose
extern size_t fwrite(const void *,size_t,size_t,FILE *);
extern int fputc(int,FILE *);
extern int fseek(FILE *,long,int);
extern int fflush(FILE *);
extern int fclose(FILE *);
int io_failure;
size_t test_fwrite(const void *p,size_t size,size_t count,FILE *f) { return io_failure==1 ? 0:fwrite(p,size,count,f); }
int test_fputc(int value,FILE *f) { return io_failure==1 ? EOF:fputc(value,f); }
int test_fseek(FILE *f,long offset,int origin) { return io_failure==2 ? -1:fseek(f,offset,origin); }
int test_fflush(FILE *f) { return io_failure==3 ? EOF:fflush(f); }
int test_fclose(FILE *f) { int result=fclose(f);return io_failure==4 ? EOF:result; }
