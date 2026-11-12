#pragma once

#define UI_NUM_LINES 8

typedef struct {
    const char *line[UI_NUM_LINES];
} ui_model_t;

void ui_init(void);
void ui_render(const ui_model_t *model);
