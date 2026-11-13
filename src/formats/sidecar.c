#include "storage/fs.h"
/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from bales0/MZ-SD2CMT2-Reborn, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * ESP32-S3: native constant data and foreground algorithms; no AVR peripherals.
 */
#include "sidecar.h"
#include "storage/record_file.h"
#include "file_format.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

static bool fields(const char *type,const char *speed,loader_mode_t *out) {
    if (!strcasecmp(type,"UL")) { *out=LOADER_MODE_UL;
        return true;
        }
    if (!strcasecmp(type,"UL_MZ800")) { *out=LOADER_MODE_UL_MZ800;
        return true;
        }
    if (!strcasecmp(type,"UL_MZ700")) { *out=LOADER_MODE_UL_MZ700;
        return true;
        }
    if (strlen(speed)!=3 || speed[0]!='1' || speed[1]!=':' || speed[2]<'1' || speed[2]>'4') return false;
    static const loader_mode_t normal[]={LOADER_MODE_NORMAL_1_1,LOADER_MODE_NORMAL_1_2,LOADER_MODE_NORMAL_1_3,LOADER_MODE_NORMAL_1_4};
    static const loader_mode_t ic[]={LOADER_MODE_IC_1_1,LOADER_MODE_IC_1_2,LOADER_MODE_IC_1_3,LOADER_MODE_IC_1_4};
    static const loader_mode_t tc[]={LOADER_MODE_TC_1_1,LOADER_MODE_TC_1_2,LOADER_MODE_TC_1_3};
    unsigned speed_index=(unsigned)(speed[2]-'1');
    if (!strcasecmp(type,"NORMAL")) { *out=normal[speed_index];
        return true;
        }
    if (!strcasecmp(type,"IC")) { *out=ic[speed_index];
        return true;
        }
    if (!strcasecmp(type,"TC") && speed_index<3) { *out=tc[speed_index];
        return true;
        }
    if (!strcasecmp(type,"MZ700") && (speed_index==0 || speed_index==2)) {
        *out=speed_index ? LOADER_MODE_MZ700_3X : LOADER_MODE_MZ700_1X;
        return true;
    }
    return false;
}
static bool record_number(const char *s,uint16_t *out) {
    uint32_t n=0;
    if (!*s) return false;
    for(;*s;++s) { if (*s<'0'||*s>'9') return false;
        n=n*10+(unsigned)(*s-'0');
        if(n>65535) return false;
        }
    if (!n) return false;
    *out=(uint16_t)n;
    return true;
}
static bool single(cmt_file_t *fp,loader_mode_t *mode) {
    char text[128],type[12]={0},speed[5]={0};
    bool got_type=false,got_speed=false;
    size_t n=cmt_fread(text,1,sizeof(text),fp);
    if (!n || n>=sizeof(text) || cmt_ferror(fp)) return false;
    text[n]=0;
    char *line=text;
    while (*line) {
        char *end=line;
        while (*end && *end!='\r' && *end!='\n') ++end;
        char next=*end;
        *end=0;
        if (!got_type && !strncmp(line,"TYPE=",5) && line[5]) {
            if (strlen(line+5)>=sizeof(type)) return false;
            strcpy(type,line+5);
            got_type=true;
        }
        if (!got_speed && !strncmp(line,"SPEED=",6) && line[6]) {
            if (strlen(line+6)>=sizeof(speed)) return false;
            strcpy(speed,line+6);
            got_speed=true;
        }
        if (!next) break;
        line=end+1;
    }
    return fields(type,speed,mode);
}
static bool multi(cmt_file_t *fp,uint16_t wanted,loader_mode_t *mode) {
    char line[24],type[12]={0},speed[5]={0};
    unsigned len=0;
    bool overflow=false,active=false;
    for (;;) {
        int c=cmt_fgetc(fp);
        if (c==EOF || c=='\r' || c=='\n') {
            if (overflow && active) return false;
            if (!overflow && len) {
                line[len]=0;
                if (!strncmp(line,"RECORD=",7)) {
                    if (active) return fields(type,speed,mode);
                    uint16_t record;
                    if (record_number(line+7,&record)) active=record==wanted;
                } else if (active && !strncmp(line,"TYPE=",5)) {
                    if (strlen(line+5)>=sizeof(type)) return false;
                    strcpy(type,line+5);
                } else if (active && !strncmp(line,"SPEED=",6)) {
                    if (strlen(line+6)>=sizeof(speed)) return false;
                    strcpy(speed,line+6);
                }
            }
            len=0;
            overflow=false;
            if(c==EOF) return !cmt_ferror(fp) && active && fields(type,speed,mode);
        } else if (!overflow) {
            if (len<sizeof(line)-1) line[len++]=(char)c;
            else overflow=true;
        }
    }
}
bool sidecar_read(const char *path,uint16_t record,loader_mode_t *mode) {
    if (!path || !mode || !record) return false;
    file_format_t format=file_format_detect_from_name(path);
    const char *ext=format==FILE_FORMAT_MZF ? "MFI" : format==FILE_FORMAT_M12 ? "M2I" : format==FILE_FORMAT_MZT ? "MTI" : NULL;
    if (!ext) return false;
    char name[1024];
    size_t n=strlen(path);
    if (n<4 || n>=sizeof(name)) return false;
    memcpy(name,path,n+1);
    memcpy(name+n-3,ext,3);
    cmt_file_t *fp=cmt_fopen(name,"rb");
    if (!fp) { for(unsigned i=0;i<3;++i) name[n-3+i]=(char)(ext[i]>='A'&&ext[i]<='Z' ? ext[i]+32 : ext[i]);
        fp=cmt_fopen(name,"rb");
        }
    if (!fp) return false;
    loader_mode_t resolved=LOADER_MODE_NORMAL_1_1;
    bool ok=format==FILE_FORMAT_MZT ? multi(fp,record,&resolved) : single(fp,&resolved);
    if(cmt_fclose(fp)) ok=false;
    if(ok) *mode=resolved;
    return ok;
}
loader_mode_t sidecar_resolve(const char *path,uint16_t record,loader_mode_t requested,bool *used) {
    if(used) *used=false;
    if(requested!=LOADER_MODE_AUTO) return (unsigned)requested<LOADER_MODE_COUNT ? requested : LOADER_MODE_NORMAL_1_1;
    loader_mode_t mode=LOADER_MODE_NORMAL_1_1;
    bool ok=sidecar_read(path,record,&mode);
    if(used) *used=ok;
    return ok ? mode : LOADER_MODE_NORMAL_1_1;
}

