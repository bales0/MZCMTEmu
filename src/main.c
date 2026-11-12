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
#include "audio/wav_reader.h"
#include "audio/edge_stream.h"
#include "file_list.h"
#include "audio/raw_out.h"
#include "formats/mz_tape_profiles.h"
#include "config/settings.h"
#include "app/route_switch.h"
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
static char ui_items[FILE_LIST_VIEW_ROWS][UI_BUFFER_SIZE];

// playback screen buffers
static char ui_play_file[UI_BUFFER_SIZE];
static char ui_play_bar[UI_BUFFER_SIZE];
static char ui_play_time[UI_WORK_BUFFER_SIZE];
static char ui_play_rate[UI_BUFFER_SIZE];
static char ui_play_dir[UI_BUFFER_SIZE];
static char ui_play_footer[UI_BUFFER_SIZE];

// record screen buffers
static char ui_rec_file[UI_BUFFER_SIZE];
static char ui_rec_signal[UI_BUFFER_SIZE];
static char ui_rec_buffer[UI_BUFFER_SIZE];
static char ui_rec_time[UI_WORK_BUFFER_SIZE];
static char ui_rec_rate[UI_BUFFER_SIZE];
static char ui_rec_dir[UI_BUFFER_SIZE];
static char ui_rec_footer[UI_BUFFER_SIZE];

// menu screen buffers
static char ui_menu_item0[UI_BUFFER_SIZE];
static char ui_menu_item1[UI_BUFFER_SIZE];
static char ui_menu_item2[UI_BUFFER_SIZE];
static char ui_menu_item3[UI_BUFFER_SIZE];
static char ui_menu_item4[UI_BUFFER_SIZE];
static char ui_menu_item5[UI_BUFFER_SIZE];

// no-sd screen buffers
static char ui_no_sd_line0[UI_BUFFER_SIZE];
static char ui_no_sd_line1[UI_BUFFER_SIZE];

static void format_pause_header(char *dst, size_t dst_size, const char *text, char source)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    snprintf(dst, dst_size, "%-*s%c", UI_TEXT_WIDTH - 1, text, source);
}

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

// MENU state
typedef enum {
    MENU_ITEM_MODE = 0,
    MENU_ITEM_SIGNAL_INVERT,
    MENU_ITEM_PLAY_ULTRAFAST,
    MENU_ITEM_REC_FORMAT,
    MENU_ITEM_REC_AUTO_SIGNAL,
    MENU_ITEM_TAP_SPEED,
    MENU_ITEM_COUNT
} menu_item_t;

static bool s_menu_active = false;
static bool s_settings_error;
static int s_menu_selected = 0;
static bool s_menu_pre_wrap_next = false;
static bool s_menu_pre_wrap_prev = false;

static const char *ui_overlay_status = NULL;
static TickType_t ui_overlay_until = 0;

// true = uživatel zmáčkl PLAY při MOTOR LOW a čekáme na MOTOR HIGH.
static bool play_wait_motor = false;
static bool play_motor_override = false;
static TickType_t play_wait_motor_armed_tick = 0;

// zámek PLAY, aby se jeden fyzický stisk / repeat nerozpadl na víc akcí
// uvolní se až při BTN_NONE
static bool play_key_lock = false;

// preview metadata pro waiting playback screen
static bool s_play_preview_valid = false;
static char s_play_preview_path[256];
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
static bool s_rec_buffer_display_valid = false;
static uint8_t s_rec_buffer_display_ok = 100;
static TickType_t s_rec_buffer_display_last = 0;

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

static bool path_is_binary_tape_path(const char *path)
{
    const char *ext;

    if (path == NULL) {
        return false;
    }

    ext = strrchr(path, '.');
    if (ext == NULL) {
        return false;
    }

    return strcasecmp(ext, ".mzf") == 0 ||
           strcasecmp(ext, ".mzt") == 0 ||
           strcasecmp(ext, ".m12") == 0;
}

