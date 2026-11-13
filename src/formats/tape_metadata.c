/* SPDX-License-Identifier: GPL-3.0-only */
#include "formats/tape_metadata.h"
#include "record/record_autoname.h"
static file_format_t source_format;
static bool held;
static uint8_t held_level;
static uint64_t held_us;
static void flush(void){
 if(!held)return;
 uint64_t units=(held_us+8)/16;
 if(!units || units>1024)record_autoname_break_signal();
 else record_autoname_feed_level_interval((uint16_t)units,held_level);
 held=false;
 held_us=0;
}
void tape_metadata_begin(file_format_t f){source_format=f;
held=false;
held_us=0;
record_autoname_begin(true,FILE_FORMAT_L16,0);
}
void tape_metadata_feed(uint8_t level,uint32_t us,bool synthetic){
 if(synthetic){flush();
 return;
 }
 level=level!=0;
 if(held && held_level!=level)flush();
 if(!held){held=true;
 held_level=level;
 }
 held_us+=us;
 if(held_us>32768){record_autoname_break_signal();
 held_us=32768;
 }
}
void tape_metadata_end(void){flush();record_autoname_end_metadata();
}
void tape_metadata_snapshot(program_metadata_t *m){record_autoname_metadata(m);
m->format=source_format;
}
