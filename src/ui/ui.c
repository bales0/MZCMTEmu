#include "ui.h"
#include "drivers/oled.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UI_TEXT_WIDTH 21
#define UI_REFRESH_MS 50U
static TickType_t last_refresh;
static bool refresh_started;
#define UI_CACHE_TEXT_LEN (UI_TEXT_WIDTH + 1)
#define UI_CLEAR_LINE "                     "   // 21 mezer

static char s_last_lines[UI_NUM_LINES][UI_CACHE_TEXT_LEN];
static bool s_initialized = false;

static const char *ui_safe_text(const char *s)
{
    return (s != NULL) ? s : "";
}

static void ui_make_render_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    const char *in = ui_safe_text(src);

    size_t i = 0;
    while (in[i] != '\0' && i < (dst_size - 1)) {
        dst[i] = in[i];
        i++;
    }
    dst[i] = '\0';
}

void ui_init(void)
{
    for (int i = 0; i < UI_NUM_LINES; i++) {
        s_last_lines[i][0] = '\0';
    }
    refresh_started=false;
    s_initialized = true;
}

void ui_render(const ui_model_t *model)
{
    if (model == NULL) {
        return;
    }

    if (!s_initialized) {
        ui_init();
    }

    TickType_t now=xTaskGetTickCount();
    if(refresh_started && (TickType_t)(now-last_refresh)<pdMS_TO_TICKS(UI_REFRESH_MS))return;
    uint32_t dirty_mask = 0;
    char current_lines[UI_NUM_LINES][UI_CACHE_TEXT_LEN];

    for (int i = 0; i < UI_NUM_LINES; i++) {
        ui_make_render_text(current_lines[i], sizeof(current_lines[i]), model->line[i]);

        if (strcmp(current_lines[i], s_last_lines[i]) != 0) {
            dirty_mask |= (1u << i);
        }
    }

    if (dirty_mask == 0) {
        return;
    }

    for (int i = 0; i < UI_NUM_LINES; i++) {
        if (dirty_mask & (1u << i)) {
            oled_draw_text(i, UI_CLEAR_LINE);
            oled_draw_text(i, current_lines[i]);

        }
    }

    uint8_t completed=oled_update_pages((uint8_t)dirty_mask);
    for(int i=0;i<UI_NUM_LINES;++i)if(completed & (1U<<i))
        memcpy(s_last_lines[i],current_lines[i],sizeof(s_last_lines[i]));
    last_refresh=now;refresh_started=true;
}