bool sidecar_write_mzf(const char *path,loader_mode_t mode) {
    if(file_format_detect_from_name(path)!=FILE_FORMAT_MZF) return false;
    const char *type="NORMAL";unsigned speed=1;
    switch(mode) {
    case LOADER_MODE_NORMAL_1_1: break;
    case LOADER_MODE_NORMAL_1_2: speed=2;break;
    case LOADER_MODE_NORMAL_1_3: speed=3;break;
    case LOADER_MODE_NORMAL_1_4: speed=4;break;
    case LOADER_MODE_MZ700_1X: type="MZ700";break;
    case LOADER_MODE_IC_1_1: type="IC";break;
    case LOADER_MODE_IC_1_2: type="IC";speed=2;break;
    case LOADER_MODE_IC_1_3: type="IC";speed=3;break;
    case LOADER_MODE_IC_1_4: type="IC";speed=4;break;
    case LOADER_MODE_TC_1_1: type="TC";break;
    case LOADER_MODE_TC_1_2: type="TC";speed=2;break;
    case LOADER_MODE_TC_1_3: type="TC";speed=3;break;
    default: return false;
    }
    char name[1024];size_t n=strlen(path);
    if(n<4 || n>=sizeof(name)) return false;
    memcpy(name,path,n+1);memcpy(name+n-3,"MFI",3);
    cmt_file_t *out=record_file_create(name,sizeof(name));
    if(!out)return false;
    bool ok=cmt_fprintf(out,"TYPE=%s\nSPEED=1:%u\n",type,speed)>0 && !cmt_fflush(out);
    if(cmt_fclose(out)) ok=false;
    if(!ok) cmt_remove(name);
    return ok;
}
