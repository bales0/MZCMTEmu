#include "formats/file_format.h"
#include "storage/fs.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "drivers/i2c.h"
#include "drivers/oled.h"
#include "drivers/sdcard.h"
#include "drivers/buttons.h"
#include "ui/ui.h"
#include "audio/player.h"
#include "audio/recorder.h"
#include "audio/mzf_writer.h"
#include "record/record_autoname.h"
#include "audio/wav_reader.h"
#include "audio/edge_stream.h"
#include "file_list.h"
#include "audio/raw_out.h"
#include "formats/mz_tape_profiles.h"
#include "config/settings.h"
#include "config/loader_choice.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "app/route_switch.h"
#include "app/cmt_bus_policy.h"
#include "ui/view.h"
#include "ui/policy.h"
#include "esp_timer.h"

static ui_model_t ui_model = {
    .line = {
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        ""
    }
};

#define UI_TEXT_WIDTH 21
#define UI_BUFFER_SIZE (UI_TEXT_WIDTH + 1)
#define UI_WORK_BUFFER_SIZE 48
#define UI_SCROLL_HOLD_MS 700
#define UI_SCROLL_STEP_MS 180
#define UI_SCROLL_GAP_CHARS 3

static char ui_header[UI_BUFFER_SIZE];
static char ui_path[UI_BUFFER_SIZE];

// playback screen buffers

// record screen buffers

// menu screen buffers
static char ui_menu_item0[UI_BUFFER_SIZE];
static char ui_menu_item1[UI_BUFFER_SIZE];
static char ui_menu_item2[UI_BUFFER_SIZE];
static char ui_menu_item3[UI_BUFFER_SIZE];
static char ui_menu_item4[UI_BUFFER_SIZE];
static char ui_menu_item5[UI_BUFFER_SIZE];
static char ui_menu_item6[UI_BUFFER_SIZE];
static char ui_menu_item7[UI_BUFFER_SIZE],ui_menu_item8[UI_BUFFER_SIZE],ui_menu_item9[UI_BUFFER_SIZE];
static char ui_menu_item10[UI_BUFFER_SIZE],ui_menu_item11[UI_BUFFER_SIZE];

// no-sd screen buffers
static char ui_no_sd_line0[UI_BUFFER_SIZE];
static char ui_no_sd_line1[UI_BUFFER_SIZE];



#define APP_TASK_DELAY_MS 5
#define WRAP_REPEAT_DELAY_MS 320
#define READ_GPIO GPIO_NUM_4
#define MOTOR_GPIO GPIO_NUM_6
#define SENSE_GPIO GPIO_NUM_7
#define MONITOR_GPIO GPIO_NUM_41
#define SWITCH_GPIO GPIO_NUM_21
#define SD_DETECT_GPIO GPIO_NUM_47
#define RECORDINGS_DIR "/sd/RECORDINGS"

// SD detect: socket detect pin against GND
// LOW  = card inserted
// HIGH = no card
#define SD_DETECT_ACTIVE_LEVEL 0
#define SD_DETECT_DEBOUNCE_COUNT 6
#define SD_MOUNT_RETRY_MS 500

// runtime MENU settings
static bool s_setting_motor_auto = true;
static bool s_setting_signal_invert = false;
static bool s_setting_play_ultrafast = false;
static uint32_t s_setting_rec_rate = 44100;
static recorder_format_t s_setting_rec_format = RECORDER_FORMAT_WAV;
static bool s_setting_rec_auto_signal = false;
static uint8_t s_setting_rec_control,s_setting_brightness=80,s_loader_speeds=0xB0;
static unsigned s_menu_info_page;

// MENU state
typedef enum {
    MENU_ITEM_MODE = 0,
    MENU_ITEM_SIGNAL_INVERT,
    MENU_ITEM_PLAY_ULTRAFAST,
    MENU_ITEM_REC_FORMAT,
    MENU_ITEM_REC_AUTO_SIGNAL,
    MENU_ITEM_TAP_SPEED,
    MENU_ITEM_AUTONAME,
    MENU_ITEM_LOADER_SPEED,
    MENU_ITEM_CMT_SOURCE,
    MENU_ITEM_BRIGHTNESS,
    MENU_ITEM_ABOUT,
    MENU_ITEM_KEYMAP,
    MENU_ITEM_COUNT
} menu_item_t;

static bool s_menu_active = false;
static bool s_settings_error;
static int s_menu_selected = 0;
static bool s_menu_pre_wrap_next = false;
static bool s_menu_pre_wrap_prev = false;




// true = uživatel zmáčkl PLAY při MOTOR LOW a čekáme na MOTOR HIGH.
static bool play_wait_motor = false;
static bool play_motor_override = false;
static TickType_t play_wait_motor_armed_tick = 0;

// zámek PLAY, aby se jeden fyzický stisk / repeat nerozpadl na víc akcí
// uvolní se až při BTN_NONE
static bool play_key_lock = false;

// preview metadata pro waiting playback screen
static bool s_play_preview_valid = false;
static char s_play_preview_path[1024];
static uint32_t s_play_preview_total_seconds = 0;
static uint32_t s_play_preview_sample_rate = 0;
static bool s_file_pre_wrap_next = false;
static bool s_file_pre_wrap_prev = false;

// SD media/runtime state
static bool s_sd_present_stable = false;
static bool s_sd_present_last_raw = false;
static int s_sd_debounce_count = 0;
static bool s_sd_ready = false;
static TickType_t s_sd_next_mount_retry = 0;

// jen příprava logiky pro SWITCH:
// false = interní režim
// true  = EXT režim (v budoucnu při SWITCH LOW)
static bool switch_ext_mode = false;

static const char *basename_ptr(const char *path);

static bool player_screen_active(player_state_t st)
{
    return play_wait_motor ||
           st == PLAYER_STATE_PLAYING ||
           st == PLAYER_STATE_PAUSED ||
           st == PLAYER_STATE_EOF ||
           st == PLAYER_STATE_UNDERRUN ||
           st == PLAYER_STATE_ERROR || st == PLAYER_STATE_READY;
}



static bool motor_signal_is_high(void)
{
    return (gpio_get_level(MOTOR_GPIO) != 0);
}

static bool transport_motor_is_high(void)
{
    return !s_setting_motor_auto || motor_signal_is_high();
}

static bool record_motor_is_high(void)
{
    return s_setting_rec_control!=0 || motor_signal_is_high();
}

static bool switch_signal_is_high(void)
{
    return (gpio_get_level(SWITCH_GPIO) != 0);
}

static bool sd_detect_card_inserted(void)
{
    return (gpio_get_level(SD_DETECT_GPIO) == SD_DETECT_ACTIVE_LEVEL);
}

