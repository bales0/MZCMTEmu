#include "storage/fs.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#if CMT_STORAGE_DIAGNOSTICS
#ifdef CMT_STORAGE_TEST
#include <time.h>
static uint64_t metrics_now(void){struct timespec t;timespec_get(&t,TIME_UTC);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
#else
#include "esp_timer.h"
static portMUX_TYPE metrics_lock=portMUX_INITIALIZER_UNLOCKED;
static uint64_t metrics_now(void){return (uint64_t)esp_timer_get_time();}
#endif
static cmt_read_metrics_t read_metrics;
static void metrics_add(uint64_t *field,uint64_t value){
#ifndef CMT_STORAGE_TEST
 portENTER_CRITICAL(&metrics_lock);
#endif
 *field+=value;
#ifndef CMT_STORAGE_TEST
 portEXIT_CRITICAL(&metrics_lock);
#endif
}
#define METRIC_ADD(field,value) metrics_add(&read_metrics.field,(value))
#else
#define METRIC_ADD(field,value) ((void)0)
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_attr.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifdef CMT_STORAGE_TEST
#include <assert.h>
extern bool cmt_storage_test_is_owner(void);
extern ssize_t cmt_test_write(int,const void *,size_t);
extern off_t cmt_test_seek(int,off_t,int);
extern int cmt_test_sync(int);
extern int cmt_test_close(int);
#define write cmt_test_write
#define lseek cmt_test_seek
#define fsync cmt_test_sync
#define close cmt_test_close
#endif
#if defined(_WIN32) && defined(CMT_STORAGE_TEST)
#include <direct.h>
#endif

#define FILE_SLOTS 8U
#define CACHE_BYTES 1024U
#define STORAGE_STACK_BYTES 8192U
struct cmt_file {
    int native;
    bool open;
    uint64_t position,cache_start;
    size_t cache_length,cache_position;
    bool writing,io_error,eof;
    uint8_t cache[CACHE_BYTES];
};
/* Caches never enter an ISR/DMA path. Native descriptors live only here;
 * no stdio FILE/buffer allocation occurs when opening each tape. */
static struct cmt_file files[FILE_SLOTS];
typedef enum {OPEN,CREATE,READ,WRITE,SEEK,FLUSH,CLOSE,REMOVE,RENAME,STAT,MKDIR,OPENDIR,READDIR,CLOSEDIR,EXECUTE} operation_t;
typedef struct {
    operation_t op;TaskHandle_t caller;cmt_file_t *file;
    const char *path,*other;void *buffer;size_t size;
    long offset;int origin,code,saved_errno;size_t count;void *result;
    bool io_error,eof;void (*function)(void *);void *context;
} request_t;
static TaskHandle_t worker;
static request_t async_request;
static bool async_busy;
static QueueHandle_t requests;
static StaticQueue_t queue_control;
static uint8_t queue_storage[4*sizeof(request_t *)];
static StaticTask_t task_control;
static StackType_t task_stack[STORAGE_STACK_BYTES/sizeof(StackType_t)];

static void perform(request_t *r) {
#ifdef CMT_STORAGE_TEST
    assert(cmt_storage_test_is_owner());
#endif
    errno=0;r->code=0;r->count=0;r->result=NULL;r->io_error=r->eof=false;
    int native=r->file ? r->file->native:-1;
    switch(r->op) {
    case OPEN:case CREATE: {
        cmt_file_t *slot=NULL;
        for(unsigned i=0;i<FILE_SLOTS;++i)if(!files[i].open){slot=&files[i];break;}
        if(!slot) {errno=EMFILE;break;}
        int flags=r->op==CREATE ? O_WRONLY|O_CREAT|O_EXCL : !strcmp(r->other,"rb") ? O_RDONLY:O_WRONLY|O_CREAT|O_TRUNC;
        int opened=open(r->path,flags|O_BINARY,0666);
        if(opened>=0) {
            memset(slot,0,sizeof(*slot));slot->native=opened;slot->open=true;
            slot->writing=r->op==CREATE || !strcmp(r->other,"wb");r->result=slot;
            if(slot->writing)cmt_storage_invalidate();
        }
        break;
    }
    case READ: {
        #if CMT_STORAGE_DIAGNOSTICS
        uint64_t started=metrics_now();
        #endif
        ssize_t n=read(native,r->buffer,r->size);
        #if CMT_STORAGE_DIAGNOSTICS
        uint64_t elapsed=metrics_now()-started;
        METRIC_ADD(native_us,elapsed);
        #endif
        METRIC_ADD(read_calls,1);METRIC_ADD(read_bytes,n>0 ? (uint64_t)n:0);
        r->io_error=n<0;
        r->count=n<0 ? 0:(size_t)n;r->eof=n>=0 && r->count<r->size;break;
    }
    case WRITE: {
        cmt_storage_invalidate();
        ssize_t n=write(native,r->buffer,r->size);r->count=n<0 ? 0:(size_t)n;
        r->io_error=n<0 || r->count!=r->size;break;
    }
    case SEEK: {
        off_t pos=lseek(native,r->offset,r->origin);r->code=pos<0 || (uint64_t)pos>LONG_MAX ? -1:0;
        if(!r->code)r->count=(size_t)pos;
        break;
    }
    case FLUSH:r->code=fsync(native);break;
    case CLOSE:r->code=close(native);if(r->file->writing)cmt_storage_invalidate();r->file->open=false;break;
    case REMOVE:r->code=remove(r->path);break;
    case RENAME:r->code=rename(r->path,r->other);break;
    case STAT:r->code=stat(r->path,r->buffer);break;
    case MKDIR:
#if defined(_WIN32) && defined(CMT_STORAGE_TEST)
        r->code=_mkdir(r->path);
#else
        r->code=mkdir(r->path,(mode_t)r->origin);
#endif
        break;
    case OPENDIR:r->result=opendir(r->path);break;
    case READDIR:r->result=readdir(r->buffer);break;
    case CLOSEDIR:r->code=closedir(r->buffer);break;
    case EXECUTE:r->function(r->context);break;
    }
    r->saved_errno=errno;
}
static bool process_one(TickType_t timeout) {
    request_t *r;
    if(xQueueReceive(requests,&r,timeout)!=pdTRUE)return false;
    perform(r);
    if(r->caller)xTaskNotifyGive(r->caller);
    else __atomic_store_n(&async_busy,false,__ATOMIC_RELEASE);
    return true;
}
#ifdef CMT_STORAGE_TEST
void cmt_storage_test_pump(void) {(void)process_one(0);}
#endif
static void storage_loop(void *context) {
    (void)context;
    for(;;) {
        (void)process_one(portMAX_DELAY);
    }
}
bool cmt_storage_init(void) {
    if(worker)return true;
    requests=xQueueCreateStatic(4,sizeof(request_t *),queue_storage,&queue_control);
    if(!requests)return false;
    worker=xTaskCreateStaticPinnedToCore(storage_loop,"storage",STORAGE_STACK_BYTES,NULL,6,task_stack,&task_control,0);
    return worker!=NULL;
}
static bool allowed(void) {if(xPortInIsrContext()){errno=EPERM;return false;}return true;}
static bool submit(request_t *r) {
    if(!allowed() || !worker) {errno=EIO;return false;}
    r->caller=xTaskGetCurrentTaskHandle();
    if(r->caller==worker) {perform(r);return true;}
#if CMT_STORAGE_DIAGNOSTICS
    uint64_t started=r->op==READ ? metrics_now():0;
#endif
    /* Notification slot 0 is reserved for synchronous filesystem requests in
     * the UI/player tasks. Their request stays on the caller stack until ack. */
    if(xQueueSend(requests,&r,portMAX_DELAY)!=pdTRUE) {errno=EIO;return false;}
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);errno=r->saved_errno;
#if CMT_STORAGE_DIAGNOSTICS
    if(r->op==READ)METRIC_ADD(submit_us,metrics_now()-started);
#endif
    return true;
}
bool cmt_storage_execute(void (*fn)(void *),void *arg) {
    request_t r={.op=EXECUTE,.function=fn,.context=arg};return fn && submit(&r);
}
bool cmt_storage_execute_async(void (*fn)(void *),void *context) {
    if(!allowed() || !worker || !fn)return false;
    bool expected=false;
    if(!__atomic_compare_exchange_n(&async_busy,&expected,true,false,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))return false;
    async_request=(request_t){.op=EXECUTE,.function=fn,.context=context};request_t *r=&async_request;
    if(xQueueSend(requests,&r,0)!=pdTRUE) {__atomic_store_n(&async_busy,false,__ATOMIC_RELEASE);return false;}
    return true;
}
cmt_file_t *cmt_fopen(const char *path,const char *mode) {
    if(!path || !mode || (strcmp(mode,"rb") && strcmp(mode,"wb"))) {errno=EINVAL;return NULL;}
    request_t r={.op=OPEN,.path=path,.other=mode};return submit(&r) ? r.result:NULL;
}
cmt_file_t *cmt_create_exclusive(const char *path) {
    if(!path || !path[0])return NULL;
    request_t r={.op=CREATE,.path=path};return submit(&r) ? r.result:NULL;
}
static bool valid(cmt_file_t *f) {return allowed() && f && f->open;}
static bool flush_cache(cmt_file_t *f) {
    if(!f->writing || !f->cache_length)return true;
    request_t r={.op=WRITE,.file=f,.buffer=f->cache,.size=f->cache_length};
    bool ok=submit(&r) && r.count==f->cache_length && !r.io_error;
    if(ok && f->position!=f->cache_start+f->cache_length) {
        request_t seek={.op=SEEK,.file=f,.offset=(long)f->position,.origin=SEEK_SET};
        ok=submit(&seek) && !seek.code;
    }
    f->cache_start=f->position;f->cache_length=f->cache_position=0;
    if(!ok)f->io_error=true;
    return ok;
}
size_t cmt_fread(void *out,size_t size,size_t count,cmt_file_t *f) {
    if(!valid(f) || f->writing || !size || !count || !out)return 0;
    if(count>SIZE_MAX/size) {f->io_error=true;errno=EOVERFLOW;return 0;}
    size_t total=size*count,received=0;uint8_t *bytes=out;
    while(received<total) {
        if(f->cache_position<f->cache_length) {
            size_t n=f->cache_length-f->cache_position;if(n>total-received)n=total-received;
            #if CMT_STORAGE_DIAGNOSTICS
            uint64_t started=metrics_now();
            #endif
            memcpy(bytes+received,f->cache+f->cache_position,n);
            #if CMT_STORAGE_DIAGNOSTICS
            uint64_t elapsed=metrics_now()-started;
            METRIC_ADD(copy_us,elapsed);
            #endif
            METRIC_ADD(copy_bytes,n);
            f->cache_position+=n;f->position+=n;received+=n;continue;
        }
        if(f->eof || f->io_error)break;
        request_t r={.op=READ,.file=f,.buffer=f->cache,.size=CACHE_BYTES};
        if(!submit(&r)) {f->io_error=true;break;}
        f->cache_start=f->position;f->cache_length=r.count;f->cache_position=0;f->io_error=r.io_error;f->eof=r.eof;
        if(!r.count)break;
    }
    return received/size;
}
size_t cmt_fwrite(const void *input,size_t size,size_t count,cmt_file_t *f) {
    if(size && count)cmt_storage_invalidate();
    if(!valid(f) || !f->writing || !size || !count || !input || f->io_error)return 0;
    if(count>SIZE_MAX/size) {f->io_error=true;errno=EOVERFLOW;return 0;}
    size_t total=size*count,written=0;const uint8_t *bytes=input;
    while(written<total) {
        if(f->cache_length==CACHE_BYTES && !flush_cache(f))break;
        if(!f->cache_length && total-written>=CACHE_BYTES) {
            request_t r={.op=WRITE,.file=f,.buffer=(void *)(bytes+written),.size=total-written};
            if(!submit(&r)){f->io_error=true;break;}
            written+=r.count;f->position+=r.count;f->cache_start=f->position;
            if(r.io_error){f->io_error=true;break;}continue;
        }
        size_t n=CACHE_BYTES-f->cache_position;if(n>total-written)n=total-written;
        memcpy(f->cache+f->cache_position,bytes+written,n);f->cache_position+=n;
        if(f->cache_position>f->cache_length)f->cache_length=f->cache_position;
        written+=n;f->position+=n;
    }
    return written/size;
}
int cmt_fgetc(cmt_file_t *f) {uint8_t value;return cmt_fread(&value,1,1,f)==1 ? value:EOF;}
int cmt_fputc(int value,cmt_file_t *f) {uint8_t byte=(uint8_t)value;return cmt_fwrite(&byte,1,1,f)==1 ? byte:EOF;}
int cmt_ungetc(int value,cmt_file_t *f) {
    if(!valid(f) || f->writing || value==EOF || !f->cache_position || f->cache[f->cache_position-1]!=(uint8_t)value)return EOF;
    --f->cache_position;--f->position;f->eof=false;return (uint8_t)value;
}
long cmt_ftell(cmt_file_t *f) {if(!valid(f) || f->position>LONG_MAX){errno=EOVERFLOW;return -1;}return (long)f->position;}
int cmt_fseek(cmt_file_t *f,long offset,int origin) {
    if(!valid(f))return -1;
    int64_t absolute=origin==SEEK_SET ? offset:origin==SEEK_CUR ? (int64_t)f->position+offset:-1;
    if(absolute>=0 && (uint64_t)absolute>=f->cache_start && (uint64_t)absolute<=f->cache_start+f->cache_length) {
        f->cache_position=(size_t)((uint64_t)absolute-f->cache_start);f->position=(uint64_t)absolute;f->eof=false;return 0;
    }
    if(!flush_cache(f))return -1;
    if(origin==SEEK_CUR && (absolute<0 || absolute>LONG_MAX)) {errno=EINVAL;return -1;}
    request_t r={.op=SEEK,.file=f,.offset=origin==SEEK_CUR ? (long)absolute:offset,.origin=origin==SEEK_CUR ? SEEK_SET:origin};
    if(!submit(&r) || r.code) {f->io_error=true;return -1;}
    f->position=r.count;f->cache_start=f->position;f->cache_length=f->cache_position=0;f->eof=false;return 0;
}
int cmt_ferror(cmt_file_t *f) {return !valid(f) || f->io_error;}
int cmt_feof(cmt_file_t *f) {return valid(f) && f->eof && f->cache_position==f->cache_length;}
int cmt_fflush(cmt_file_t *f) {
    if(!valid(f) || !flush_cache(f))return EOF;
    request_t r={.op=FLUSH,.file=f};if(!submit(&r) || r.code){f->io_error=true;return EOF;}return 0;
}
int cmt_fclose(cmt_file_t *f) {
    if(!valid(f))return EOF;
    bool ok=flush_cache(f) && !f->io_error;
    request_t r={.op=CLOSE,.file=f};return submit(&r) && !r.code && ok ? 0:EOF;
}
int cmt_fprintf(cmt_file_t *f,const char *format,...) {
    char text[192];va_list args;va_start(args,format);int n=vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(n<0 || (size_t)n>=sizeof(text)){errno=EOVERFLOW;return -1;}
    return cmt_fwrite(text,1,(size_t)n,f)==(size_t)n ? n:-1;
}
int cmt_remove(const char *path) {cmt_storage_invalidate();request_t r={.op=REMOVE,.path=path};return submit(&r) ? r.code:-1;}
int cmt_rename(const char *from,const char *to) {cmt_storage_invalidate();request_t r={.op=RENAME,.path=from,.other=to};return submit(&r) ? r.code:-1;}
int cmt_stat(const char *path,struct stat *st) {request_t r={.op=STAT,.path=path,.buffer=st};return submit(&r) ? r.code:-1;}
int cmt_mkdir(const char *path,mode_t mode) {request_t r={.op=MKDIR,.path=path,.origin=(int)mode};return submit(&r) ? r.code:-1;}
DIR *cmt_opendir(const char *path) {request_t r={.op=OPENDIR,.path=path};return submit(&r) ? r.result:NULL;}
struct dirent *cmt_readdir(DIR *dir) {request_t r={.op=READDIR,.buffer=dir};return submit(&r) ? r.result:NULL;}
int cmt_closedir(DIR *dir) {request_t r={.op=CLOSEDIR,.buffer=dir};return submit(&r) ? r.code:-1;}
#endif

static uint32_t media_epoch=1;
uint32_t cmt_storage_epoch(void){return __atomic_load_n(&media_epoch,__ATOMIC_ACQUIRE);}
void cmt_storage_invalidate(void){__atomic_add_fetch(&media_epoch,1,__ATOMIC_ACQ_REL);}
bool cmt_storage_read_metrics(cmt_read_metrics_t *out,bool reset){
 if(!out)return false;
#if defined(ESP_PLATFORM) && CMT_STORAGE_DIAGNOSTICS
#ifndef CMT_STORAGE_TEST
 portENTER_CRITICAL(&metrics_lock);
#endif
 *out=read_metrics;if(reset)memset(&read_metrics,0,sizeof(read_metrics));
#ifndef CMT_STORAGE_TEST
 portEXIT_CRITICAL(&metrics_lock);
#endif
 return true;
#else
 (void)reset;*out=(cmt_read_metrics_t){0};return false;
#endif
}
