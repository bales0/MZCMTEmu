#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "play/loader_mode.h"
/* Allocate the bounded payload workspace once, outside capture/start/stop. */
void mzf_writer_init(void);
bool mzf_writer_open(const char *path);
bool mzf_writer_feed(uint8_t level,uint32_t duration_us);
bool mzf_writer_complete(void);
bool mzf_writer_finalize(void);
void mzf_writer_abort_delete(void);
const char *mzf_writer_error(void);
uint8_t mzf_writer_progress(void);
bool mzf_writer_profile(loader_mode_t *mode);
const uint8_t *mzf_writer_header(void);

#include "formats/program_metadata.h"
void mzf_writer_metadata(program_metadata_t *out);
