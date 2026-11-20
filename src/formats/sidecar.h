#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "play/loader_mode.h"
/* Record numbers are 1-based. Manual mode bypasses metadata completely. */
bool sidecar_read(const char *tape_path, uint16_t record, loader_mode_t *mode);
loader_mode_t sidecar_resolve(const char *path,uint16_t record,loader_mode_t requested,bool *used);

/* Creates metadata for a closed canonical MZF. Existing metadata is preserved. */
bool sidecar_write_mzf(const char *path,loader_mode_t mode);

/* Presence only, including invalid/manual profiles; no payload parsing. */
bool sidecar_exists(const char *tape_path);

#include <stddef.h>
/* Pure path construction; output is untouched on error. */
bool sidecar_build_path(const char *,char *,size_t);
/* Finds the actual directory spelling. Ambiguous case aliases fail. */
bool sidecar_find(const char *,char *,size_t,bool *exists);