static cmt_bus_outputs_t bus_outputs(void)
{
    return cmt_bus_outputs(switch_ext_mode,player_is_ultrafast_active(),play_wait_motor,
        player_get_state(),recorder_get_state(),player_monitor_active());
}
static void service_sense_output(void)
{
    cmt_bus_outputs_t out=bus_outputs();
    if(!out.ul_owns_sense)gpio_set_level(SENSE_GPIO,out.sense_high);
}
static void service_monitor_output(void)
{
    gpio_set_level(MONITOR_GPIO,bus_outputs().monitor_enabled);
}

static void ui_show_overlay(const char *text,uint32_t duration_ms)
{
    unsigned priority=strstr(text,"ERROR") || strstr(text,"LOSS") || strstr(text,"OVERRUN") ? 3:
        strstr(text,"EXTERNAL") || strstr(text,"NO SD") ? 2:1;
    ui_overlay_show(text,duration_ms,priority);
}
static void ui_clear_overlay(void){ui_overlay_clear(false);}

static void play_wait_motor_clear(void)
{
    play_wait_motor = false;
    play_motor_override = false;
    play_wait_motor_armed_tick = 0;
}

static void play_wait_motor_arm(void)
{
    play_wait_motor = true;
    play_motor_override = false;
    play_wait_motor_armed_tick = xTaskGetTickCount() + pdMS_TO_TICKS(200);
}

static void clear_play_preview(void)
{
    s_play_preview_valid = false;
    s_play_preview_path[0] = '\0';
    s_play_preview_total_seconds = 0;
    s_play_preview_sample_rate = 0;
}

static void build_play_preview_for_selected(void)
{
    const char *path = file_list_get_path();
    wav_info_t info;

    clear_play_preview();

    if (path == NULL || path[0] == '\0') {
        return;
    }

    if (!wav_reader_probe(path, &info)) {
        return;
    }

    snprintf(s_play_preview_path, sizeof(s_play_preview_path), "%s", path);
    s_play_preview_sample_rate = info.sample_rate;

    if (info.byte_rate != 0) {
        s_play_preview_total_seconds = info.data_size / info.byte_rate;
    } else {
        s_play_preview_total_seconds = 0;
    }

    s_play_preview_valid = true;
}

static size_t ui_get_scroll_offset(const char *text,size_t visible_chars)
{
    static ui_marquee_t scrolling;
    if(!text)return 0;
    return ui_marquee_offset(&scrolling,text,visible_chars,
        (uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS),(uint32_t)file_list_get_selected_index());
}

static void ui_copy_scroll_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    size_t max_len = dst_size - 1;
    size_t src_len = strlen(src);

    if (src_len <= max_len) {
        memcpy(dst, src, src_len + 1);
        return;
    }

    size_t start = ui_get_scroll_offset(src, max_len);

    size_t i = 0;
    while (src[start + i] != '\0' && i < max_len) {
        dst[i] = src[start + i];
        i++;
    }
    dst[i] = '\0';
}

static void ui_copy_path_text(char *dst, size_t dst_size, const char *path)
{
    const char *display_path = path;
    size_t len;
    size_t max_len;

    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (display_path == NULL || display_path[0] == '\0') {
        snprintf(dst, dst_size, "/");
        return;
    }

    if (strncmp(display_path, "/sd", 3) == 0 &&
        (display_path[3] == '\0' || display_path[3] == '/')) {
        display_path += 3;
        if (display_path[0] == '\0') {
            display_path = "/";
        }
    }

    if (strcmp(display_path, "/") == 0) {
        snprintf(dst, dst_size, "/");
        return;
    }

    len = strlen(display_path);
    max_len = dst_size - 1;

    if (display_path[len - 1] == '/') {
        if (len <= max_len) {
            memcpy(dst, display_path, len + 1);
            return;
        }
    } else if ((len + 1U) <= max_len) {
        memcpy(dst, display_path, len);
        dst[len] = '/';
        dst[len + 1U] = '\0';
        return;
    }

    const char *name = basename_ptr(display_path);
    if (name[0] != '\0') {
        snprintf(dst, dst_size, "../%s/", name);
    } else {
        ui_copy_scroll_text(dst, dst_size, display_path);
    }
}



static const char *basename_ptr(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return "";
    }

    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return path;
    }

    return slash + 1;
}







// --------------------------------------------------------
// SD runtime helpers
// --------------------------------------------------------

static bool ensure_recordings_dir(void)
{
    struct stat st;

    if (cmt_stat(RECORDINGS_DIR, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;
        }

        printf("RECORDINGS path exists but is not dir\n");
        return false;
    }

    if (cmt_mkdir(RECORDINGS_DIR, 0775) == 0) {
        printf("created %s\n", RECORDINGS_DIR);
        return true;
    }

    printf("mkdir %s failed\n", RECORDINGS_DIR);
    return false;
}

static char s_sd_recover_path[1024];
static uint16_t s_sd_recover_record;
static bool sd_runtime_try_mount(void)
{
    // žádný zapisovací sdcard_test()
    sdcard_init();

    if (!sdcard_is_mounted()) {
        s_sd_ready = false;
        return false;
    }

    if (!ensure_recordings_dir()) {
        sdcard_unmount();
        s_sd_ready = false;
        return false;
    }

    if (!file_list_init("/sd")) {
        printf("browser init FAIL\n");
        sdcard_unmount();
        s_sd_ready = false;
        return false;
    }

    s_sd_ready = true;
    s_sd_next_mount_retry = 0;

    if(s_sd_recover_path[0]) {
        if(!recorder_is_active() && !switch_ext_mode) {
            if(!player_prepare_record(s_sd_recover_path,s_sd_recover_record))player_stop();
            else ui_show_overlay("SELECTOR RESTORED",1200);
        }
        s_sd_recover_path[0]=0;
    }
    printf("browser count = %d\n", file_list_count());
    printf("browser dir = %s\n", file_list_get_current_dir());
    printf("browser selected = %s\n", file_list_get_name());

    return true;
}

/* Called only after the foreground writer/transport has closed its handles. */
static void sd_runtime_request_reinit(void)
{
    player_stop();
    sdcard_unmount();
    s_sd_ready=false;
    s_sd_next_mount_retry=xTaskGetTickCount()+pdMS_TO_TICKS(SD_MOUNT_RETRY_MS);
}

