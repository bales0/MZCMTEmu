#pragma once
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
typedef struct cmt_file cmt_file_t;
bool cmt_storage_init(void);
/* One bounded asynchronous job; callback/context must remain valid until completion. */
bool cmt_storage_execute_async(void (*function)(void *),void *context);
bool cmt_storage_execute(void (*function)(void *),void *context);
cmt_file_t *cmt_fopen(const char *,const char *);
cmt_file_t *cmt_create_exclusive(const char *);
size_t cmt_fread(void *,size_t,size_t,cmt_file_t *);
size_t cmt_fwrite(const void *,size_t,size_t,cmt_file_t *);
int cmt_fgetc(cmt_file_t *);
int cmt_fputc(int,cmt_file_t *);
int cmt_ungetc(int,cmt_file_t *);
int cmt_fseek(cmt_file_t *,long,int);
long cmt_ftell(cmt_file_t *);
int cmt_ferror(cmt_file_t *);
int cmt_feof(cmt_file_t *);
int cmt_fflush(cmt_file_t *);
int cmt_fclose(cmt_file_t *);
int cmt_fprintf(cmt_file_t *,const char *,...);
int cmt_remove(const char *);
int cmt_rename(const char *,const char *);
int cmt_stat(const char *,struct stat *);
int cmt_mkdir(const char *,mode_t);
DIR *cmt_opendir(const char *);
struct dirent *cmt_readdir(DIR *);
int cmt_closedir(DIR *);
#else
/* Host protocol tests use the same interface over native libc; firmware
 * owns every underlying descriptor and FAT/SPI operation on the storage task. */
typedef FILE cmt_file_t;
static inline bool cmt_storage_init(void) {return true;}
static inline bool cmt_storage_execute(void (*fn)(void *),void *arg) {fn(arg);return true;}
#ifdef CMT_STORAGE_ASYNC_TEST
bool cmt_storage_execute_async(void (*fn)(void *),void *arg);
#else
static inline bool cmt_storage_execute_async(void (*fn)(void *),void *arg) {fn(arg);return true;}
#endif
#define cmt_fopen fopen
#define cmt_fread fread
#define cmt_fwrite fwrite
#define cmt_fgetc fgetc
#define cmt_fputc fputc
#define cmt_ungetc ungetc
#define cmt_fseek fseek
#define cmt_ftell ftell
#define cmt_ferror ferror
#define cmt_feof feof
#define cmt_fflush fflush
#define cmt_fclose fclose
#define cmt_fprintf fprintf
#define cmt_remove remove
#define cmt_rename rename
#define cmt_stat stat
#define cmt_mkdir mkdir
#define cmt_opendir opendir
#define cmt_readdir readdir
#define cmt_closedir closedir
#endif