static bool motor_signal_is_high(void)
{
    return (gpio_get_level(MOTOR_GPIO) != 0);
}

static bool transport_motor_is_high(void)
{
    return !s_setting_motor_auto || motor_signal_is_high();
}

static bool switch_signal_is_high(void)
{
    return (gpio_get_level(SWITCH_GPIO) != 0);
}

static bool sd_detect_card_inserted(void)
{
    return (gpio_get_level(SD_DETECT_GPIO) == SD_DETECT_ACTIVE_LEVEL);
}

static void service_sense_output(void)
{
    if (player_is_ultrafast_active()) {
        return;
    }

    recorder_state_t rst = recorder_get_state();

    // RECORD workflow má prioritu:
    // RECORDING / RECORD PAUSED / MOTOR PAUSED / WAIT_MOTOR -> LOW.
    // RECORD FINISHED / RECORD ERROR -> HIGH
    switch (rst) {
        case RECORDER_STATE_WAIT_MOTOR:
        case RECORDER_STATE_RECORDING:
        case RECORDER_STATE_PAUSED_USER:
        case RECORDER_STATE_PAUSED_MOTOR:
            gpio_set_level(SENSE_GPIO, 0);
            return;

        case RECORDER_STATE_FINISHED:
        case RECORDER_STATE_DELETED:
        case RECORDER_STATE_ERROR:
            gpio_set_level(SENSE_GPIO, 1);
            return;

        case RECORDER_STATE_IDLE:
        default:
            break;
    }

    if (play_wait_motor) {
        gpio_set_level(SENSE_GPIO, 0);
        return;
    }

    // standardní playback logika
    player_state_t pst = player_get_state();
    if (pst == PLAYER_STATE_PLAYING || pst == PLAYER_STATE_PAUSED) {
        gpio_set_level(SENSE_GPIO, 0);   // PLAY / PAUSE -> LOW
    } else {
        gpio_set_level(SENSE_GPIO, 1);   // STOP / IDLE / EOF / ERROR / BUF! -> HIGH
    }
}

static void service_monitor_output(void)
{
    recorder_state_t rst = recorder_get_state();
    player_state_t pst = player_get_state();

    if (player_is_ultrafast_active()) {
        gpio_set_level(MONITOR_GPIO, 0);
        return;
    }

    if (play_wait_motor ||
        pst == PLAYER_STATE_PLAYING ||
        pst == PLAYER_STATE_PAUSED ||
        rst == RECORDER_STATE_WAIT_MOTOR ||
        rst == RECORDER_STATE_RECORDING ||
        rst == RECORDER_STATE_PAUSED_USER ||
        rst == RECORDER_STATE_PAUSED_MOTOR) {
        gpio_set_level(MONITOR_GPIO, 1);
        return;
    }

    if (rst == RECORDER_STATE_IDLE &&
        (pst == PLAYER_STATE_IDLE ||
         pst == PLAYER_STATE_STOPPED ||
         pst == PLAYER_STATE_EOF ||
         pst == PLAYER_STATE_UNDERRUN ||
         pst == PLAYER_STATE_ERROR)) {
        gpio_set_level(MONITOR_GPIO, 0);
    }
}

static void ui_show_overlay(const char *text, uint32_t duration_ms)
{
    ui_overlay_status = text;

    // duration_ms == 0 => overlay je latched a zmizí jen přes ui_clear_overlay()
    if (duration_ms == 0) {
        ui_overlay_until = 0;
    } else {
        ui_overlay_until = xTaskGetTickCount() + pdMS_TO_TICKS(duration_ms);
    }
}

static void ui_clear_overlay(void)
{
    ui_overlay_status = NULL;
    ui_overlay_until = 0;
}

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

