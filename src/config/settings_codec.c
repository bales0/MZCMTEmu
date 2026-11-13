#include "config/settings.h"
#include "config/loader_choice.h"
#include <string.h>

static uint32_t crc32(const uint8_t *p,size_t n) {
    uint32_t crc=UINT32_MAX;
    while(n--) { crc^=*p++;
        for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1) ? 0xEDB88320U:0);
        }
    return ~crc;
}
static uint32_t read32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
    }
static void write32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(i*8));
    }
void settings_defaults(app_settings_t *out) {
    if(!out) return;
    *out=(app_settings_t){.motor_auto=true,.invert=false,.record_auto=false,.loader=LOADER_MODE_NORMAL_1_1,
        .record_format=RECORDER_FORMAT_WAV,.tap_speed=TAP_SPEED_1400,.record_rate=44100,.brightness=80,.loader_speeds=0xB0,.record_control=0};
}
bool settings_valid(const app_settings_t *s) {
    return s && (unsigned)s->loader<LOADER_MODE_COUNT && (unsigned)s->tap_speed<TAP_SPEED_COUNT &&
        (unsigned)s->record_format<=RECORDER_FORMAT_MZF && (s->record_rate==22050 || s->record_rate==44100) &&
        s->brightness>=10 && s->brightness<=100 && !(s->brightness%10) &&
        loader_speed_memory_valid(s->loader_speeds) && s->record_control<=2 && s->record_auto==(s->record_control==2);
}
bool settings_encode(const app_settings_t *s,uint8_t out[SETTINGS_BYTES]) {
    if(!out || !settings_valid(s)) return false;
    memset(out,0,SETTINGS_BYTES);
    memcpy(out,"MZC1",4);
    out[4]=2;
    out[5]=SETTINGS_BYTES;
    out[7]=(uint8_t)((s->motor_auto ? 1:0)|(s->invert ? 2:0)|(s->record_auto ? 4:0)|(s->autoname ? 8:0));
    out[8]=(uint8_t)s->loader;
    out[9]=(uint8_t)s->record_format;
    out[10]=(uint8_t)s->tap_speed;
    out[11]=s->brightness;
    write32(out+12,s->record_rate);
    out[16]=s->loader_speeds;
    out[17]=s->record_control;
    write32(out+20,crc32(out,20));
    return true;
}
bool settings_decode(const uint8_t *data,size_t size,app_settings_t *out) {
    if(!data || !out || size!=SETTINGS_BYTES || memcmp(data,"MZC1",4) ||
       (data[4]!=1 && data[4]!=2) || data[5]!=SETTINGS_BYTES || data[6] ||
       (data[7]&~15U) || read32(data+20)!=crc32(data,20)) return false;
    if(data[4]==1 ? (data[11] || read32(data+16)):(data[18] || data[19])) return false;
    app_settings_t candidate;settings_defaults(&candidate);
    candidate.motor_auto=(data[7]&1)!=0;candidate.invert=(data[7]&2)!=0;
    candidate.record_auto=(data[7]&4)!=0;candidate.autoname=(data[7]&8)!=0;
    candidate.loader=(loader_mode_t)data[8];candidate.record_format=(recorder_format_t)data[9];
    candidate.tap_speed=(tap_speed_t)data[10];candidate.record_rate=read32(data+12);
    if(data[4]==2) {
        candidate.brightness=data[11];candidate.loader_speeds=data[16];candidate.record_control=data[17];
    } else {
        /* Valid v1 values survive; only newly introduced fields get defaults. */
        candidate.record_control=candidate.record_auto ? 2:candidate.motor_auto ? 0:1;
        candidate.loader_speeds=loader_speed_remember(candidate.loader_speeds,candidate.loader);
    }
    if(!settings_valid(&candidate)) return false;
    *out=candidate;return true;
}