static void sd_runtime_on_removed(void)
{
    play_wait_motor_clear();
    clear_play_preview();
    ui_clear_overlay();

    player_state_t pst = player_get_state();
    if((pst==PLAYER_STATE_READY || pst==PLAYER_STATE_PLAYING || pst==PLAYER_STATE_PAUSED) &&
       file_format_is_record_container(file_format_detect_from_name(player_get_current_path()))) {
        snprintf(s_sd_recover_path,sizeof(s_sd_recover_path),"%s",player_get_current_path());
        s_sd_recover_record=player_sharp_details()->record;
    }
    if (pst == PLAYER_STATE_READY || pst == PLAYER_STATE_PLAYING ||
        pst == PLAYER_STATE_PAUSED ||
        pst == PLAYER_STATE_EOF ||
        pst == PLAYER_STATE_UNDERRUN ||
        pst == PLAYER_STATE_ERROR) {
        player_stop();
    }

    recorder_media_removed();

    sdcard_unmount();

    s_sd_ready = false;
    s_sd_next_mount_retry = 0;
}

static void service_sd_detect(void)
{
    bool raw_present = sd_detect_card_inserted();

    if (raw_present != s_sd_present_last_raw) {
        s_sd_present_last_raw = raw_present;
        s_sd_debounce_count = 0;
        return;
    }

    if (raw_present != s_sd_present_stable) {
        if (s_sd_debounce_count < SD_DETECT_DEBOUNCE_COUNT) {
            s_sd_debounce_count++;
            return;
        }

        s_sd_debounce_count = 0;
        s_sd_present_stable = raw_present;

        if (!s_sd_present_stable) {
            printf("SD removed\n");
            sd_runtime_on_removed();
        } else {
            printf("SD inserted\n");

            // první pokus hned po potvrzeném insertu
            if (!sd_runtime_try_mount()) {
                s_sd_next_mount_retry =
                    xTaskGetTickCount() + pdMS_TO_TICKS(SD_MOUNT_RETRY_MS);
            }
        }
        return;
    }

    // karta je stabilně vložená, ale mount ještě není ready:
    // zkoušej periodicky znovu
    if (s_sd_present_stable && !s_sd_ready) {
        TickType_t now = xTaskGetTickCount();

        if (s_sd_next_mount_retry == 0 || now >= s_sd_next_mount_retry) {
            if (!sd_runtime_try_mount()) {
                s_sd_next_mount_retry =
                    now + pdMS_TO_TICKS(SD_MOUNT_RETRY_MS);
            }
        }
    }
}

// --------------------------------------------------------
// MENU helpers
// --------------------------------------------------------

static bool menu_is_active(void)
{
    return s_menu_active;
}

static void menu_enter(void)
{
    s_menu_active = true;
    s_menu_info_page=0;
    s_menu_selected = 0;
    s_menu_pre_wrap_next = false;
    s_menu_pre_wrap_prev = false;
}

static void menu_exit(void)
{
    if(s_menu_info_page) {s_menu_info_page=0;return;}
    app_settings_t settings={.motor_auto=s_setting_motor_auto,.invert=raw_out_get_read_invert(),
        .record_auto=recorder_get_auto_signal(),.autoname=recorder_get_autoname(),.loader=player_get_loader_mode(),
        .record_format=recorder_get_format(),.tap_speed=player_get_tap_speed(),.record_rate=s_setting_rec_rate,
        .brightness=s_setting_brightness,.loader_speeds=s_loader_speeds,.record_control=s_setting_rec_control};
    s_settings_error=!settings_save(&settings);
    if(s_settings_error) ui_show_overlay("SETTINGS ERROR",2000);
    s_menu_active = false;
    s_menu_pre_wrap_next = false;
    s_menu_pre_wrap_prev = false;
}

static void menu_next(void)
{
    if(s_menu_info_page) {s_menu_info_page=s_menu_info_page==5 ? 1:s_menu_info_page+1;return;}
    s_menu_selected++;
    if (s_menu_selected >= MENU_ITEM_COUNT) {
        s_menu_selected = 0;
    }
}

static void menu_prev(void)
{
    if(s_menu_info_page) {s_menu_info_page=s_menu_info_page==1 ? 5:s_menu_info_page-1;return;}
    s_menu_selected--;
    if (s_menu_selected < 0) {
        s_menu_selected = MENU_ITEM_COUNT - 1;
    }
}

static const char *rec_format_to_text(recorder_format_t format)
{
    switch (format) {
        case RECORDER_FORMAT_LEP: return "LEP";
        case RECORDER_FORMAT_L16: return "L16";
        case RECORDER_FORMAT_MZF: return "MZF";
        case RECORDER_FORMAT_WAV:
        default:
            if (s_setting_rec_rate == 22050U) {
                return "WAV 22kHz";
            }
            return "WAV 44kHz";
    }
}

static void menu_activate_selected(void)
{
    if(s_menu_info_page)return;
    switch ((menu_item_t)s_menu_selected) {
        case MENU_ITEM_MODE:
            s_setting_motor_auto = !s_setting_motor_auto;
            if (!s_setting_motor_auto) {
                play_wait_motor_clear();
                ui_clear_overlay();
            }
            break;

        case MENU_ITEM_SIGNAL_INVERT:
            s_setting_signal_invert = !s_setting_signal_invert;
            raw_out_set_read_invert(s_setting_signal_invert);
            break;

        case MENU_ITEM_PLAY_ULTRAFAST:
            s_loader_speeds=loader_speed_remember(s_loader_speeds,player_get_loader_mode());
            (void)player_set_loader_mode(loader_from_family((loader_family(player_get_loader_mode())+1)%8,s_loader_speeds));
            s_setting_play_ultrafast = player_get_ultrafast_enabled();
            break;

        case MENU_ITEM_REC_FORMAT:
            if (s_setting_rec_format == RECORDER_FORMAT_WAV &&
                s_setting_rec_rate == 44100U) {
                s_setting_rec_rate = 22050U;
                recorder_set_sample_rate(s_setting_rec_rate);
            } else if (s_setting_rec_format == RECORDER_FORMAT_WAV &&
                       s_setting_rec_rate == 22050U) {
                s_setting_rec_format = RECORDER_FORMAT_LEP;
                recorder_set_format(s_setting_rec_format);
                s_setting_rec_rate = 44100U;
                recorder_set_sample_rate(s_setting_rec_rate);
            } else if (s_setting_rec_format == RECORDER_FORMAT_WAV) {
                s_setting_rec_format = RECORDER_FORMAT_LEP;
                recorder_set_format(s_setting_rec_format);
            } else if (s_setting_rec_format == RECORDER_FORMAT_LEP) {
                s_setting_rec_format = RECORDER_FORMAT_L16;
                recorder_set_format(s_setting_rec_format);
            } else if (s_setting_rec_format == RECORDER_FORMAT_L16) {
                s_setting_rec_format = RECORDER_FORMAT_MZF;
                recorder_set_format(s_setting_rec_format);
            } else {
                s_setting_rec_format = RECORDER_FORMAT_WAV;
                s_setting_rec_rate = 44100U;
                recorder_set_format(s_setting_rec_format);
                recorder_set_sample_rate(s_setting_rec_rate);
            }
            break;

        case MENU_ITEM_LOADER_SPEED:
            s_loader_speeds=loader_speed_cycle(s_loader_speeds,player_get_loader_mode());
            (void)player_set_loader_mode(loader_from_family(loader_family(player_get_loader_mode()),s_loader_speeds));
            break;
        case MENU_ITEM_CMT_SOURCE:
            ui_show_overlay("USE SWITCH S1",1500);
            break;
        case MENU_ITEM_BRIGHTNESS:
            s_setting_brightness=s_setting_brightness==100 ? 10:s_setting_brightness+10;
            if(!oled_set_brightness(s_setting_brightness))ui_show_overlay("OLED ERROR",1500);
            break;
        case MENU_ITEM_ABOUT: s_menu_info_page=1;break;
        case MENU_ITEM_KEYMAP: s_menu_info_page=2;break;
        case MENU_ITEM_AUTONAME:
            recorder_set_autoname(!recorder_get_autoname());
            break;

        case MENU_ITEM_TAP_SPEED:
            (void)player_set_tap_speed((tap_speed_t)((player_get_tap_speed()+1U)%TAP_SPEED_COUNT));
            break;

        case MENU_ITEM_REC_AUTO_SIGNAL:
            s_setting_rec_control=(uint8_t)((s_setting_rec_control+1)%3);
            s_setting_rec_auto_signal=s_setting_rec_control==2;
            recorder_set_auto_signal(s_setting_rec_auto_signal);
            break;

        default:
            break;
    }
}