static size_t ui_get_scroll_offset(const char *text, size_t visible_chars)
{
    if (text == NULL || visible_chars == 0) {
        return 0;
    }

    size_t len = strlen(text);
    if (len <= visible_chars) {
        return 0;
    }

    size_t max_offset = len - visible_chars;
    TickType_t now = xTaskGetTickCount();

    uint32_t hold_ticks = pdMS_TO_TICKS(UI_SCROLL_HOLD_MS);
    uint32_t step_ticks = pdMS_TO_TICKS(UI_SCROLL_STEP_MS);
    if (step_ticks == 0) {
        step_ticks = 1;
    }

    // na začátku chvíli stoj
    if ((uint32_t)now < hold_ticks) {
        return 0;
    }

    uint32_t scrolled_ticks = (uint32_t)now - hold_ticks;
    uint32_t step_index = scrolled_ticks / step_ticks;

    // délka cyklu = rozsah posunu + krátká mezera před návratem
    size_t cycle = max_offset + UI_SCROLL_GAP_CHARS + 1;
    if (cycle == 0) {
        return 0;
    }

    size_t phase = step_index % cycle;

    // v "gap" části skoč zpět na začátek
    if (phase > max_offset) {
        return 0;
    }

    return phase;
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

static void ui_build_browser_line(int row, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    dst[0] = '\0';

    const char *name = file_list_get_visible_name(row);
    file_item_type_t type = file_list_get_visible_type(row);
    bool selected = file_list_is_visible_selected(row);

    if (name == NULL || name[0] == '\0') {
        return;
    }

    size_t pos = 0;

    if (pos < (dst_size - 1)) {
        dst[pos++] = ' ';
    }

    // zvýraznění výběru
    if (pos < (dst_size - 1)) {
        dst[pos++] = selected ? '}' : ' ';
    }
    // parent položka
    if (type == FILE_ITEM_PARENT) {
        const char *parent_text = "..";
        size_t i = 0;
        while (parent_text[i] != '\0' && pos < (dst_size - 1)) {
            dst[pos++] = parent_text[i++];
        }
        dst[pos] = '\0';
        return;
    }

    // kolik znaků zbývá na jméno
    size_t max_name_len = (dst_size - 1) - pos;
    if (type == FILE_ITEM_DIR && max_name_len > 0) {
        max_name_len--;   // rezervuj místo pro '/'
    }

    size_t name_len = strlen(name);
    size_t start = 0;

    // scrolluj jen vybraný řádek a jen pokud se text nevejde
    if (selected && name_len > max_name_len) {
        start = ui_get_scroll_offset(name, max_name_len);
    }

    size_t i = 0;
    while (name[start + i] != '\0' && i < max_name_len && pos < (dst_size - 1)) {
        dst[pos++] = name[start + i];
        i++;
    }

    if (type == FILE_ITEM_DIR && pos < (dst_size - 1)) {
        dst[pos++] = '/';
    }

    dst[pos] = '\0';
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

static void dirname_copy(char *dst, size_t dst_size, const char *path)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    dst[0] = '\0';

    if (path == NULL || path[0] == '\0') {
        return;
    }

    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return;
    }

    size_t len = (size_t)(slash - path);
    if (len >= dst_size) {
        len = dst_size - 1;
    }

    memcpy(dst, path, len);
    dst[len] = '\0';
}

static void format_mmss(char *dst, size_t dst_size, uint32_t seconds)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    uint32_t mm = seconds / 60U;
    uint32_t ss = seconds % 60U;
    snprintf(dst, dst_size, "%02lu:%02lu",
             (unsigned long)mm,
             (unsigned long)ss);
}

static void build_progress_bar(char *dst, size_t dst_size, uint8_t percent)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    const int inner = 19;   // [###################]
    int filled = (percent * inner) / 100;

    int pos = 0;
    if (pos < (int)(dst_size - 1)) {
        dst[pos++] = '[';
    }

    for (int i = 0; i < inner && pos < (int)(dst_size - 1); i++) {
        dst[pos++] = (i < filled) ? '#' : '-';
    }

    if (pos < (int)(dst_size - 1)) {
        dst[pos++] = ']';
    }

    dst[pos] = '\0';
}

// --------------------------------------------------------
// SD runtime helpers
// --------------------------------------------------------

