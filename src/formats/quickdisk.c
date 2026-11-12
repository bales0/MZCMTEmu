/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from bales0/MZ-SD2CMT2-Reborn, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * ESP32-S3: native constant data and foreground algorithms; no AVR peripherals.
 */
#include "formats/quickdisk.h"
#include <string.h>

typedef enum { LOGICAL, QDF, PHYSICAL } kind_t;
typedef struct { uint32_t header,body;
    uint16_t length;
    } entry_t;
static FILE *fp;
static uint32_t file_size,track_offset,track_size,scan_pos,cache_offset,source_pos;
static uint16_t cache_size,records,selected;
static uint8_t phase,cache[512];
static entry_t entries[50];
static kind_t kind;
static quickdisk_cancel_fn cancel_fn;
static void *cancel_context;
static bool cancelled,io_error,scanning;
static bool poll_cancel(void) {
    if(scanning && cancel_fn && cancel_fn(cancel_context)) { cancelled=true;
        return true;
        }
    return cancelled;
}
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
    }
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0]|(uint16_t)((uint16_t)p[1]<<8);
    }
static uint16_t crc_byte(uint16_t crc,uint8_t value) {
    for(unsigned bit=0;bit<8;++bit) { unsigned mix=(value&1)^((crc>>15)&1);
        value>>=1;
        crc<<=1;
        if(mix)crc^=0x8005;
        }
    return crc;
}
static bool raw_byte(uint32_t position,uint8_t *out) {
    uint32_t limit=kind==PHYSICAL ? track_size:file_size;
    uint32_t base=kind==PHYSICAL ? track_offset:0;
    if(position>=limit) return false;
    if(!cache_size || position<cache_offset || position-cache_offset>=cache_size) {
        if(poll_cancel()) return false;
        uint32_t left=limit-position;
        size_t n=left<sizeof(cache) ? left:sizeof(cache);
        if(fseek(fp,(long)(base+position),SEEK_SET) || fread(cache,1,n,fp)!=n) {io_error=true;
            return false;
            }
        cache_offset=position;
        cache_size=(uint16_t)n;
    }
    *out=cache[position-cache_offset];
    return true;
}
static bool logical_byte(uint32_t position,uint8_t *out) {
    if(kind!=PHYSICAL) return raw_byte(position,out);
    uint64_t first=(uint64_t)phase+(uint64_t)position*16;
    if(first+14>=(uint64_t)track_size*8) return false;
    uint32_t offset=(uint32_t)(first>>3),raw;
    unsigned shift=(unsigned)(first&7);
    uint8_t value;
    if(!raw_byte(offset,&value)) return false;
    raw=value;
    if(!raw_byte(offset+1,&value))return false;
    raw|=(uint32_t)value<<8;
    if(shift>=2) {if(!raw_byte(offset+2,&value))return false;
        raw|=(uint32_t)value<<16;
        }
    uint16_t bits=(uint16_t)(raw>>shift)&0x5555;
    bits=(uint16_t)((bits|(bits>>1))&0x3333);
    bits=(uint16_t)((bits|(bits>>2))&0x0F0F);
    *out=(uint8_t)(bits|(bits>>4));
    return true;
}
static bool read_scan(uint8_t *out) { if(!logical_byte(scan_pos,out))return false;
    ++scan_pos;
    return true;
    }
static bool sync_frame(void) {
    if(kind==LOGICAL) {
        unsigned matched=0;
        uint8_t value;
        while(read_scan(&value)) {
            if(matched==0) matched=value==0 ? 1:0;
            else if(matched==1) matched=value==0x16 ? 2:value==0 ? 1:0;
            else if(matched==2) matched=value==0x16 ? 3:value==0 ? 1:0;
            else { if(value==0xA5)return true;
                matched=value==0 ? 1:0;
                }
        }
        return false;
    }
    uint8_t value;
    unsigned sync=0,since_break=255;
    bool seen=false;
    while(read_scan(&value)) {
        if(value==0) { seen=true;
            sync=0;
            since_break=0;
            }
        else if(kind==PHYSICAL) {
            if(since_break!=255) ++since_break;
            if(value==0x16) {if(sync!=255)++sync;
                }
            else { if(value==0xA5 && sync>=2 && since_break<=sync+17) return true;
                sync=0;
                }
        } else if(seen && value==0x16) { if(sync!=255)++sync;
            }
        else if(seen && value==0xA5 && sync>=2) return true;
        else {seen=false;
            sync=0;
            }
    }
    return false;
}
static bool count_frame(uint8_t *count) {
    uint8_t a,b;
    if(!sync_frame() || !read_scan(count) || !read_scan(&a) || !read_scan(&b)) return false;
    uint16_t crc=crc_byte(crc_byte(crc_byte(crc_byte(0,0xA5),*count),a),b);
    return !crc && !(*count&1) && *count<=100;
}
static bool frame(bool header,uint8_t bytes[64],uint16_t *length,uint32_t *position) {
    uint8_t type,lo,hi,value;
    if(!sync_frame() || !read_scan(&type) || !read_scan(&lo) || !read_scan(&hi))return false;
    if(header ? type!=0 : type!=1 && type!=5)return false;
    *length=(uint16_t)lo|(uint16_t)((uint16_t)hi<<8);
    if(header && *length!=64)return false;
    *position=scan_pos;
    uint16_t crc=crc_byte(crc_byte(crc_byte(crc_byte(0,0xA5),type),lo),hi);
    for(uint32_t i=0;i<*length;++i) {
        if(!read_scan(&value))return false;
        if(header)bytes[i]=value;
        crc=crc_byte(crc,value);
    }
    if(kind==LOGICAL) {
        uint8_t trailer[3];
        for(unsigned i=0;i<3;++i)if(!read_scan(&trailer[i]))return false;
        return !memcmp(trailer,"CRC",3);
    }
    for(unsigned i=0;i<2;++i) {if(!read_scan(&value))return false;
        crc=crc_byte(crc,value);
        }
    return crc==0;
}
static bool configure(const uint8_t *prefix,bool hxc) {
    uint32_t descriptor=hxc ? le32(prefix+36):0x200;
    if(hxc && (file_size<40 || le32(prefix+12)!=1 || le32(prefix+16)!=1 || le32(prefix+20)))return false;
    if(descriptor>file_size || file_size-descriptor<16)return false;
    uint8_t bytes[16];
    if(fseek(fp,(long)descriptor,SEEK_SET) || fread(bytes,1,16,fp)!=16)return false;
    track_offset=le32(bytes);
    track_size=le32(bytes+4);
    return track_offset>=0x400 && track_size && track_offset<=file_size && track_size<=file_size-track_offset &&
        le32(bytes+8)<=le32(bytes+12) && le32(bytes+12)<=track_size;
}
void quickdisk_close(void) { fp=NULL;
    records=selected=0;
    cache_size=0;
    scanning=false;
    }
