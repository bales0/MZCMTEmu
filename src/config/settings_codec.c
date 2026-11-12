#include "config/settings.h"
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
        .record_format=RECORDER_FORMAT_WAV,.tap_speed=TAP_SPEED_1400,.record_rate=44100};
}
bool settings_valid(const app_settings_t *s) {
    return s && (unsigned)s->loader<LOADER_MODE_COUNT && (unsigned)s->tap_speed<TAP_SPEED_COUNT &&
        (unsigned)s->record_format<=RECORDER_FORMAT_L16 && (s->record_rate==22050 || s->record_rate==44100);
}
bool settings_encode(const app_settings_t *s,uint8_t out[SETTINGS_BYTES]) {
    if(!out || !settings_valid(s)) return false;
    memset(out,0,SETTINGS_BYTES);
    memcpy(out,"MZC1",4);
    out[4]=1;
    out[5]=SETTINGS_BYTES;
    out[7]=(uint8_t)((s->motor_auto ? 1:0)|(s->invert ? 2:0)|(s->record_auto ? 4:0));
    out[8]=(uint8_t)s->loader;
    out[9]=(uint8_t)s->record_format;
    out[10]=(uint8_t)s->tap_speed;
    write32(out+12,s->record_rate);
    write32(out+20,crc32(out,20));
    return true;
}
bool settings_decode(const uint8_t *data,size_t size,app_settings_t *out) {
    if(!data || !out || size!=SETTINGS_BYTES || memcmp(data,"MZC1",4) || data[4]!=1 || data[5]!=SETTINGS_BYTES ||
        data[6] || (data[7]&~7U) || data[11] || read32(data+16) || read32(data+20)!=crc32(data,20)) return false;
    app_settings_t candidate={.motor_auto=(data[7]&1)!=0,.invert=(data[7]&2)!=0,.record_auto=(data[7]&4)!=0,
        .loader=(loader_mode_t)data[8],.record_format=(recorder_format_t)data[9],
        .tap_speed=(tap_speed_t)data[10],.record_rate=read32(data+12)};
    if(!settings_valid(&candidate)) return false;
    *out=candidate;
    return true;
}