static bool ensure_recordings_dir(void)
{
    struct stat st;

    if (stat(RECORDINGS_DIR, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;
        }

        printf("RECORDINGS path exists but is not dir\n");
        return false;
    }

    if (mkdir(RECORDINGS_DIR, 0775) == 0) {
        printf("created %s\n", RECORDINGS_DIR);
        return true;
    }

    printf("mkdir %s failed\n", RECORDINGS_DIR);
    return false;
}

static bool sd_runtime_try_mount(void)
{
    // žádný zapisovací sdcard_test()
    sdcard_init();

    if (!sdcard_is_mounted()) {
        s_sd_ready = false;
        return false;
    }

    if (!ensure_recordings_dir()) {
        s_sd_ready = false;
        return false;
    }

    if (!file_list_init("/sd")) {
        printf("browser init FAIL\n");
        s_sd_ready = false;
        return false;
    }

    s_sd_ready = true;
    s_sd_next_mount_retry = 0;

    printf("browser count = %d\n", file_list_count());
    printf("browser dir = %s\n", file_list_get_current_dir());
    printf("browser selected = %s\n", file_list_get_name());

    return true;
}

static void sd_runtime_on_removed(void)
{
    play_wait_motor_clear();
    clear_play_preview();
    ui_clear_overlay();

    player_state_t pst = player_get_state();
    if (pst == PLAYER_STATE_PLAYING ||
        pst == PLAYER_STATE_PAUSED ||
        pst == PLAYER_STATE_EOF ||
        pst == PLAYER_STATE_UNDERRUN ||
        pst == PLAYER_STATE_ERROR) {
        player_stop();
    }

    if (recorder_is_active()) {
        recorder_handle_stop();
    }

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
    s_menu_selected = 0;
    s_menu_pre_wrap_next = false;
    s_menu_pre_wrap_prev = false;
}

static void menu_exit(void)
{
    app_settings_t settings={.motor_auto=s_setting_motor_auto,.invert=raw_out_get_read_invert(),
        .record_auto=recorder_get_auto_signal(),.loader=player_get_loader_mode(),
        .record_format=recorder_get_format(),.tap_speed=player_get_tap_speed(),.record_rate=s_setting_rec_rate};
    s_settings_error=!settings_save(&settings);
    if(s_settings_error) ui_show_overlay("SETTINGS ERROR",2000);
    s_menu_active = false;
    s_menu_pre_wrap_next = false;
    s_menu_pre_wrap_prev = false;
}

static void menu_next(void)
{
    s_menu_selected++;
    if (s_menu_selected >= MENU_ITEM_COUNT) {
        s_menu_selected = 0;
    }
}

static void menu_prev(void)
{
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
            (void)player_set_loader_mode((loader_mode_t)((player_get_loader_mode()+1U)%LOADER_MODE_COUNT));
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
            } else {
                s_setting_rec_format = RECORDER_FORMAT_WAV;
                s_setting_rec_rate = 44100U;
                recorder_set_format(s_setting_rec_format);
                recorder_set_sample_rate(s_setting_rec_rate);
            }
            break;

        case MENU_ITEM_TAP_SPEED:
            (void)player_set_tap_speed((tap_speed_t)((player_get_tap_speed()+1U)%TAP_SPEED_COUNT));
            break;

        case MENU_ITEM_REC_AUTO_SIGNAL:
            s_setting_rec_auto_signal = !s_setting_rec_auto_signal;
            recorder_set_auto_signal(s_setting_rec_auto_signal);
            break;

        default:
            break;
    }
}

