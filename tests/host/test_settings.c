#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "config/settings.h"
#include "nvs.h"
#include "nvs_flash.h"
static uint8_t stored[24],staged[24];
static size_t stored_size;
static int init_error,open_error,get_error,set_error,commit_error;
static unsigned writes,commits,handles;
static bool exists;
esp_err_t nvs_flash_init(void) { return init_error;
}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out) {
 assert(!strcmp(name,"mzcmtemu"));
 if(open_error)return open_error;
 if(!exists && mode==NVS_READONLY)return ESP_ERR_NVS_NOT_FOUND;
 *out=1;
 ++handles;
 return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *value,size_t *size) {
 assert(handle==1 && handles && !strcmp(key,"settings"));
 if(get_error)return get_error;
 if(!exists)return ESP_ERR_NVS_NOT_FOUND;
 if(*size<stored_size){*size=stored_size;
 return ESP_ERR_NVS_INVALID_LENGTH;
 }
 memcpy(value,stored,stored_size);
 *size=stored_size;
 return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t size) {
 assert(handle==1 && handles && !strcmp(key,"settings") && size==24);
 ++writes;
 if(set_error)return set_error;
 memcpy(staged,value,24);
 return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
 assert(handle==1 && handles);
 ++commits;
 if(commit_error)return commit_error;
 memcpy(stored,staged,24);
 stored_size=24;
 exists=true;
 return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle==1 && handles);
--handles;
}
static void same(const app_settings_t *a,const app_settings_t *b) {
 uint8_t first[24],second[24];
 assert(settings_encode(a,first) && settings_encode(b,second));
 assert(!memcmp(first,second,24));
}
static void codec(void) {
 app_settings_t s,out;
 settings_defaults(&s);
 uint8_t bytes[24];
 assert(s.motor_auto && !s.invert && s.loader==LOADER_MODE_NORMAL_1_1 && s.record_rate==44100);
 for(int loader=0;loader<17;++loader)for(int speed=0;speed<4;++speed)for(int format=0;format<4;++format) {
  s.loader=(loader_mode_t)loader;
  s.tap_speed=(tap_speed_t)speed;
  s.record_format=(recorder_format_t)format;
  s.autoname=(loader%2)==0;
  s.motor_auto=(loader%2)!=0;
  s.invert=(speed%2)!=0;
  s.record_auto=(format%2)!=0;
  s.record_control=s.record_auto ? 2:(format==2 ? 1:0);
  s.record_rate=format ? 22050:44100;
  assert(settings_encode(&s,bytes) && settings_decode(bytes,24,&out));
  same(&s,&out);
 }
 for(unsigned i=0;i<24;++i)for(unsigned bit=0;bit<8;++bit) {
  bytes[i]^=(uint8_t)(1U<<bit);
  assert(!settings_decode(bytes,24,&out));
  bytes[i]^=(uint8_t)(1U<<bit);
 }
 assert(!settings_decode(bytes,23,&out) && !settings_decode(bytes,25,&out));
 s.loader=LOADER_MODE_COUNT;
 assert(!settings_encode(&s,bytes));
 settings_defaults(&s);
 s.record_rate=48000;
 assert(!settings_encode(&s,bytes));
 settings_defaults(&s);
 s.tap_speed=TAP_SPEED_COUNT;
 assert(!settings_valid(&s));
 assert(!settings_decode(NULL,24,&out) && !settings_encode(NULL,bytes));
 puts("PASS versioned settings codec: all modes/rates/formats, CRC bit corruption, invalid ranges and unknown schema fail closed");
}
static void storage(void) {
 app_settings_t s,out;
 settings_defaults(&s);
 assert(settings_load(&out)==0);
 same(&s,&out);
 s.loader=LOADER_MODE_UL_MZ700;
 s.tap_speed=TAP_SPEED_2500;
 s.record_format=RECORDER_FORMAT_MZF;
 s.record_rate=22050;
 s.invert=true;
 s.motor_auto=false;
 s.record_auto=true;
 s.record_control=2;
 s.autoname=true;
 assert(settings_save(&s) && writes==1 && commits==1 && !handles);
 assert(settings_load(&out)==1);
 same(&s,&out);
 assert(settings_save(&s) && writes==1 && commits==1);
 stored[8]^=1;
 assert(settings_load(&out)==-1);
 app_settings_t defaults;
 settings_defaults(&defaults);
 same(&out,&defaults);
 assert(settings_save(&s));
 init_error=-1;
 assert(settings_load(&out)==-1 && !settings_save(&s));
 init_error=0;
 open_error=-2;
 assert(settings_load(&out)==-1 && !settings_save(&s));
 open_error=0;
 get_error=-3;
 assert(settings_load(&out)==-1 && !handles);
 get_error=0;
 s.invert=false;
 set_error=-4;
 assert(!settings_save(&s) && !handles);
 set_error=0;
 commit_error=-5;
 assert(!settings_save(&s) && !handles);
 commit_error=0;
 assert(settings_save(&s) && settings_load(&out)==1);
 same(&s,&out);
 assert(!settings_save(NULL) && settings_load(NULL)==-1 && !handles);
 puts("PASS NVS restore and commit, no rewrite for unchanged settings, corrupt blob defaults, init/open/read/write/commit failure and handle cleanup");
}
static void migration(void) {
 const uint8_t legacy[24]={77,90,67,49,1,24,0,1,12,2,3,0,34,86,0,0,0,0,0,0,159,209,110,244};
 app_settings_t out;
 assert(settings_decode(legacy,24,&out));
 assert(out.loader==LOADER_MODE_NORMAL_1_3 && out.record_format==RECORDER_FORMAT_L16 && out.tap_speed==TAP_SPEED_2500 && out.record_rate==22050);
 assert(out.brightness==80 && out.record_control==0 && (out.loader_speeds&3)==2);
 uint8_t upgraded[24];
 assert(settings_encode(&out,upgraded) && upgraded[4]==2 && settings_decode(upgraded,24,&out));
 for(unsigned brightness=10;brightness<=100;brightness+=10)for(unsigned control=0;control<3;++control) {
  out.brightness=(uint8_t)brightness;
  out.record_control=(uint8_t)control;
  out.record_auto=control==2;
  assert(settings_encode(&out,upgraded));
  app_settings_t restored;
  assert(settings_decode(upgraded,24,&restored));
  same(&out,&restored);
 }
 out.brightness=0;
 assert(!settings_valid(&out));
 out.brightness=80;
 out.loader_speeds=4;
 assert(!settings_valid(&out));
 puts("PASS settings schema v1 to v2 preserves loader/TAP/record/rate and seeds new defaults; brightness, speed memory and independent RECORD control validated");
}
int main(void) { codec();
migration();
storage();
return 0;
}
