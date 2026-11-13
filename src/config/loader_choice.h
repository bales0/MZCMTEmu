#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "play/loader_mode.h"
/* Reference order: NORMAL,AUTO,UL,UL800,UL700,MZ700,IC,TC. */
static inline unsigned loader_family(loader_mode_t mode) {
    switch(mode) {
    case LOADER_MODE_AUTO:return 1;
    case LOADER_MODE_UL:return 2;
    case LOADER_MODE_UL_MZ800:return 3;
    case LOADER_MODE_UL_MZ700:return 4;
    case LOADER_MODE_MZ700_1X:case LOADER_MODE_MZ700_3X:return 5;
    case LOADER_MODE_IC_1_1:case LOADER_MODE_IC_1_2:case LOADER_MODE_IC_1_3:case LOADER_MODE_IC_1_4:return 6;
    case LOADER_MODE_TC_1_1:case LOADER_MODE_TC_1_2:case LOADER_MODE_TC_1_3:return 7;
    default:return 0;
    }
}
static inline unsigned loader_speed(loader_mode_t mode) {
    switch(mode) {
    case LOADER_MODE_NORMAL_1_2:case LOADER_MODE_IC_1_2:case LOADER_MODE_TC_1_2:return 1;
    case LOADER_MODE_NORMAL_1_3:case LOADER_MODE_IC_1_3:case LOADER_MODE_TC_1_3:case LOADER_MODE_MZ700_3X:return 2;
    case LOADER_MODE_NORMAL_1_4:case LOADER_MODE_IC_1_4:return 3;
    default:return 0;
    }
}
static inline bool loader_speed_memory_valid(uint8_t packed) {return (((packed>>2)&3)==0 || ((packed>>2)&3)==2) && ((packed>>6)&3)<3;}
static inline int loader_speed_slot(unsigned family) {return !family ? 0:family>=5 && family<=7 ? (int)family-4:-1;}
static inline uint8_t loader_speed_remember(uint8_t packed,loader_mode_t mode) {
    int slot=loader_speed_slot(loader_family(mode));
    if(slot<0)return packed;
    unsigned shift=(unsigned)slot*2;
    return (uint8_t)((packed&~(3U<<shift))|(loader_speed(mode)<<shift));
}
static inline loader_mode_t loader_from_family(unsigned family,uint8_t packed) {
    static const loader_mode_t normal[]={LOADER_MODE_NORMAL_1_1,LOADER_MODE_NORMAL_1_2,LOADER_MODE_NORMAL_1_3,LOADER_MODE_NORMAL_1_4};
    static const loader_mode_t ic[]={LOADER_MODE_IC_1_1,LOADER_MODE_IC_1_2,LOADER_MODE_IC_1_3,LOADER_MODE_IC_1_4};
    static const loader_mode_t tc[]={LOADER_MODE_TC_1_1,LOADER_MODE_TC_1_2,LOADER_MODE_TC_1_3};
    if(!family)return normal[packed&3];
    if(family<=4)return (loader_mode_t)family;
    if(family==5)return ((packed>>2)&3)==2 ? LOADER_MODE_MZ700_3X:LOADER_MODE_MZ700_1X;
    if(family==6)return ic[(packed>>4)&3];
    return tc[((packed>>6)&3)<3 ? (packed>>6)&3:2];
}
static inline uint8_t loader_speed_cycle(uint8_t packed,loader_mode_t mode) {
    unsigned family=loader_family(mode),speed=loader_speed(mode);int slot=loader_speed_slot(family);
    if(slot<0)return packed;
    speed=family==5 ? (speed ? 0:2):family==7 ? (speed+1)%3:(speed+1)%4;
    unsigned shift=(unsigned)slot*2;
    return (uint8_t)((packed&~(3U<<shift))|(speed<<shift));
}