static void ui_build_menu_model(void)
{
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
             "%cREC AUTODETECT:%s",
             (s_menu_selected == MENU_ITEM_REC_AUTO_SIGNAL) ? '}' : ' ',
             recorder_get_auto_signal() ? "ON" : "OFF");

    snprintf(ui_menu_item5, sizeof(ui_menu_item5), "%cTAP SPEED:%u",
             (s_menu_selected == MENU_ITEM_TAP_SPEED) ? '}' : ' ',
             (unsigned)tap_stream_speed_baud(player_get_tap_speed()));

    ui_model.line[0] = ui_header;
    ui_model.line[1] = s_settings_error ? "SETTINGS ERROR" : "";
    ui_model.line[2] = ui_menu_item0;
    ui_model.line[3] = ui_menu_item1;
    ui_model.line[4] = ui_menu_item2;
    ui_model.line[5] = ui_menu_item3;
    ui_model.line[6] = ui_menu_item4;
    ui_model.line[7] = ui_menu_item5;
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
    const char *path = recorder_get_path();
    const char *file_name = basename_ptr(path);
    char time_text[16];
    recorder_state_t st = recorder_get_state();

    // řádek 0: header
    switch (st) {
        case RECORDER_STATE_WAIT_MOTOR:
            format_pause_header(ui_header, sizeof(ui_header), "RECORD PAUSED", 'M');
            break;

        case RECORDER_STATE_RECORDING:
            if (recorder_is_waiting_signal()) {
                snprintf(ui_header, sizeof(ui_header), "RECORDING READY");
            } else {
                snprintf(ui_header, sizeof(ui_header), "RECORDING");
            }
            break;

        case RECORDER_STATE_PAUSED_USER:
            format_pause_header(ui_header, sizeof(ui_header), "RECORD PAUSED", 'U');
            break;

        case RECORDER_STATE_PAUSED_MOTOR:
            format_pause_header(ui_header, sizeof(ui_header), "RECORD PAUSED", 'M');
            break;

        case RECORDER_STATE_FINISHED:
            snprintf(ui_header, sizeof(ui_header), "RECORD SAVED");
            break;

        case RECORDER_STATE_DELETED:
            snprintf(ui_header, sizeof(ui_header), "RECORD DELETED");
            break;

        case RECORDER_STATE_ERROR:
            snprintf(ui_header, sizeof(ui_header), "RECORD ERROR");
            break;

        default:
            snprintf(ui_header, sizeof(ui_header), "RECORD");
            break;
    }

    // řádek 1: cesta / adresář
    dirname_copy(ui_rec_dir, sizeof(ui_rec_dir), path);
    ui_copy_path_text(ui_path, sizeof(ui_path), ui_rec_dir);

    // řádek 2: název souboru
    ui_copy_scroll_text(ui_rec_file, sizeof(ui_rec_file), file_name);

    // řádek 3: signálový stav pod názvem souboru
    ui_rec_signal[0] = '\0';
    if (recorder_get_auto_signal()) {
        if (st == RECORDER_STATE_WAIT_MOTOR) {
            snprintf(ui_rec_signal, sizeof(ui_rec_signal), "AUTO SIGNAL ON");
        } else if (recorder_is_waiting_signal()) {
            snprintf(ui_rec_signal, sizeof(ui_rec_signal), "WAITING SIGNAL");
        } else if (recorder_has_valid_signal()) {
            snprintf(ui_rec_signal, sizeof(ui_rec_signal), "VALID SIGNAL");
        }
    }

    ui_rec_buffer[0] = '\0';
    if (st == RECORDER_STATE_WAIT_MOTOR ||
        st == RECORDER_STATE_RECORDING ||
        st == RECORDER_STATE_PAUSED_USER ||
        st == RECORDER_STATE_PAUSED_MOTOR) {
        uint8_t buffer_ok = recorder_get_buffer_ok_percent();
        TickType_t now = xTaskGetTickCount();

        if (!s_rec_buffer_display_valid || buffer_ok == 0U) {
            s_rec_buffer_display_ok = buffer_ok;
            s_rec_buffer_display_valid = true;
            s_rec_buffer_display_last = now;
        } else if ((uint32_t)(now - s_rec_buffer_display_last) >= pdMS_TO_TICKS(500)) {
            if (buffer_ok > s_rec_buffer_display_ok) {
                uint8_t step = buffer_ok - s_rec_buffer_display_ok;
                s_rec_buffer_display_ok += (step > 4U) ? 4U : step;
            } else if (buffer_ok < s_rec_buffer_display_ok) {
                uint8_t step = s_rec_buffer_display_ok - buffer_ok;
                s_rec_buffer_display_ok -= (step > 8U) ? 8U : step;
            }
            s_rec_buffer_display_last = now;
        }

        buffer_ok = s_rec_buffer_display_ok;
        snprintf(ui_rec_buffer, sizeof(ui_rec_buffer), "BUF %3u%% %s",
                 buffer_ok,
                 (buffer_ok == 0U) ? "BAD" : "OK");
    } else {
        s_rec_buffer_display_valid = false;
        s_rec_buffer_display_ok = 100U;
    }

    // řádek 5: čas záznamu
    format_mmss(time_text, sizeof(time_text), recorder_get_elapsed_seconds());
    snprintf(ui_rec_time, sizeof(ui_rec_time), "%s", time_text);

    // řádek 6: zvolený záznamový formát
    if (recorder_get_format() == RECORDER_FORMAT_LEP) {
        snprintf(ui_rec_rate, sizeof(ui_rec_rate), "LEP 50us");
    } else if (recorder_get_format() == RECORDER_FORMAT_L16) {
        snprintf(ui_rec_rate, sizeof(ui_rec_rate), "L16 16us");
    } else {
        snprintf(ui_rec_rate, sizeof(ui_rec_rate), "RATE %lu",
                 (unsigned long)recorder_get_sample_rate());
    }

    // řádek 7: footer
    ui_rec_footer[0] = '\0';

    ui_model.line[0] = ui_header;
    ui_model.line[1] = "";
    ui_model.line[2] = ui_path;
    ui_model.line[3] = ui_rec_file;
    ui_model.line[4] = ui_rec_signal;
    ui_model.line[5] = ui_rec_buffer;
    ui_model.line[6] = ui_rec_time;
    ui_model.line[7] = ui_rec_rate;
}

