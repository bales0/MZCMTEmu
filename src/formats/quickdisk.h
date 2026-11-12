#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef bool (*quickdisk_cancel_fn)(void *context);
/* Non-owning FILE, read-only. Header/payload directory is bounded to 50 records. */
bool quickdisk_open(FILE *file,uint32_t size,quickdisk_cancel_fn cancel,void *context);
void quickdisk_close(void);
uint16_t quickdisk_count(void);
bool quickdisk_select(uint16_t record,uint8_t header[128],uint32_t *length);
bool quickdisk_seek(uint32_t position);
size_t quickdisk_read(void *out,size_t size);
bool quickdisk_was_cancelled(void);
const char *quickdisk_kind(void);
