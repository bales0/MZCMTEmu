#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "storage/fs.h"
#include "freertos/task.h"
#include "freertos/queue.h"
static int ui_task,storage_task;
static TaskHandle_t current=&ui_task;
static void *pending;
static bool notified,in_isr;
static unsigned rpc_count,worker_creations;
static int io_failure;
ssize_t cmt_test_write(int fd,const void *bytes,size_t n) {if(io_failure==1){errno=ENOSPC;
return -1;
}
return write(fd,bytes,n);
}
off_t cmt_test_seek(int fd,off_t offset,int origin) {if(io_failure==2){errno=EINVAL;
return -1;
}
return lseek(fd,offset,origin);
}
int cmt_test_sync(int fd) {
 if(io_failure==3){errno=EIO;
 return -1;
 }
#ifdef _WIN32
 return _commit(fd);
#else
 return fsync(fd);
#endif
}
int cmt_test_close(int fd) {int result=close(fd);
if(io_failure==4){errno=EIO;
return -1;
}
return result;
}
void cmt_storage_test_pump(void);
bool cmt_storage_test_is_owner(void) {return current==&storage_task;
}
bool xPortInIsrContext(void) {return in_isr;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void) {return current;
}
TaskHandle_t xTaskCreateStaticPinnedToCore(void (*entry)(void *),const char *name,unsigned stack,void *arg,unsigned priority,StackType_t *memory,StaticTask_t *control,int core) {
 assert(entry && !strcmp(name,"storage") && stack==8192 && !arg && priority==6 && memory && control && core==0);
 ++worker_creations;
 return &storage_task;
}
QueueHandle_t xQueueCreateStatic(unsigned capacity,unsigned item_size,uint8_t *memory,StaticQueue_t *control) {assert(capacity==4 && item_size==sizeof(void *) && memory && control);
return control;
}
int xQueueSend(QueueHandle_t queue,const void *item,TickType_t timeout) {assert(queue && !pending && current==&ui_task && (timeout==portMAX_DELAY || timeout==0));
memcpy(&pending,item,sizeof(pending));
++rpc_count;
return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue,void *out,TickType_t timeout) {assert(queue && current==&storage_task);
(void)timeout;
if(!pending)return pdFALSE;
memcpy(out,&pending,sizeof(pending));
pending=NULL;
return pdTRUE;
}
void xTaskNotifyGive(TaskHandle_t task) {assert(current==&storage_task && task==&ui_task);
notified=true;
}
uint32_t ulTaskNotifyTake(int clear,TickType_t timeout) {assert(clear==pdTRUE && timeout==portMAX_DELAY);
current=&storage_task;
cmt_storage_test_pump();
current=&ui_task;
assert(notified);
notified=false;
return 1;
}
static void nested(void *context) {assert(current==&storage_task);
struct stat st;
assert(cmt_stat(context,&st)==0);
}
int main(void) {
 assert(cmt_storage_init() && cmt_storage_init() && worker_creations==1);
 uint8_t bytes[2573],actual[2573];
 for(unsigned i=0;i<sizeof(bytes);++i)bytes[i]=(uint8_t)(i*37);
 cmt_file_t *f=cmt_create_exclusive("cache.bin");
 assert(f);
 unsigned before=rpc_count;
 for(unsigned i=0;i<sizeof(bytes);++i)assert(cmt_fputc(bytes[i],f)==bytes[i]);
 assert(cmt_ftell(f)==sizeof(bytes) && cmt_fflush(f)==0 && cmt_fclose(f)==0 && rpc_count-before<10);
 assert(!cmt_create_exclusive("cache.bin"));
 f=cmt_fopen("cache.bin","rb");
 assert(f);
 before=rpc_count;
 for(unsigned i=0;i<sizeof(bytes);++i) {assert(cmt_fgetc(f)==bytes[i]);
 if(i==123) {assert(cmt_ungetc(bytes[i],f)==bytes[i] && cmt_fgetc(f)==bytes[i]);
 }}
 assert(cmt_fgetc(f)==EOF && cmt_feof(f) && !cmt_ferror(f) && rpc_count-before<6);
 assert(!cmt_fseek(f,0,SEEK_SET) && cmt_fread(actual,1,sizeof(actual),f)==sizeof(actual) && !memcmp(bytes,actual,sizeof(bytes)));
 assert(!cmt_fseek(f,-3,SEEK_END) && cmt_ftell(f)==2570 && cmt_fread(actual,1,3,f)==3 && !memcmp(actual,bytes+2570,3));
 assert(!cmt_fclose(f));
 cmt_file_t *slots[8];
 for(unsigned i=0;i<8;++i) {slots[i]=cmt_fopen("cache.bin","rb");
 assert(slots[i]);
 }
 assert(!cmt_fopen("cache.bin","rb") && errno==EMFILE);
 for(unsigned i=0;i<8;++i)assert(!cmt_fclose(slots[i]));
 before=rpc_count;
 assert(cmt_storage_execute(nested,"cache.bin") && rpc_count==before+1);
 assert(cmt_storage_execute_async(nested,"cache.bin") && pending);
 assert(!cmt_storage_execute_async(nested,"cache.bin"));
 current=&storage_task;
 cmt_storage_test_pump();
 current=&ui_task;
 assert(!pending && !notified && cmt_storage_execute_async(nested,"cache.bin"));
 current=&storage_task;
 cmt_storage_test_pump();
 current=&ui_task;
 assert(!notified);
 in_isr=true;
 before=rpc_count;
 assert(!cmt_fopen("cache.bin","rb") && !cmt_storage_execute(nested,"cache.bin") && rpc_count==before);
 in_isr=false;
 for(int fault=1;fault<=4;++fault) {
  f=cmt_create_exclusive("error.bin");
  assert(f && cmt_fwrite(bytes,1,100,f)==100);
  io_failure=fault;
  if(fault==2)assert(cmt_fseek(f,2000,SEEK_SET)!=0);
  else if(fault!=4)assert(cmt_fflush(f)!=0);
  assert(cmt_fclose(f)!=0);
  io_failure=0;
  assert(!cmt_remove("error.bin"));
 }
 assert(!cmt_fopen("cache.bin","ab") && errno==EINVAL);
 f=cmt_create_exclusive("patch.bin");
 assert(f);
 before=rpc_count;
 for(unsigned i=0;i<100;++i)assert(cmt_fputc(bytes[i],f)==bytes[i]);
 assert(!cmt_fseek(f,0,SEEK_SET) && cmt_fputc(99,f)==99);
 assert(!cmt_fseek(f,100,SEEK_SET) && cmt_fputc(88,f)==88 && rpc_count==before);
 assert(!cmt_fclose(f));
 f=cmt_fopen("patch.bin","rb");
 assert(f);
 assert(cmt_fread(actual,1,101,f)==101 && actual[0]==99 && actual[100]==88 && !memcmp(actual+1,bytes+1,99));
 assert(!cmt_fclose(f) && !cmt_remove("patch.bin"));
 assert(!cmt_rename("cache.bin","renamed.bin"));
 struct stat st;
 assert(!cmt_stat("renamed.bin",&st) && st.st_size==sizeof(bytes));
 assert(!cmt_mkdir("folder",0777));
 DIR *dir=cmt_opendir("folder");
 assert(dir);
 while(cmt_readdir(dir)){}
assert(!cmt_closedir(dir));
 assert(!cmt_remove("renamed.bin"));
#ifdef _WIN32
 assert(!_rmdir("folder"));
#else
 assert(!rmdir("folder"));
#endif
 puts("PASS actual storage worker/cache: native operations only on owner, byte IO batched, seek/ungetc/EOF, exclusive creation, pool exhaustion, nested calls, ISR refusal and error propagation");
 return 0;
}