static void ui_build_info_model(void)
{
    ui_use_text_view();
    static const char *const keys[4][8]={
      {"BROWSER KEYS","PLAY: OPEN / START","STOP: PARENT","HOLD STOP: MENU","NEXT/PREV: SELECT","REC: START RECORD","NEXT/PREV: MORE","STOP: BACK"},
      {"PLAY KEYS","PLAY: USER PAUSE","PLAY: RESUME","STOP: END / SELECT","HOLD STOP: END","MOTOR: AUTO PAUSE","NEXT/PREV: MORE","STOP: BACK"},
      {"RECORD KEYS","REC: START / ARM","PLAY: PAUSE/RESUME","STOP: SAVE","HOLD STOP: DELETE","AUTO: IDLE STOP 5s","NEXT/PREV: MORE","STOP: BACK"},
      {"IMAGE SELECTOR KEYS","NEXT/PREV: RECORD","REC: RECORD PROFILE","PLAY: START RECORD","STOP: BACK TO BROWSER","MZT: AUTO CONTINUE","NEXT/PREV: MORE","STOP: BACK"}};
    if(s_menu_info_page>=2) {
        for(unsigned row=0;row<8;++row)ui_model.line[row]=keys[s_menu_info_page-2][row];
        return;
    }
    snprintf(ui_header,sizeof(ui_header),"MZCMTEmu ESP32-S3");
    snprintf(ui_menu_item0,sizeof(ui_menu_item0),"IDF %.16s",esp_get_idf_version());
    snprintf(ui_menu_item1,sizeof(ui_menu_item1),"INT FREE %uK",(unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024));
    snprintf(ui_menu_item2,sizeof(ui_menu_item2),"INT MIN %uK",(unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)/1024));
    snprintf(ui_menu_item3,sizeof(ui_menu_item3),"PSRAM FREE %uK",(unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024));
    ui_model.line[0]=ui_header;ui_model.line[1]="PORT / REF 2.0.3";
    ui_model.line[2]=ui_menu_item0;ui_model.line[3]=ui_menu_item1;ui_model.line[4]=ui_menu_item2;ui_model.line[5]=ui_menu_item3;
#ifdef CONFIG_MZ_CAPTURE_GPIO_FALLBACK
    ui_model.line[6]="RX GPIO FALLBACK";
#else
#if CONFIG_MZ_RX_PCNT_GUARD
    ui_model.line[6]="RX MCPWM + PCNT";
#else
    ui_model.line[6]="RX MCPWM CAPTURE";
#endif
#endif
    ui_model.line[7]="STOP BACK / NEXT MORE";
}
static void ui_build_menu_model(void)
{
    ui_use_text_view();
    if(s_menu_info_page) {ui_build_info_model();return;}
    snprintf(ui_header, sizeof(ui_header), "MENU");

    snprintf(ui_menu_item0, sizeof(ui_menu_item0),
             "%cMODE:%s",
             (s_menu_selected == MENU_ITEM_MODE) ? '}' : ' ',
             s_setting_motor_auto ? "AUTO" : "MANUAL");

    snprintf(ui_menu_item1, sizeof(ui_menu_item1),
             "%cSIGNAL INVERT:%s",
             (s_menu_selected == MENU_ITEM_SIGNAL_INVERT) ? '}' : ' ',
             raw_out_get_read_invert() ? "ON" : "OFF");

    snprintf(ui_menu_item2, sizeof(ui_menu_item2),
             "%cMZF:%s",
             (s_menu_selected == MENU_ITEM_PLAY_ULTRAFAST) ? '}' : ' ',
             loader_mode_name(player_get_loader_mode()));

    snprintf(ui_menu_item3, sizeof(ui_menu_item3),
             "%cREC FORMAT:%s",
             (s_menu_selected == MENU_ITEM_REC_FORMAT) ? '}' : ' ',
             rec_format_to_text(recorder_get_format()));

    snprintf(ui_menu_item4, sizeof(ui_menu_item4),
             "%cREC CTRL:%s",
             (s_menu_selected == MENU_ITEM_REC_AUTO_SIGNAL) ? '}' : ' ',
             s_setting_rec_control==2 ? "AUTO":s_setting_rec_control==1 ? "MANUAL":"MOTOR");

    snprintf(ui_menu_item5, sizeof(ui_menu_item5), "%cTAP SPEED:%u",
             (s_menu_selected == MENU_ITEM_TAP_SPEED) ? '}' : ' ',
             (unsigned)tap_stream_speed_baud(player_get_tap_speed()));

    snprintf(ui_menu_item6,sizeof(ui_menu_item6),"%cAUTONAME:%s",
             s_menu_selected==MENU_ITEM_AUTONAME ? '}':' ',recorder_get_autoname() ? "ON":"OFF");
    unsigned family=loader_family(player_get_loader_mode());
    snprintf(ui_menu_item7,sizeof(ui_menu_item7),"%cSPEED:%s",s_menu_selected==MENU_ITEM_LOADER_SPEED ? '}':' ',
             loader_speed_slot(family)<0 ? "--":loader_speed(player_get_loader_mode())==0 ? "1:1":loader_speed(player_get_loader_mode())==1 ? "1:2":loader_speed(player_get_loader_mode())==2 ? "1:3":"1:4");
    snprintf(ui_menu_item8,sizeof(ui_menu_item8),"%cCMT:%s S1",s_menu_selected==MENU_ITEM_CMT_SOURCE ? '}':' ',switch_ext_mode ? "EXTERNAL":"INTERNAL");
    snprintf(ui_menu_item9,sizeof(ui_menu_item9),"%cOLED BRIGHT:%u%%",s_menu_selected==MENU_ITEM_BRIGHTNESS ? '}':' ',s_setting_brightness);
    snprintf(ui_menu_item10,sizeof(ui_menu_item10),"%cABOUT / MEMORY",s_menu_selected==MENU_ITEM_ABOUT ? '}':' ');
    snprintf(ui_menu_item11,sizeof(ui_menu_item11),"%cKEYMAP",s_menu_selected==MENU_ITEM_KEYMAP ? '}':' ');
    const char *rows[]={ui_menu_item0,ui_menu_item1,ui_menu_item2,ui_menu_item3,
                        ui_menu_item4,ui_menu_item5,ui_menu_item6,ui_menu_item7,ui_menu_item8,ui_menu_item9,ui_menu_item10,ui_menu_item11};
    int first=s_menu_selected>5 ? s_menu_selected-5:0;
    ui_model.line[0] = ui_header;
    ui_model.line[1] = s_settings_error ? "SETTINGS ERROR" : "";
    for(unsigned row=0;row<6;++row) ui_model.line[row+2]=rows[first+row];
}

