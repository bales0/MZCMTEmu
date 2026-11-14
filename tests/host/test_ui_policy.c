#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui/policy.h"
#include "audio/edge_guard.h"
int main(void){
 char path[32];
 ui_compact_path(path,sizeof(path),"/MZF",20);assert(!strcmp(path,"/MZF"));
 ui_compact_path(path,sizeof(path),"/sd/games/action/SHARP",15);assert(!strcmp(path,"../action/SHARP"));
 ui_compact_path(path,sizeof(path),"/sd/games/action/SHARP",12);assert(!strcmp(path,"../SHARP"));
 ui_compact_path(path,sizeof(path),"/sd/0123456789ABCDEF",12);assert(!strcmp(path,"../789ABCDEF"));
 ui_compact_path(path,5,"/sd/games/SHARP",20);assert(!strcmp(path,"../P"));
 ui_compact_path(path,sizeof(path),"/sd",2);assert(!strcmp(path,".."));
 ui_compact_path(path,sizeof(path),"/sd",0);assert(!path[0]);
 ui_compact_path(path,sizeof(path),NULL,12);assert(!path[0]);
 strcpy(path,"unchanged");ui_compact_path(path,0,"/sd/games",4);assert(!strcmp(path,"unchanged"));
 puts("PASS path ../ compaction: component boundaries, long leaf suffix, narrow fields, empty input and capacity limits");
 ui_headroom_t h={0};
 assert(ui_headroom_update(&h,100,0)==100);
 assert(ui_headroom_update(&h,20,1)==20);
 assert(ui_headroom_update(&h,80,499)==20);
 assert(ui_headroom_update(&h,80,501)==24);
 assert(ui_headroom_update(&h,9,502)==9 && ui_buffer_alarm(9)==UI_BUFFER_CRITICAL);
 assert(ui_headroom_update(&h,0,503)==0 && ui_buffer_alarm(0)==UI_BUFFER_BAD);
 assert(ui_buffer_alarm(24)==UI_BUFFER_LOW && ui_buffer_alarm(25)==UI_BUFFER_OK);
 ui_marquee_t m={0};
 assert(!ui_marquee_offset(&m,"ABCDEFGHIJKLMNOPQRSTUVWXYZ",10,100000,1));
 assert(!ui_marquee_offset(&m,"ABCDEFGHIJKLMNOPQRSTUVWXYZ",10,100699,1));
 assert(ui_marquee_offset(&m,"ABCDEFGHIJKLMNOPQRSTUVWXYZ",10,100880,1)==1);
 assert(!ui_marquee_offset(&m,"ABCDEFGHIJKLMNOPQRSTUVWXYZ",10,101000,2));
 assert(!ui_marquee_offset(&m,"NEW NAME ABCDEFGHIJK",10,102000,2));
 edge_guard_status_t s=edge_guard_compare(0,0,60001,60001,60001);
 assert(!s.confirmed && !s.suspected && !s.unreliable);
 for(unsigned lost=1;lost<=2;++lost){s=edge_guard_compare(0,0,60001,60001,60001-lost);
 assert(s.confirmed);
 }
 s=edge_guard_compare(0,2,100,103,100);
 assert(!s.confirmed && s.suspected && s.lower==98 && s.upper==103);
 s=edge_guard_compare(0,0,10,10,11);
 assert(s.confirmed);
 // duplicate/invalid ISR count
 s=edge_guard_compare(0,0,0x20000000,0x20000000,0);
 assert(s.unreliable);
 s=edge_guard_compare(0,100,1000,1200,1000);
 assert(s.unreliable);
 s=edge_guard_compare(5,4,10,10,6);
 assert(s.unreliable);
 puts("PASS RX immediate drop/recovery/actual critical thresholds; marquee starts on selection/context; PCNT epoch brackets, odd/even loss, overflow and pending-ISR ambiguity");
 return 0;
}
