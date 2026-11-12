#include "config/settings.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

/* Own namespace only. Init failure never erases the whole NVS partition. */
static bool initialize(void) { return nvs_flash_init()==ESP_OK;
    }
int settings_load(app_settings_t *out) {
    if(!out) return -1;
    settings_defaults(out);
    if(!initialize()) return -1;
    nvs_handle_t handle;
    esp_err_t status=nvs_open("mzcmtemu",NVS_READONLY,&handle);
    if(status==ESP_ERR_NVS_NOT_FOUND) return 0;
    if(status!=ESP_OK) return -1;
    uint8_t data[SETTINGS_BYTES];
    size_t size=sizeof(data);
    status=nvs_get_blob(handle,"settings",data,&size);
    nvs_close(handle);
    if(status==ESP_ERR_NVS_NOT_FOUND) return 0;
    return status==ESP_OK && settings_decode(data,size,out) ? 1:-1;
}
bool settings_save(const app_settings_t *settings) {
    uint8_t data[SETTINGS_BYTES],previous[SETTINGS_BYTES];
    if(!settings_encode(settings,data) || !initialize()) return false;
    nvs_handle_t handle;
    if(nvs_open("mzcmtemu",NVS_READWRITE,&handle)!=ESP_OK) return false;
    size_t size=sizeof(previous);
    bool unchanged=nvs_get_blob(handle,"settings",previous,&size)==ESP_OK && size==sizeof(data) && !memcmp(previous,data,sizeof(data));
    bool ok=unchanged || (nvs_set_blob(handle,"settings",data,sizeof(data))==ESP_OK && nvs_commit(handle)==ESP_OK);
    nvs_close(handle);
    return ok;
}
