#include "storage/fs.h"
/* SPDX-License-Identifier: GPL-3.0-only
 * Protocol algorithms from bales0/MZ-SD2CMT2-Reborn, firmware 2.0.3,
 * commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * Native ESP32-S3 foreground processing; no AVR peripheral code.
 */
#include "record/record_autoname.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <strings.h>

bool record_autoname_apply(char *path,size_t capacity) {
    const char *name=record_autoname_get_name();
    if(!path || !name || !capacity) return false;
    const char *slash=strrchr(path,'/'),*dot=strrchr(path,'.');
    if(!slash || !dot || dot<slash) return false;
    size_t directory=(size_t)(slash-path);
    char target[256];
    for(unsigned suffix=0;suffix<=99;++suffix) {
        int n=suffix ? snprintf(target,sizeof(target),"%.*s/%s_%02u%s",(int)directory,path,name,suffix,dot):
            snprintf(target,sizeof(target),"%.*s/%s%s",(int)directory,path,name,dot);
        if(n<0 || (size_t)n>=sizeof(target) || (size_t)n>=capacity) return false;
        if(!strcmp(path,target)) return true;
        struct stat st;
        if(!cmt_stat(target,&st)) continue;
        if(errno!=ENOENT) return false;
        if(!strcasecmp(dot,".MZF")) {
            char metadata[256];memcpy(metadata,target,(size_t)n+1);memcpy(metadata+n-3,"MFI",3);
            if(!cmt_stat(metadata,&st)) continue;
            if(errno!=ENOENT) return false;
        }
        /* This foreground session owns the filesystem while recording. */
        if(cmt_rename(path,target)) return false;
        memcpy(path,target,(size_t)n+1);return true;
    }
    return false;
}
