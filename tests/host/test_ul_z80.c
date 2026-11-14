/* Small instruction interpreter for the generated UL/UL700/UL800 receivers, not a full MZ emulator.
 * ROM initialization/banking is skipped. PC5=!READ, PC4=!SENSE, WRITE=!PC1
 * are explicit idealized test assumptions; MOTOR gating and analog paths are not modeled.
 * 14 MHz time units represent a 3.5 MHz Z80 exactly (4 units per T state). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "play/mzf_loader.h"
static uint8_t mem[65536],data[256],read_pin,sense_pin,pc1;
static unsigned cursor,copied;static uint16_t load_dest;static int sinv,winv,done,z;
static uint16_t pc,bc,de,hl,sp,ix,bc2,de2,hl2;static uint8_t a;static uint64_t cpu_ticks;
static bool seek_src(uint32_t p){if(p<128||p>384)return false;cursor=p-128;return true;}
static int16_t read_src(void *b,uint16_t n){if(cursor+n>256)return 0;memcpy(b,data+cursor,n);cursor+=n;return n;}
static uint16_t imm16(void){uint16_t v=mem[pc++];v|=(uint16_t)mem[pc++]<<8;return v;}
static uint8_t rd(uint16_t addr){if(addr==0xE002)return (uint8_t)(((!read_pin)<<5)|((sense_pin^sinv)<<4));return mem[addr];}
static void wr(uint16_t addr,uint8_t v){if(addr==0xE003){assert((v>>1)==1);pc1=v&1;}else{mem[addr]=v;if(addr>=load_dest&&addr<load_dest+256)++copied;}}
static void step(void){unsigned t=0;uint8_t op=mem[pc++],v;int8_t rel;switch(op){
case 0x01:bc=imm16();t=10;break;case 0x21:hl=imm16();t=10;break;case 0x11:de=imm16();t=10;break;
case 0x00:t=4;break;
case 0x3E:a=mem[pc++];t=7;break;
case 0xD3:++pc;t=11;break; /* Banking is outside this receiver/PIO model. */
case 0x31:sp=imm16();t=10;break;
case 0xAF:a=0;z=1;t=4;break;
case 0x3D:--a;z=!a;t=4;break;
case 0xDD:{v=mem[pc++];if(v==0x21){ix=imm16();t=14;}
 else {assert(v==0x36);int8_t disp=(int8_t)mem[pc++];uint8_t val=mem[pc++];wr((uint16_t)(ix+disp),val);t=19;}break;}
case 0xED:{v=mem[pc++];uint16_t addr=imm16();if(v==0x73){wr(addr,(uint8_t)sp);wr(addr+1,(uint8_t)(sp>>8));}
 else {assert(v==0x7B);sp=(uint16_t)(rd(addr)|((uint16_t)rd(addr+1)<<8));}t=20;break;}
