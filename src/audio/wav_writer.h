#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool wav_writer_open(const char *path, uint32_t sample_rate, uint16_t bits_per_sample, uint16_t num_channels);
size_t wav_writer_write(const uint8_t *data, size_t len);
bool wav_writer_finalize(void);
void wav_writer_abort_delete(void);
void wav_writer_close(void);

bool wav_writer_is_open(void);
const char *wav_writer_get_path(void);
uint32_t wav_writer_get_data_bytes(void);
uint32_t wav_writer_get_sample_rate(void);

#ifdef __cplusplus
}
#endif