#pragma once
#include <stdio.h>
#include <stdint.h>

typedef struct {
    FILE *f;
    uint32_t sample_rate;
    uint32_t data_size;
} wav_file_t;

int wav_open_read(wav_file_t *w, const char *path);
int wav_read(wav_file_t *w, uint8_t *buf, int len);

int wav_open_write(wav_file_t *w, const char *path, uint32_t sr);
int wav_write(wav_file_t *w, uint8_t *buf, int len);
void wav_close(wav_file_t *w);