// --------------------------------------------------------
// NO SD screen
// --------------------------------------------------------

static bool ui_should_show_no_sd_screen(void)
{
    return !s_sd_ready;
}

static void ui_build_no_sd_model(void)
{
    ui_use_text_view();
    snprintf(ui_no_sd_line0, sizeof(ui_no_sd_line0), "NO SD CARD");
    snprintf(ui_no_sd_line1, sizeof(ui_no_sd_line1), "INSERT CARD");

    ui_model.line[0] = ui_no_sd_line0;
    ui_model.line[1] = "";
    ui_model.line[2] = ui_no_sd_line1;
    ui_model.line[3] = "";
    ui_model.line[4] = "";
    ui_model.line[5] = "";
    ui_model.line[6] = "";
    ui_model.line[7] = "";
}

// --------------------------------------------------------
// RECORD screen via recorder.c
// --------------------------------------------------------

static bool ui_should_show_record_screen(void)
{
    return recorder_is_active();
}

static void ui_build_record_model(void)
{
    ui_transport_view_t view={0};recorder_state_t state=recorder_get_state();
    recorder_metadata(&view.program);view.filename=basename_ptr(recorder_get_path());
    view.session=(uint32_t)recorder_get_format();view.seconds=recorder_get_elapsed_seconds();view.sample_rate=recorder_get_sample_rate();
    edge_capture_status_t capture;recorder_capture_status(&capture);
    view.rx_free=capture.free_percent;view.rx_min=capture.min_free_percent;
    view.record_active=state==RECORDER_STATE_RECORDING;
    switch(state){
    case RECORDER_STATE_WAIT_MOTOR:view.status="WAIT MOTOR";break;
    case RECORDER_STATE_PAUSED_USER:view.status="PAUSE USER";break;
    case RECORDER_STATE_PAUSED_MOTOR:view.status="PAUSE MOTOR";break;
    case RECORDER_STATE_FINALIZING:view.status="SAVING";break;
    case RECORDER_STATE_FINISHED:view.status="SAVED";break;
    case RECORDER_STATE_DELETED:view.status="CANCELLED";break;
    case RECORDER_STATE_ERROR:view.status="ERROR";break;
    default:view.status=recorder_is_waiting_signal() ? "WAIT SIGNAL":"REC";break;
    }
    view.warning=recorder_capture_error();
    if(!view.warning[0] && capture.guard.suspected)view.warning="EDGE CHECK SUSPECT";
    if(!view.warning[0] && recorder_metadata_warning())view.warning="METADATA ERROR";
    if(state==RECORDER_STATE_ERROR && !view.warning[0] && recorder_get_format()==RECORDER_FORMAT_MZF)view.warning=mzf_writer_error();
    ui_set_transport_view(true,&view);snprintf(ui_header,sizeof(ui_header),"%s",view.status);ui_model.line[0]=ui_header;
}

// --------------------------------------------------------

static bool ui_should_show_playback_screen(void)
{
    return player_screen_active(player_get_state());
}

static void ui_build_playback_model(void)
{
    ui_transport_view_t view={0};player_state_t state=player_get_state();
    const char *path=play_wait_motor ? file_list_get_path():player_get_current_path();
    player_metadata(&view.program);if(play_wait_motor)program_metadata_reset(&view.program,file_format_detect_from_name(path));
    view.filename=basename_ptr(path);
    view.seconds=player_get_position_seconds();view.progress=player_get_progress_percent();
    view.sample_rate=player_get_sample_rate();view.playback_baud=player_is_tap() ? (uint16_t)player_get_baud_rate():0;
    raw_pulse_status(&view.tx);view.tx_expected=state==PLAYER_STATE_PLAYING && view.tx.running && !view.tx.paused && !player_is_ultrafast_active();
    view.session=(uint32_t)view.program.format;
    if(play_wait_motor)view.status="WAIT MOTOR";
    else if(state==PLAYER_STATE_READY)view.status="SELECT RECORD";
    else if(state==PLAYER_STATE_PAUSED)view.status=player_get_pause_reason()==PLAYER_PAUSE_MOTOR ? "PAUSE MOTOR":"PAUSE USER";
    else if(state==PLAYER_STATE_ERROR)view.status="ERROR";
    else if(state==PLAYER_STATE_UNDERRUN)view.status="UNDERRUN";
    else if(state==PLAYER_STATE_EOF)view.status="FINISHED";
    else if(player_is_ultrafast_active())view.status="UL HANDSHAKE";
    else if(!view.tx.running)view.status="BETWEEN BLOCKS";
    else view.status="PLAY";
    ui_set_transport_view(false,&view);snprintf(ui_header,sizeof(ui_header),"%s",view.status);ui_model.line[0]=ui_header;
}

static void ui_build_browser_model(void)
{
    const char *rows[6];int selected=-1;unsigned directories=0;
    ui_copy_path_text(ui_path,sizeof(ui_path),file_list_get_current_dir());ui_model.line[0]=ui_path;
    for(int row=0;row<6;++row){
        rows[row]=file_list_get_visible_name(row);
        if(file_list_is_visible_selected(row))selected=row;
        if(file_list_get_visible_type(row)==FILE_ITEM_DIR || file_list_get_visible_type(row)==FILE_ITEM_PARENT)directories|=1U<<row;
    }
    ui_set_browser_view(rows,selected,directories,(unsigned)file_list_get_selected_index(),(unsigned)file_list_count());
}