// --------------------------------------------------------

static bool ui_should_show_playback_screen(void)
{
    return player_screen_active(player_get_state());
}

static void ui_build_playback_model(void)
{
    const char *path = NULL;
    const char *file_name;
    char time_pos[16];
    char time_total[16];
    uint32_t shown_pos_seconds = 0;
    uint32_t shown_total_seconds = 0;
    uint32_t shown_sample_rate = 0;
    uint8_t shown_progress = 0;
    player_state_t st = player_get_state();
    player_pause_reason_t reason = player_get_pause_reason();
    bool edge_playback = false;
    bool binary_playback = false;
    bool tap_playback = false;

    if (play_wait_motor) {
        path = file_list_get_path();
    }

    if (path == NULL || path[0] == '\0') {
        path = player_get_current_path();
    }

    file_name = basename_ptr(path);
    edge_playback = (play_wait_motor && edge_stream_is_edge_path(path)) ||
                    (!play_wait_motor && player_is_edge_tape());
    binary_playback = path_is_binary_tape_path(path) ||
                      (!play_wait_motor && player_is_binary_tape());

    tap_playback = (play_wait_motor && tap_stream_is_tap_path(path)) ||
                   (!play_wait_motor && player_is_tap());

    // řádek 0: stav
    if (st == PLAYER_STATE_READY) {
        const sharp_stream_info_t *info=player_sharp_details();
        snprintf(ui_header,sizeof(ui_header),"RECORD %u/%u",(unsigned)info->record,(unsigned)info->records);
    } else if (st == PLAYER_STATE_EOF) {
        snprintf(ui_header, sizeof(ui_header), "PLAY FINISHED");
    } else if (st == PLAYER_STATE_UNDERRUN) {
        snprintf(ui_header, sizeof(ui_header), "PLAY BUF!");
    } else if (st == PLAYER_STATE_ERROR) {
        snprintf(ui_header, sizeof(ui_header), "PLAY ERROR");
    } else if (play_wait_motor) {
        format_pause_header(ui_header, sizeof(ui_header), "PLAY PAUSED", 'M');
    } else if (st == PLAYER_STATE_PAUSED && reason == PLAYER_PAUSE_MOTOR) {
        format_pause_header(ui_header, sizeof(ui_header), "PLAY PAUSED", 'M');
    } else if (st == PLAYER_STATE_PAUSED) {
        format_pause_header(ui_header, sizeof(ui_header), "PLAY PAUSED", 'U');
    } else {
        snprintf(ui_header, sizeof(ui_header), "PLAYING");
    }

    // waiting playback screen používá preview metadata,
    // běžící playback používá player_get_*()
    if (play_wait_motor) {
        shown_pos_seconds = 0;
        shown_progress = 0;

        if (s_play_preview_valid &&
            path != NULL &&
            strcmp(path, s_play_preview_path) == 0) {
            shown_total_seconds = s_play_preview_total_seconds;
            shown_sample_rate = s_play_preview_sample_rate;
        } else {
            shown_total_seconds = 0;
            shown_sample_rate = 0;
        }
    } else {
        shown_pos_seconds = player_get_position_seconds();
        shown_total_seconds = player_get_total_seconds();
        shown_sample_rate = player_get_sample_rate();
        shown_progress = player_get_progress_percent();
    }

    // řádek 1: cesta / adresář (scroll)
    dirname_copy(ui_play_dir, sizeof(ui_play_dir), path);
    ui_copy_path_text(ui_path, sizeof(ui_path), ui_play_dir);

    // řádek 2: název souboru (scroll)
    ui_copy_scroll_text(ui_play_file, sizeof(ui_play_file), file_name);

    if (st==PLAYER_STATE_READY) {
        const sharp_stream_info_t *info=player_sharp_details();
        shown_pos_seconds=0; shown_progress=0;
        shown_total_seconds=(uint32_t)(info->total_ticks/16000000ULL);
        ui_copy_scroll_text(ui_play_file,sizeof(ui_play_file),info->title);
    }

    // řádek 4: progress bar
    build_progress_bar(ui_play_bar, sizeof(ui_play_bar), shown_progress);

    // řádek 5: čas mm:ss / mm:ss
    if (edge_playback) {
        ui_play_time[0] = '\0';
    } else {
        format_mmss(time_pos, sizeof(time_pos), shown_pos_seconds);
        format_mmss(time_total, sizeof(time_total), shown_total_seconds);
        snprintf(ui_play_time, sizeof(ui_play_time), "%s / %s", time_pos, time_total);
    }

    // řádek 6: rate/baud
    if (edge_playback) {
        uint32_t pps = player_get_pulses_per_second();
        snprintf(ui_play_rate, sizeof(ui_play_rate), "p/s %lu",
                 (unsigned long)pps);
    } else if (tap_playback) {
        uint16_t baud = play_wait_motor ? tap_stream_speed_baud(player_get_tap_speed()) :
                                        (uint16_t)player_get_baud_rate();
        snprintf(ui_play_rate, sizeof(ui_play_rate), "TAP %u Bd", (unsigned)baud);
    } else if (binary_playback) {
        const sharp_stream_info_t *info=player_sharp_details();
        snprintf(ui_play_rate,sizeof(ui_play_rate),"%s%s%s",info->sidecar ? "I " : "",
                 loader_mode_name(info->effective),info->fallback ? " !" : "");
        if(info->ultrafast) snprintf(ui_play_time,sizeof(ui_play_time),"%lu / --:--",(unsigned long)shown_pos_seconds);
    } else {
        snprintf(ui_play_rate, sizeof(ui_play_rate), "RATE %lu",
                 (unsigned long)shown_sample_rate);
    }

    // řádek 7: footer
    ui_play_footer[0] = '\0';

    ui_model.line[0] = ui_header;
    ui_model.line[1] = "";
    ui_model.line[2] = ui_path;
    ui_model.line[3] = ui_play_file;
    ui_model.line[4] = "";
    ui_model.line[5] = ui_play_bar;
    ui_model.line[6] = ui_play_time;
    ui_model.line[7] = ui_play_rate;
}

