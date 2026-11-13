#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "file_list.h"
#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#define delete_dir(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define make_dir(path) mkdir(path,0777)
#define delete_dir(path) rmdir(path)
#endif
unsigned host_heap_allocations;
static bool cancelled;
static bool cancel(void *context) { (void)context;return cancelled; }
static void touch(const char *path) { FILE *f=fopen(path,"wb");assert(f && !fclose(f)); }
static void select_name(const char *name) {
 unsigned budget=(unsigned)file_list_count()+1;
 while(strcmp(file_list_get_name(),name)) {assert(budget--);assert(file_list_next());}
}
int main(void) {
 assert(!make_dir("browser"));assert(!make_dir("browser/Zdir"));assert(!make_dir("browser/adir"));
 const char *other[]={"sample.wav","sample.QD","sample.QDF","sample.TAP","sample.MFI","sample.MTI","sample.M2I","sample.MZI"};
 char path[1024];for(unsigned i=0;i<8;++i){snprintf(path,sizeof(path),"browser/%s",other[i]);touch(path);}
 for(unsigned i=0;i<1000;++i){snprintf(path,sizeof(path),"browser/file%04u.MzQ",999-i);touch(path);}
 assert(file_list_init("browser/") && file_list_count()==1006 && host_heap_allocations==2);
 assert(!strcmp(file_list_get_name(),"adir") && file_list_get_type()==FILE_ITEM_DIR && !file_list_get_path());
 assert(file_list_next() && !strcmp(file_list_get_name(),"Zdir"));
 assert(file_list_next() && !strcmp(file_list_get_name(),"file0000.MzQ"));
 for(unsigned i=1;i<1000;++i){assert(file_list_next());char name[32];snprintf(name,sizeof(name),"file%04u.MzQ",i);assert(!strcmp(file_list_get_name(),name));}
 select_name("sample.TAP");int top=file_list_get_scroll_top();
 assert(file_list_reload_current_dir() && !strcmp(file_list_get_name(),"sample.TAP") && file_list_get_scroll_top()==top);
 assert(!file_list_go_parent());select_name("adir");assert(file_list_enter_selected());
 assert(file_list_count()==1 && file_list_get_type()==FILE_ITEM_PARENT);assert(file_list_enter_selected());assert(!strcmp(file_list_get_name(),"adir"));
 for(unsigned i=0;i<20;++i)assert(file_list_reload_current_dir());assert(file_list_init("browser"));assert(host_heap_allocations==2);
 file_list_set_scan_control(cancel,NULL);cancelled=true;assert(!file_list_reload_current_dir() && !file_list_count() && !strcmp(file_list_last_error(),"CANCELLED"));
 cancelled=false;assert(file_list_reload_current_dir() && file_list_count()==1006);assert(host_heap_allocations==2);
 const char *dir="browser/abcdefghijklmnopqrstuvwxyz0123456789";assert(!make_dir(dir));char long_name[245];
#ifdef _WIN32
 unsigned name_length=64; /* Legacy MinGW CRT has MAX_PATH; ESP paths are tested separately on device. */
#else
 unsigned name_length=240;
#endif
 memset(long_name,'x',name_length);memcpy(long_name+name_length,".tap",5);
 snprintf(path,sizeof(path),"%s/%s",dir,long_name);touch(path);assert(file_list_reload_current_dir());select_name("abcdefghijklmnopqrstuvwxyz0123456789");assert(file_list_enter_selected());
 assert(file_list_count()==2 && file_list_next() && !strcmp(file_list_get_name(),long_name));assert(!strcmp(file_list_get_path(),path));
#ifndef _WIN32
 assert(strlen(file_list_get_path())>256);
#endif
 assert(file_list_go_parent() && !strcmp(file_list_get_name(),"abcdefghijklmnopqrstuvwxyz0123456789"));assert(!remove(path) && !delete_dir(dir));
 for(unsigned i=0;i<1000;++i){snprintf(path,sizeof(path),"browser/file%04u.MzQ",i);assert(!remove(path));}
 for(unsigned i=0;i<8;++i){snprintf(path,sizeof(path),"browser/%s",other[i]);assert(!remove(path));}
 assert(!delete_dir("browser/adir") && !delete_dir("browser/Zdir") && !delete_dir("browser"));
 assert(!file_list_reload_current_dir() && !file_list_count());assert(!make_dir("browser"));assert(file_list_reload_current_dir());assert(!delete_dir("browser"));
 puts("PASS browser 1000+ files, case-insensitive stem/extension sort, hidden metadata, parent selection, retained scroll, path construction and cancel/retry");
 puts("PASS browser PSRAM index/name arena allocated exactly twice across repeated initialization and scans; no per-entry allocation");return 0;
}