static void start_selected_file(void)
{
    if(switch_ext_mode) return;
    const char *path = file_list_get_path();

    if (path == NULL) {
        ui_show_overlay("NOFILE", 1000);
        return;
    }

    player_start(path);
    service_monitor_output();
    clear_play_preview();
}

static void service_motor_pause_resume(void)
{
    if(player_is_ultrafast_active()) return;
    if (!s_setting_motor_auto) {
        play_motor_override = false;
        return;
    }

    bool motor_high = motor_signal_is_high();
    player_state_t st = player_get_state();
    player_pause_reason_t reason = player_get_pause_reason();

    if (st == PLAYER_STATE_PLAYING && !motor_high && !play_motor_override) {
        player_pause_motor();
        return;
    }

    if (st == PLAYER_STATE_PAUSED) {
        if (reason == PLAYER_PAUSE_USER) {
            return;
        }

        if (!motor_high) {
            return;
        }

        if (reason == PLAYER_PAUSE_MOTOR) {
            play_motor_override = false;
            player_resume_motor();
        }
    }

    if (st != PLAYER_STATE_PLAYING && st != PLAYER_STATE_PAUSED) {
        play_motor_override = false;
    }
}

/* Observe S1; the actual CMT route is selected by its physical contacts. */
static void service_route_switch(void)
{
    static route_switch_t route;
    route_event_t event=route_switch_update(&route,switch_signal_is_high(),(uint64_t)esp_timer_get_time());
    switch_ext_mode=route.external;
    if(event==ROUTE_EXTERNAL) {
        play_wait_motor_clear();
        player_stop();
        recorder_state_t state=recorder_get_state();
        if(state==RECORDER_STATE_FINALIZING || state==RECORDER_STATE_RECORDING || state==RECORDER_STATE_WAIT_MOTOR ||
           state==RECORDER_STATE_PAUSED_USER || state==RECORDER_STATE_PAUSED_MOTOR)
            recorder_handle_abort();
        gpio_set_level(READ_GPIO,0);gpio_set_level(SENSE_GPIO,1);gpio_set_level(MONITOR_GPIO,0);
        ui_show_overlay("EXTERNAL CMT",1200);
    }
}
static void ui_mark_external_route(void)
{
    if(!switch_ext_mode || ui_model.line[0]!=ui_header) return;
    size_t length=strlen(ui_header);
    if(length>UI_TEXT_WIDTH-6) length=UI_TEXT_WIDTH-6;
    snprintf(ui_header+length,sizeof(ui_header)-length," [EXT]");
}

/* The analysis runs with the transport idle. Keep STOP/SD removal responsive. */
static bool cancel_image_analysis(void *context)
{
    (void)context;
    static int64_t yielded_at,shown_at,external_since;
    static unsigned spinner;
    static char status[32];
    btn_event_t event=buttons_poll();
    if(event==BTN_STOP || event==BTN_STOP_LONG || gpio_get_level(GPIO_NUM_47)) return true;
    int64_t now=esp_timer_get_time();
    if(switch_signal_is_high())external_since=0;
    else if(!external_since)external_since=now;
    else if(now-external_since>=40000)return true;
    if(now-shown_at>=100000) {
        shown_at=now;snprintf(status,sizeof(status),"ANALYZING %c","|/-\\"[spinner++%4]);
        ui_use_text_view();ui_model_t model={{status,"","SD / TAPE PREFLIGHT","","STOP TO CANCEL","","",""}};ui_render(&model);
    }
    if(now-yielded_at>=20000) { yielded_at=now; vTaskDelay(1); }
    return false;
}
static void selector_move(int delta)
{
    const sharp_stream_info_t *info=player_sharp_details();
    if(!info->records) return;
    uint16_t next=delta>0 ? (info->record==info->records ? 1 : info->record+1) :
        (info->record==1 ? info->records : info->record-1);
    char path[1024]; snprintf(path,sizeof(path),"%s",player_get_current_path());
    if(!player_prepare_record(path,next)) ui_show_overlay(sharp_stream_analysis_cancelled() ? "CANCELLED":"RECORD ERROR",1000);
}

static void activate_selected_item(bool motor_high)
{
    file_item_type_t type = file_list_get_type();

    if (type == FILE_ITEM_DIR || type == FILE_ITEM_PARENT) {
        play_wait_motor_clear();
        clear_play_preview();
        ui_clear_overlay();

        if (!file_list_enter_selected()) {
            ui_show_overlay(sd_detect_card_inserted() ? file_list_last_error():"NO SD",1000);
            if(sd_detect_card_inserted() && file_list_error_requires_reinit())sd_runtime_request_reinit();
        }
        return;
    }

    if (type == FILE_ITEM_WAV || type == FILE_ITEM_TAPE) {
        if(switch_ext_mode) { ui_show_overlay("EXTERNAL CMT",1000); return; }
        if (file_format_is_record_container(file_format_detect_from_name(file_list_get_path()))) {
            if(!player_prepare_record(file_list_get_path(),1)) ui_show_overlay(sharp_stream_analysis_cancelled() ? "CANCELLED":"IMAGE ERROR",1000);
            return;
        }
        if (!motor_high) {
            if (type == FILE_ITEM_WAV) {
                build_play_preview_for_selected();
            } else {
                clear_play_preview();
            }
            play_wait_motor_arm();
            ui_show_overlay("MOTOR", 0);
        } else {
            clear_play_preview();
            play_wait_motor_clear();
            ui_clear_overlay();
            start_selected_file();
        }
        return;
    }

    ui_show_overlay("NOFILE", 1000);
}

static void service_wait_start_on_motor(void)
{
    if (switch_ext_mode || !s_setting_motor_auto) {
        return;
    }

    if (!play_wait_motor) {
        return;
    }

    if (play_wait_motor_armed_tick != 0 &&
        (int32_t)(xTaskGetTickCount() - play_wait_motor_armed_tick) < 0) {
        return;
    }

    if (motor_signal_is_high()) {
        play_wait_motor_clear();
        ui_clear_overlay();
        start_selected_file();
    }
}

