#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "formats/quickdisk.h"

static uint8_t wire[512],image[2048],header[64],data[]={0,128,1,255};
static size_t used;
static uint16_t crc_byte(uint16_t crc,uint8_t value) {
 for(unsigned i=0;i<8;++i) {unsigned bit=((crc>>15)&1)^(value&1);
 value>>=1;
 crc<<=1;
 if(bit)crc^=0x8005;
 }
 return crc;
}
static uint8_t reverse(uint8_t value) {uint8_t out=0;
for(unsigned i=0;i<8;++i){out=(uint8_t)((out<<1)|(value&1));
value>>=1;
}
return out;
}
static void emit(uint8_t v) {assert(used<sizeof(wire));
wire[used++]=v;
}
static void sync_word(void) {emit(0);
emit(0x16);
emit(0x16);
emit(0xA5);
}
static void crc_tail(uint16_t crc) {uint8_t a=reverse((uint8_t)(crc>>8)),b=reverse((uint8_t)crc);
assert(!crc_byte(crc_byte(crc,a),b));
emit(a);
emit(b);
}
static void frame(uint8_t type,const uint8_t *bytes,uint16_t n,bool logical) {
 sync_word();
 uint16_t crc=crc_byte(0,0xA5);
 emit(type);
 crc=crc_byte(crc,type);
 emit((uint8_t)n);
 crc=crc_byte(crc,(uint8_t)n);
 emit((uint8_t)(n>>8));
 crc=crc_byte(crc,(uint8_t)(n>>8));
 for(unsigned i=0;i<n;++i){emit(bytes[i]);
 crc=crc_byte(crc,bytes[i]);
 }
 if(logical){emit('C');
 emit('R');
 emit('C');
 }
else crc_tail(crc);
}
static void make_wire(bool logical,bool qdf) {
 used=0;
 if(qdf) {const uint8_t prefix[16]={'-','Q','D',' ','f','o','r','m','a','t','-',255,255,255,255,255};
 for(unsigned i=0;i<16;++i)emit(prefix[i]);
 }
 sync_word();
 emit(2);
 if(logical){emit('C');
 emit('R');
 emit('C');
 }
else crc_tail(crc_byte(crc_byte(0,0xA5),2));
 frame(0,header,64,logical);
 frame(5,data,4,logical);
}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));
}
static void physical(bool hxc,unsigned phase) {
 make_wire(false,false);
 memset(image,0,sizeof(image));
 if(hxc){memcpy(image,"HXCQDDRV",8);
 put32(image+12,1);
 put32(image+16,1);
 put32(image+36,0x200);
 }
 else {image[3]='Q';
 image[4]='D';
 }
 put32(image+0x200,0x400);
 put32(image+0x204,512);
 put32(image+0x20c,512);
 for(size_t i=0;i<used;++i)for(unsigned bit=0;bit<8;++bit) {
  uint32_t pos=(uint32_t)(phase+i*16+bit*2);
  if(wire[i]&(1U<<bit))image[0x400+pos/8]|=(uint8_t)(1U<<(pos%8));
 }
}
static FILE *open_bytes(const uint8_t *bytes,size_t size) {
 FILE *f=fopen("test.qd","wb");
 assert(f);
 assert(fwrite(bytes,1,size,f)==size && !fclose(f));
 f=fopen("test.qd","rb");
 assert(f);
 return f;
}
static void check(const uint8_t *bytes,size_t size,const char *kind) {
 FILE *f=open_bytes(bytes,size);
 assert(quickdisk_open(f,(uint32_t)size,NULL,NULL));
 assert(!strcmp(quickdisk_kind(),kind));
 uint8_t h[128],out[8]={0};
 uint32_t length;
 assert(quickdisk_count()==1 && quickdisk_select(1,h,&length) && length==4);
 assert(h[0]==1 && !memcmp(h+1,"QDTEST",6) && h[18]==4 && h[20]==0 && h[21]==0x12);
 assert(quickdisk_read(out,8)==4 && !memcmp(out,data,4));
 assert(!quickdisk_read(out,1));
 assert(quickdisk_seek(1) && quickdisk_read(out,2)==2 && !memcmp(out,data+1,2));
 assert(!quickdisk_seek(5) && !quickdisk_select(2,h,&length));
 quickdisk_close();
 assert(!fclose(f));
 assert(!remove("test.qd"));
}
static void invalid(const uint8_t *bytes,size_t size) {
 FILE *f=open_bytes(bytes,size);
 assert(!quickdisk_open(f,(uint32_t)size,NULL,NULL));
 quickdisk_close();
 assert(!fclose(f));
 assert(!remove("test.qd"));
}
static bool cancel(void *ctx) { return *(bool *)ctx;
}
int main(int argc,char **argv) {
 header[0]=1;
 memcpy(header+1,"QDTEST\r",7);
 header[20]=4;
 header[23]=header[25]=0x12;
 make_wire(true,false);
 check(wire,used,"MZQ");
 wire[used-1]^=1;
 invalid(wire,used);
 make_wire(false,true);
 check(wire,used,"QDF");
 wire[used-1]^=1;
 invalid(wire,used);
 for(unsigned phase=0;phase<16;++phase){physical(true,phase);
 check(image,sizeof(image),"MFM");
 }
 physical(false,9);
 check(image,sizeof(image),"MFM");
 physical(true,6);
 put32(image+0x200,UINT32_MAX);
 invalid(image,sizeof(image));
 physical(false,9);
 put32(image+0x204,UINT32_MAX);
 invalid(image,sizeof(image));
 make_wire(true,false);
 wire[4]=102;
 invalid(wire,used);
 make_wire(true,false);
 invalid(wire,used-1);
 invalid(wire,0);
 invalid(wire,7);
 make_wire(false,true);
 bool cancelled=true;
 FILE *f=open_bytes(wire,used);
 assert(!quickdisk_open(f,(uint32_t)used,cancel,&cancelled) && quickdisk_was_cancelled());
 quickdisk_close();
 fclose(f);
 cancelled=false;
 f=fopen("test.qd","rb");
 assert(f);
 assert(quickdisk_open(f,(uint32_t)used,cancel,&cancelled));
 uint8_t h[128],out[4];
 uint32_t length;
 assert(quickdisk_select(1,h,&length));
 cancelled=true;
 assert(quickdisk_read(out,4)==4 && !memcmp(out,data,4));
 quickdisk_close();
 fclose(f);
 assert(!remove("test.qd"));
 puts("PASS QuickDisk logical MZQ, QDF CRC, HxC all 16 MFM phases, FlashFloppy, bounded selected payload and header conversion");
 puts("PASS QuickDisk damaged CRC/trailer, truncation/count/offset limits, cancellation and no analysis callback during playback");
 for(int a=1;a<argc;++a) {
  FILE *input=fopen(argv[a],"rb");
  assert(input);
  assert(!fseek(input,0,SEEK_END));
  long end=ftell(input);
  assert(end>=0);
  bool ok=quickdisk_open(input,(uint32_t)end,NULL,NULL);
  printf("QD fixture %s: %s",argv[a],ok ? quickdisk_kind():"REJECT");
  if(ok) {uint16_t count=quickdisk_count();
  printf(" %u records",count);
   for(uint16_t r=1;r<=count;++r) {uint8_t h[128],bytes[257];
   uint32_t n,total=0;
   assert(quickdisk_select(r,h,&n));
   size_t got;
    while((got=quickdisk_read(bytes,sizeof(bytes)))!=0)total+=(uint32_t)got;
    assert(total==n);
    assert(quickdisk_seek(0));
   }
  } puts("");
  quickdisk_close();
  assert(!fclose(input));
 }
 return 0;
}