bool quickdisk_open(FILE *file,uint32_t size,quickdisk_cancel_fn cancel,void *context) {
    quickdisk_close();
    fp=file;
    file_size=size;
    cancel_fn=cancel;
    cancel_context=context;
    cancelled=io_error=false;
    scanning=true;
    if(!fp || size<8)return false;
    uint8_t prefix[40]={0};
    size_t n=size<sizeof(prefix) ? size:sizeof(prefix);
    if(fseek(fp,0,SEEK_SET) || fread(prefix,1,n,fp)!=n)return false;
    static const uint8_t qdf[16]={'-','Q','D',' ','f','o','r','m','a','t','-',255,255,255,255,255};
    uint8_t count=0;
    if(n>=8 && !memcmp(prefix,"HXCQDDRV",8)) {
        kind=PHYSICAL;
        if(n<40 || !configure(prefix,true))return false;
        phase=6;
    } else if(n>=16 && !memcmp(prefix,qdf,16)) {kind=QDF;
        scan_pos=16;
        }
    else if(prefix[3]=='Q' && prefix[4]=='D') {kind=PHYSICAL;
        if(!configure(prefix,false))return false;
        phase=9;
        }
    else {kind=LOGICAL;
        if(memcmp(prefix,"\0\x16\x16\xA5",4) || memcmp(prefix+5,"CRC",3))return false;
        count=prefix[4];
        scan_pos=8;
        }
    cache_size=0;
    if(kind==PHYSICAL) {
        uint8_t preferred=phase;
        bool found=false;
        for(unsigned attempt=0;attempt<16 && !found;++attempt) {
            phase=attempt==0 ? preferred:(uint8_t)(attempt-(attempt<=preferred ? 1:0));
            scan_pos=0;
            cache_size=0;
            while(count_frame(&count)) {found=true;
                break;
                }
            /* A wrong phase can contain a false sync before the real count. */
            if(!found && !cancelled && !io_error) {
                while(sync_frame()) {
                    uint8_t value,a,b;
                    if(!read_scan(&value)||!read_scan(&a)||!read_scan(&b))break;
                    uint16_t crc=crc_byte(crc_byte(crc_byte(crc_byte(0,0xA5),value),a),b);
                    if(!crc && !(value&1) && value<=100) {count=value;
                        found=true;
                        break;
                        }
                }
            }
            if(cancelled || io_error)return false;
        }
        if(!found)return false;
    } else if(kind==QDF && !count_frame(&count))return false;
    if(!count || (count&1) || count>100)return false;
    records=count/2;
    for(unsigned i=0;i<records;++i) {
        uint8_t h[64];
        uint16_t header_length,length;
        uint32_t header_pos,body_pos;
        if(!frame(true,h,&header_length,&header_pos) || !frame(false,NULL,&length,&body_pos) || le16(h+20)!=length) {
            records=0;
            return false;
        }
        entries[i]=(entry_t){header_pos,body_pos,length};
    }
    scanning=false;
    return !cancelled && !io_error;
}
uint16_t quickdisk_count(void) { return records;
    }
bool quickdisk_select(uint16_t record,uint8_t header[128],uint32_t *length) {
    if(!fp || !record || record>records || !header || !length)return false;
    entry_t *e=&entries[record-1];
    uint8_t h[64];
    for(unsigned i=0;i<64;++i)if(!logical_byte(e->header+i,&h[i]))return false;
    if(le16(h+20)!=e->length)return false;
    memset(header,0,128);
    memcpy(header,h,18);
    header[18]=(uint8_t)e->length;
    header[19]=(uint8_t)(e->length>>8);
    memcpy(header+20,h+22,42);
    selected=record;
    source_pos=0;
    *length=e->length;
    return true;
}
bool quickdisk_seek(uint32_t pos) { if(!selected || pos>entries[selected-1].length)return false;
    source_pos=pos;
    return true;
    }
size_t quickdisk_read(void *out,size_t size) {
    if(!fp || !selected || !out)return 0;
    entry_t *e=&entries[selected-1];
    uint8_t *bytes=out;
    size_t got=0;
    while(got<size && source_pos<e->length) { if(!logical_byte(e->body+source_pos,&bytes[got]))break;
        ++got;
        ++source_pos;
        }
    return got;
}
bool quickdisk_was_cancelled(void) { return cancelled;
    }
const char *quickdisk_kind(void) { return kind==PHYSICAL ? "MFM":kind==QDF ? "QDF":"MZQ";
    }
