#pragma once
#include "storage/fs.h"
#include <string.h>
#ifndef ESP_PLATFORM
#include <fcntl.h>
#include <unistd.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif
/* Never truncate an existing recording on collision. Native handles and
 * creation execute on the storage worker in firmware. */
static inline cmt_file_t *record_file_create(const char *path,size_t capacity) {
    if(!path || !path[0] || strlen(path)>=capacity)return NULL;
#ifdef ESP_PLATFORM
    return cmt_create_exclusive(path);
#else
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_BINARY,0666);
    if(fd<0)return NULL;
    FILE *file=fdopen(fd,"wb");
    if(!file){close(fd);remove(path);}
    return file;
#endif
}