static void ui_build_browser_model(void)
{
    ui_copy_path_text(ui_path, sizeof(ui_path), file_list_get_current_dir());

    ui_model.line[0] = ui_path;

    for (int row = 0; row < FILE_LIST_VIEW_ROWS; row++) {
        ui_build_browser_line(row, ui_items[row], sizeof(ui_items[row]));
        ui_model.line[1 + row] = ui_items[row];
    }
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
        if(state==RECORDER_STATE_RECORDING || state==RECORDER_STATE_WAIT_MOTOR ||
           state==RECORDER_STATE_PAUSED_USER || state==RECORDER_STATE_PAUSED_MOTOR)
            recorder_handle_stop();
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

static void selector_move(int delta)
{
    const sharp_stream_info_t *info=player_sharp_details();
    if(!info->records) return;
    uint16_t next=delta>0 ? (info->record==info->records ? 1 : info->record+1) :
        (info->record==1 ? info->records : info->record-1);
    char path[256]; snprintf(path,sizeof(path),"%s",player_get_current_path());
    if(!player_prepare_record(path,next)) ui_show_overlay("RECORD ERROR",1000);
}

static void activate_selected_item(bool motor_high)
{
    file_item_type_t type = file_list_get_type();

    if (type == FILE_ITEM_DIR || type == FILE_ITEM_PARENT) {
        play_wait_motor_clear();
        clear_play_preview();
        ui_clear_overlay();

        if (!file_list_enter_selected()) {
            ui_show_overlay("DIRERR", 1000);
        }
        return;
    }

    if (type == FILE_ITEM_WAV || type == FILE_ITEM_TAPE) {
        if(switch_ext_mode) { ui_show_overlay("EXTERNAL CMT",1000); return; }
        if (file_format_is_record_container(file_format_detect_from_name(file_list_get_path()))) {
            if(!player_prepare_record(file_list_get_path(),1)) ui_show_overlay("IMAGE ERROR",1000);
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
            if (s_setting_motor_auto) {
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
            ui_render(&ui_model);

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
                    recorder_handle_play(motor_high);
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
                recorder_handle_stop();
                if (s_sd_ready) {
                    file_list_reload_current_dir();
                }
            }
            // playback screen a playback error/finished screen
            else if (st == PLAYER_STATE_READY || st == PLAYER_STATE_PLAYING ||
                     st == PLAYER_STATE_PAUSED ||
                     st == PLAYER_STATE_EOF ||
                     st == PLAYER_STATE_UNDERRUN ||
                     st == PLAYER_STATE_ERROR) {
                bool container=player_is_binary_tape() && player_sharp_details()->records>1;
                uint16_t record=player_sharp_details()->record;
                char path[256]; snprintf(path,sizeof(path),"%s",player_get_current_path());
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
                if (s_sd_ready) {
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
            bool motor_high = transport_motor_is_high();

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

                        recorder_handle_rec(RECORDINGS_DIR, motor_high);
                    }
                } else {
                    // REC is intentionally inert while the record workflow is active.
                }
            }
        }

        service_motor_pause_resume();
        if (s_setting_motor_auto) {
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
        ui_render(&ui_model);

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
    ui_init();

    gpio_config_t read_cfg = {
        .pin_bit_mask = (1ULL << READ_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
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
    gpio_config(&sense_cfg);
    gpio_set_level(SENSE_GPIO, 1);   // default STOP/idle = HIGH

    gpio_config_t monitor_cfg = {
        .pin_bit_mask = (1ULL << MONITOR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&monitor_cfg);
    gpio_set_level(MONITOR_GPIO, 0);   // default STOP/idle = monitor muted

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
    (void)player_set_loader_mode(restored.loader);
    (void)player_set_tap_speed(restored.tap_speed);
    s_setting_play_ultrafast=player_get_ultrafast_enabled();
    recorder_set_format(s_setting_rec_format);
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
