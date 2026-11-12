#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool edge_writer_open(const char *path, uint32_t sample_rate);
size_t edge_writer_write_samples(const uint8_t *samples, size_t len);
bool edge_writer_write_interval_units(uint8_t level, uint32_t units);
bool edge_writer_finalize(void);
void edge_writer_abort_delete(void);

#ifdef __cplusplus
}
#endif