case 0xD5:mem[--sp]=(uint8_t)(de>>8);mem[--sp]=(uint8_t)de;t=11;break;
case 0xE1:hl=mem[sp++];hl|=(uint16_t)mem[sp++]<<8;t=10;break;
case 0xE9:pc=hl;assert(pc==load_dest);done=1;t=4;break;
case 0xF3:t=4;break;case 0x1A:a=rd(de);t=7;break;case 0xE6:a&=mem[pc++];z=!a;t=7;break;
case 0x28:case 0x20:rel=(int8_t)mem[pc++];if((op==0x28)==z){pc=(uint16_t)(pc+rel);t=12;}else t=7;break;
case 0xCB:v=mem[pc++];assert(v==0x67);z=!(a&16);t=8;break;
case 0x13:++de;t=6;break;case 0x1B:--de;t=6;break;case 0x7B:a=(uint8_t)de;t=4;break;
case 0x12:wr(de,a);t=7;break;
case 0xC5:mem[--sp]=(uint8_t)(bc>>8);mem[--sp]=(uint8_t)bc;t=11;break;
case 0xC1:bc=mem[sp++];bc|=(uint16_t)mem[sp++]<<8;t=10;break;
case 0xB1:a|=(uint8_t)bc;z=!a;t=4;break;case 0x07:a=(uint8_t)((a<<1)|(a>>7));t=4;break;
case 0x4F:bc=(uint16_t)((bc&0xff00)|a);t=4;break;
case 0x10:rel=(int8_t)mem[pc++];bc=(uint16_t)((bc&255)|((uint16_t)(uint8_t)((bc>>8)-1)<<8));if(bc>>8){pc=(uint16_t)(pc+rel);t=13;}else t=8;break;
case 0x71:wr(hl,(uint8_t)bc);t=7;break;case 0x0B:--bc;t=6;break;case 0x23:++hl;t=6;break;
case 0x79:a=(uint8_t)bc;t=4;break;case 0xB0:a|=(uint8_t)(bc>>8);z=!a;t=4;break;
case 0x06:bc=(uint16_t)((bc&255)|((uint16_t)mem[pc++]<<8));t=7;break;
case 0x04:bc=(uint16_t)((bc&255)|((uint16_t)(uint8_t)((bc>>8)+1)<<8));z=!(bc>>8);t=4;break;
case 0xD9:{uint16_t tmp=bc;bc=bc2;bc2=tmp;tmp=de;de=de2;de2=tmp;tmp=hl;hl=hl2;hl2=tmp;t=4;break;}
case 0xC3:pc=imm16();assert(pc==0x1200||pc==0x1100);done=1;t=10;break;
default:fprintf(stderr,"Unknown opcode %02x at %04x\n",op,pc-1);assert(0);
}cpu_ticks+=4*t;}
static void advance(uint64_t ticks){while(!done&&cpu_ticks<ticks)step();}
static void probe(loader_mode_t mode,int sense_inv,int write_inv,unsigned hold,int high){
 uint8_t header[128]={1};header[19]=1;header[21]=header[23]=high ? 0x11:0x12;
 mzf_loader_reset();mzf_loader_set_source_io(seek_src,read_src);
 assert(mzf_loader_prepare(FILE_FORMAT_MZF,mode,header,384,128));
 memset(mem,0,sizeof(mem));
 uint16_t base=mode==LOADER_MODE_UL_MZ800 ? 0x1110:mode==LOADER_MODE_UL_MZ700 ? (high ? 0xC000:0x1108):(high ? 0xC000:0x1100);
 uint16_t n=mzf_loader_build_loader(mem+base,256);assert(n>0 && n<=256);
 if(mode==LOADER_MODE_UL_MZ800 && high){assert(n==96);memcpy(mem+0xC000,mem+0x1110+13,83);base=0xC000;n=83;}
 /* Execute generic UL from entry. For header-only modes skip only the ROM/
  * display prolog; execute generated count/address prefix and the receiver. */
 unsigned off=0;
 if(mode!=LOADER_MODE_UL){
  for(unsigned i=0;i+3<n;++i)if(mem[base+i]==0x01&&mem[base+i+1]==0&&mem[base+i+2]==1&&
      mem[base+i+3]==((high || mode==LOADER_MODE_UL_MZ700) ? 0xD9:0x21)){off=i;break;}
  assert(off);
 }
 pc=(uint16_t)(base+off);bc=de=hl=ix=bc2=de2=hl2=0;sp=0x10e0;a=0;z=done=0;copied=0;
 load_dest=high ? 0x1100:0x1200;sinv=sense_inv;winv=write_inv;pc1=0;
 cpu_ticks=mode==LOADER_MODE_UL ? 0:1400000;
 uint64_t begin_at=0;
 if(mode==LOADER_MODE_UL){
  /* Model receiver priming by the preceding normal-loader READ waveform,
   * not an added firmware delay. It reaches its first SENSE poll. */
  read_pin=0;sense_pin=1;begin_at=2000U*14;advance(begin_at);
 }
 assert(mzf_loader_begin(begin_at/14));read_pin=mzf_loader_bus_read();sense_pin=mzf_loader_bus_sense();
 uint64_t now=begin_at;unsigned loops=0;bool ok=true;
 while(ok&&!mzf_loader_is_finished()&&now<7000000ULL*14&&++loops<7000000){
  advance(now);uint8_t prev=sense_pin;
  ok=mzf_loader_bus_step(now/14,pc1^winv);
  if(!ok)break;
  read_pin=mzf_loader_bus_read();advance(now);
  sense_pin=mzf_loader_bus_sense();now+=prev!=sense_pin ? hold*14:14;advance(now);
 }
 if(ok&&mzf_loader_is_finished()){read_pin=0;sense_pin=1;advance(now+200U*14);}
 int match=!memcmp(mem+load_dest,data,256);
 if(sinv&&winv){assert(ok&&mzf_loader_is_finished()&&done&&copied==256&&match&&sp==0x10e0);}
 else assert(!match||!mzf_loader_is_finished());
 mzf_loader_bus_debug_t debug=mzf_loader_bus_debug();
 printf("mode%d high%d sense_inv%d write_inv%d hold%u: ok%d sent%u copied%u match%d pc%04x phase%s expected%d write%d\n",(int)mode,high,sinv,winv,hold,ok,mzf_loader_get_transferred(),copied,!memcmp(mem+(high ? 0x1100:0x1200),data,256),pc,debug.phase,debug.expected_write,pc1^winv);
}
int main(void){for(unsigned i=0;i<256;++i)data[i]=(uint8_t)(i*73+0xa5);for(int mode=LOADER_MODE_UL;mode<=LOADER_MODE_UL_MZ700;++mode)for(int si=0;si<2;++si)for(int wi=0;wi<2;++wi){probe((loader_mode_t)mode,si,wi,0,0);probe((loader_mode_t)mode,si,wi,0,1);}for(unsigned seed=0;seed<4;++seed){const uint8_t first[]={0,0x20,0x80,0xff};for(unsigned i=0;i<256;++i)data[i]=(uint8_t)(i*73+first[seed]);for(int mode=LOADER_MODE_UL;mode<=LOADER_MODE_UL_MZ700;++mode){probe((loader_mode_t)mode,1,1,0,0);probe((loader_mode_t)mode,1,1,0,1);}}puts("PASS actual generated UL/UL800/UL700 LOW/HIGH Z80 receivers: all 256 byte values, modeled PC4/PC5 and WRITE inversions; opposite polarities rejected");return 0;}
