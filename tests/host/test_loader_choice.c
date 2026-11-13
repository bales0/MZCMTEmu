#include <assert.h>
#include <stdio.h>
#include "config/loader_choice.h"
int main(void) {
 for(unsigned mode=0;mode<LOADER_MODE_COUNT;++mode) {
  uint8_t packed=loader_speed_remember(0xB0,(loader_mode_t)mode);
  assert(loader_speed_memory_valid(packed));assert(loader_from_family(loader_family((loader_mode_t)mode),packed)==(loader_mode_t)mode);
  for(unsigned family=0;family<8;++family) {loader_mode_t restored=loader_from_family(family,packed);assert(loader_family(restored)==family);}
 }
 uint8_t packed=0xB0;packed=loader_speed_remember(packed,LOADER_MODE_NORMAL_1_3);packed=loader_speed_remember(packed,LOADER_MODE_TC_1_2);
 packed=loader_speed_remember(packed,LOADER_MODE_MZ700_3X);assert(loader_from_family(0,packed)==LOADER_MODE_NORMAL_1_3);
 assert(loader_from_family(6,packed)==LOADER_MODE_IC_1_4 && loader_from_family(7,packed)==LOADER_MODE_TC_1_2 && loader_from_family(5,packed)==LOADER_MODE_MZ700_3X);
 for(unsigned family=0;family<8;++family)for(unsigned step=0;step<20;++step) {loader_mode_t mode=loader_from_family(family,packed);packed=loader_speed_cycle(packed,mode);assert(loader_speed_memory_valid(packed));}
 assert(!loader_speed_memory_valid(4) && !loader_speed_memory_valid(12) && !loader_speed_memory_valid(192));
 puts("PASS loader family/speed selector: all 17 modes, per-family remembered speeds, MZ700 1/3 and TC 1/2/3 constraints");return 0;
}