static void app_task(void *arg)
{
    (void)arg;

    while (1) {
        btn_event_t e = buttons_poll();
        if(e==BTN_STOP || e==BTN_STOP_LONG)ui_overlay_clear(true);

        service_sd_detect();
        service_route_switch();

        // MENU má prioritu nad browser/playback
        if (menu_is_active()) {
            if (e == BTN_PLAY) {
                if (!play_key_lock) {
                    play_key_lock = true;
                    menu_activate_selected();
                }
            }
            else if (e == BTN_STOP) {
                menu_exit();
            }
            else if (e == BTN_STOP_LONG) {
                // v menu ignore
            }
            else if (e == BTN_NEXT) {
                bool wrapped = (s_menu_selected >= (MENU_ITEM_COUNT - 1));
                if (wrapped && buttons_last_event_was_repeat() && !s_menu_pre_wrap_next) {
                    s_menu_pre_wrap_next = true;
                    s_menu_pre_wrap_prev = false;
                    buttons_delay_repeat(BTN_NEXT, WRAP_REPEAT_DELAY_MS);
                } else {
                    s_menu_pre_wrap_next = false;
                    s_menu_pre_wrap_prev = false;
                    menu_next();
                }
            }
            else if (e == BTN_PREV) {
                bool wrapped = (s_menu_selected <= 0);
                if (wrapped && buttons_last_event_was_repeat() && !s_menu_pre_wrap_prev) {
                    s_menu_pre_wrap_prev = true;
                    s_menu_pre_wrap_next = false;
                    buttons_delay_repeat(BTN_PREV, WRAP_REPEAT_DELAY_MS);
                } else {
                    s_menu_pre_wrap_next = false;
                    s_menu_pre_wrap_prev = false;
                    menu_prev();
                }
            }
            else if (e == BTN_NONE) {
                play_key_lock = false;
            }

            service_motor_pause_resume();
            if (s_setting_rec_control==0) {
                recorder_service_motor(motor_signal_is_high());
            }
            recorder_service();
            service_wait_start_on_motor();

            service_sense_output();
            service_monitor_output();

            if (ui_should_show_no_sd_screen()) {
                ui_build_no_sd_model();
            } else {
                ui_build_menu_model();
            }
            ui_mark_external_route();
            ui_set_external(switch_ext_mode);ui_render(&ui_model);

            TickType_t delay_ticks = pdMS_TO_TICKS(APP_TASK_DELAY_MS);
            if (delay_ticks == 0) {
                delay_ticks = 1;
            }
            vTaskDelay(delay_ticks);
            continue;
        }

        if (e == BTN_PLAY) {
            bool motor_high = transport_motor_is_high();
            player_state_t st = player_get_state();
            player_pause_reason_t reason = player_get_pause_reason();

            if (!play_key_lock) {
                play_key_lock = true;

                // bez SD neobsluhuj browser/playback start
                if (!s_sd_ready) {
                    // nic
                }
                // RECORD mode má prioritu
                else if (recorder_is_active()) {
                    recorder_handle_play(record_motor_is_high());
                }
                // PLAYBACK / browser logika
                else if (st == PLAYER_STATE_READY) {
                    if(motor_high) start_selected_file();
                    else { play_wait_motor_arm(); ui_show_overlay("MOTOR",0); }
                }
                else if (st == PLAYER_STATE_PLAYING) {
                    ui_clear_overlay();
                    play_wait_motor_clear();
                    clear_play_preview();
                    player_pause_user();
                }
                else if (st == PLAYER_STATE_PAUSED && reason == PLAYER_PAUSE_USER) {
                    ui_clear_overlay();
                    play_wait_motor_clear();
                    clear_play_preview();
                    if (motor_high) {
                        player_resume_user();
                    }
                }
                else if (st == PLAYER_STATE_PAUSED && reason == PLAYER_PAUSE_MOTOR) {
                    ui_clear_overlay();
                    play_wait_motor_clear();
                    clear_play_preview();
                    if (motor_high) {
                        player_resume_motor();
                    }
                }
                else if (play_wait_motor) {
                    if (motor_high) {
                        play_wait_motor_clear();
                        ui_clear_overlay();
                        start_selected_file();
                    } else {
                        ui_show_overlay("MOTOR", 0);
                    }
                }
                else {
                    activate_selected_item(motor_high);
                }
            }
        }
        else if (e == BTN_STOP) {
            player_state_t st = player_get_state();
            play_wait_motor_clear();
            ui_clear_overlay();
            clear_play_preview();

            // RECORD screen má prioritu
            if (recorder_is_active()) {
                bool failed=recorder_get_state()==RECORDER_STATE_ERROR;
                recorder_handle_stop();
                if(s_sd_ready && recorder_get_state()!=RECORDER_STATE_FINALIZING && (failed || recorder_get_state()==RECORDER_STATE_ERROR || (!file_list_reload_current_dir() && file_list_error_requires_reinit())))
                    sd_runtime_request_reinit();
            }
            // playback screen a playback error/finished screen
            else if (st == PLAYER_STATE_READY || st == PLAYER_STATE_PLAYING ||
                     st == PLAYER_STATE_PAUSED ||
                     st == PLAYER_STATE_EOF ||
                     st == PLAYER_STATE_UNDERRUN ||
                     st == PLAYER_STATE_ERROR) {
                bool container=file_format_is_record_container(file_format_detect_from_name(player_get_current_path()));
                uint16_t record=player_sharp_details()->record;
                char path[1024]; snprintf(path,sizeof(path),"%s",player_get_current_path());
                player_stop();
                if(container && st!=PLAYER_STATE_READY && s_sd_ready)
                    (void)player_prepare_record(path,record);
            }
            else if (s_sd_ready) {
                if (!file_list_go_parent()) {
                    // v rootu nedělej nic
                }
            }
        }
        else if (e == BTN_STOP_LONG) {
            player_state_t st = player_get_state();

            if (recorder_is_active()) {
                play_wait_motor_clear();
                ui_clear_overlay();
                clear_play_preview();
                recorder_handle_abort();
                if (s_sd_ready && recorder_get_state()!=RECORDER_STATE_FINALIZING) {
                    file_list_reload_current_dir();
                }
            }
            else if (player_screen_active(st)) {
                play_wait_motor_clear();
                clear_play_preview();
                ui_clear_overlay();
                player_stop();
            }
            else {
                play_wait_motor_clear();
                clear_play_preview();
                ui_clear_overlay();

                menu_enter();
            }
        }
        else if (e == BTN_NONE) {
            play_key_lock = false;
        }
        else if (e == BTN_NEXT) {
            if(player_get_state()==PLAYER_STATE_READY && !play_wait_motor) { selector_move(1); }
            else if (s_sd_ready && !recorder_is_active()) {
                bool wrapped = (file_list_count() > 0 &&
                                file_list_get_selected_index() >= (file_list_count() - 1));
                if (wrapped && buttons_last_event_was_repeat() && !s_file_pre_wrap_next) {
                    s_file_pre_wrap_next = true;
                    s_file_pre_wrap_prev = false;
                    buttons_delay_repeat(BTN_NEXT, WRAP_REPEAT_DELAY_MS);
                } else {
                    s_file_pre_wrap_next = false;
                    s_file_pre_wrap_prev = false;
                    if (file_list_next()) {
                        clear_play_preview();
                    }
                }
            }
        }
        else if (e == BTN_PREV) {
            if(player_get_state()==PLAYER_STATE_READY && !play_wait_motor) { selector_move(-1); }
            else if (s_sd_ready && !recorder_is_active()) {
                bool wrapped = (file_list_count() > 0 &&
                                file_list_get_selected_index() <= 0);
                if (wrapped && buttons_last_event_was_repeat() && !s_file_pre_wrap_prev) {
                    s_file_pre_wrap_prev = true;
                    s_file_pre_wrap_next = false;
                    buttons_delay_repeat(BTN_PREV, WRAP_REPEAT_DELAY_MS);
                } else {
                    s_file_pre_wrap_next = false;
                    s_file_pre_wrap_prev = false;
                    if (file_list_prev()) {
                        clear_play_preview();
                    }
                }
            }
        }
        else if (e == BTN_REC) {
            player_state_t st = player_get_state();

            if (!s_sd_ready) {
                // bez SD ignoruj REC
            }
            else if(switch_ext_mode) { ui_show_overlay("EXTERNAL CMT",1000); }
            else if(st==PLAYER_STATE_READY) {
                loader_mode_t mode=(loader_mode_t)((player_sharp_details()->requested+1U)%LOADER_MODE_COUNT);
                if(!player_set_record_mode(mode)) ui_show_overlay("PROFILE ERROR",1000);
            }
            else {
                if (!recorder_is_active()) {
                    if (st == PLAYER_STATE_PLAYING ||
                        st == PLAYER_STATE_PAUSED ||
                        play_wait_motor) {
                        // Playback or a queued playback start owns the transport.
                    } else {
                        if (st == PLAYER_STATE_EOF ||
                            st == PLAYER_STATE_UNDERRUN ||
                            st == PLAYER_STATE_ERROR) {
                            play_wait_motor_clear();
                            clear_play_preview();
                            ui_clear_overlay();
                            player_stop();
                        }

                        ui_clear_overlay();
                        play_wait_motor_clear();
                        clear_play_preview();

                        recorder_handle_rec(RECORDINGS_DIR, record_motor_is_high());
                    }
                } else {
                    // REC is intentionally inert while the record workflow is active.
                }
            }
        }

        service_motor_pause_resume();
        if (s_setting_rec_control==0) {
            recorder_service_motor(motor_signal_is_high());
        }
        recorder_service();
        service_wait_start_on_motor();

        service_sense_output();
        service_monitor_output();

        if (ui_should_show_no_sd_screen()) {
            ui_build_no_sd_model();
        } else if (ui_should_show_record_screen()) {
            ui_build_record_model();
        } else if (ui_should_show_playback_screen()) {
            ui_build_playback_model();
        } else {
            ui_build_browser_model();
        }

        ui_mark_external_route();
        ui_set_external(switch_ext_mode);ui_render(&ui_model);

        TickType_t delay_ticks = pdMS_TO_TICKS(APP_TASK_DELAY_MS);
        if (delay_ticks == 0) {
            delay_ticks = 1;
        }
        vTaskDelay(delay_ticks);
    }
}

