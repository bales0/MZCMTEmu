#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "play/loader_mode.h"
#include "audio/tap_stream.h"
#include "audio/recorder.h"
#define SETTINGS_BYTES 24U
typedef struct {
    bool motor_auto, invert, record_auto, autoname;
    loader_mode_t loader;
    recorder_format_t record_format;
    tap_speed_t tap_speed;
    uint32_t record_rate;
    uint8_t brightness,loader_speeds,record_control; /* 0 MOTOR,1 MANUAL,2 AUTO */
} app_settings_t;
void settings_defaults(app_settings_t *out);
bool settings_valid(const app_settings_t *settings);
bool settings_encode(const app_settings_t *settings,uint8_t out[SETTINGS_BYTES]);
bool settings_decode(const uint8_t *data,size_t size,app_settings_t *out);
/* 1 = restored, 0 = no saved settings/defaults, -1 = error/defaults. */
int settings_load(app_settings_t *out);
bool settings_save(const app_settings_t *settings);