void app_main(void)
{
    // SERIAL fix
    vTaskDelay(pdMS_TO_TICKS(500));
    fflush(stdout);

    // INIT pořadí
    i2c_init();
    oled_init();
    buttons_init();
    sharp_stream_set_analysis_control(cancel_image_analysis,NULL);
    tap_stream_set_scan_control(cancel_image_analysis,NULL);
    file_list_set_scan_control(cancel_image_analysis,NULL);
    ui_init();

    gpio_config_t read_cfg = {
        .pin_bit_mask = (1ULL << READ_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_set_level(READ_GPIO, 0); // preload latch before enabling output
    gpio_config(&read_cfg);
    gpio_set_level(READ_GPIO, 0);   // default STOP/idle = LOW

    gpio_config_t motor_cfg = {
        .pin_bit_mask = (1ULL << MOTOR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&motor_cfg);

    // SWITCH = vstup s pull-up, zatím jen příprava logiky, bez akcí
    gpio_config_t switch_cfg = {
        .pin_bit_mask = (1ULL << SWITCH_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&switch_cfg);

    // SD DETECT = input s pull-up
    // LOW = karta vložena, HIGH = karta vytažena
    gpio_config_t sd_detect_cfg = {
        .pin_bit_mask = (1ULL << SD_DETECT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sd_detect_cfg);

    gpio_config_t sense_cfg = {
        .pin_bit_mask = (1ULL << SENSE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_set_level(SENSE_GPIO, 1); // preload latch before enabling output
    gpio_config(&sense_cfg);
    gpio_set_level(SENSE_GPIO, 1);   // default STOP/idle = HIGH

    gpio_config_t monitor_cfg = {
        .pin_bit_mask = (1ULL << MONITOR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_set_level(MONITOR_GPIO, 0); // preload latch before enabling output
    gpio_config(&monitor_cfg);
    gpio_set_level(MONITOR_GPIO, 0);   // default STOP/idle = monitor muted

    if(!cmt_storage_init())printf("STORAGE TASK INIT FAILED\n");

    // player / recorder init
    player_init();
    player_task_start();
    recorder_init();
    app_settings_t restored;
    s_settings_error=settings_load(&restored)<0;
    s_setting_motor_auto=restored.motor_auto;
    s_setting_signal_invert=restored.invert;
    s_setting_rec_auto_signal=restored.record_auto;
    s_setting_rec_format=restored.record_format;
    s_setting_rec_rate=restored.record_rate;
    s_setting_rec_control=restored.record_control;s_setting_brightness=restored.brightness;s_loader_speeds=restored.loader_speeds;
    if(!oled_set_brightness(s_setting_brightness)) s_settings_error=true;
    (void)player_set_loader_mode(restored.loader);
    (void)player_set_tap_speed(restored.tap_speed);
    s_setting_play_ultrafast=player_get_ultrafast_enabled();
    recorder_set_format(s_setting_rec_format);
    recorder_set_autoname(restored.autoname);
    recorder_set_sample_rate(s_setting_rec_rate);
    recorder_set_auto_signal(s_setting_rec_auto_signal);

    raw_out_set_read_invert(s_setting_signal_invert);
    s_setting_signal_invert = raw_out_get_read_invert();

    // počáteční stav SD detect
    s_sd_present_last_raw = sd_detect_card_inserted();
    s_sd_present_stable = s_sd_present_last_raw;
    s_sd_debounce_count = 0;
    s_sd_ready = false;
    s_sd_next_mount_retry = 0;

    if (s_sd_present_stable) {
        printf("SD present at boot\n");
        sd_runtime_try_mount();
    } else {
        printf("SD not present at boot\n");
    }

    // hlavní aplikační task na CPU1
    xTaskCreatePinnedToCore(
        app_task,
        "app_task",
        8192,
        NULL,
        2,
        NULL,
        1
    );

    // main task na CPU0 ukončíme
    vTaskDelete(NULL);
}
