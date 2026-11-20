/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "play/loader_mode.h"
#define SC_MAX_PROFILES 256
#define SC_NONE 255U
#define SC_MAX_BYTES 16384U
typedef struct { uint16_t record; uint8_t original,current; } sc_entry_t;
typedef struct {
 char source[1024],target[1024];
 sc_entry_t entries[SC_MAX_PROFILES];
 uint16_t count,records;
 uint32_t original_hash,original_size,source_size;
 int64_t source_mtime;
 bool existed,recovery;
 const char *error;
} sc_document_t;
/* Caller owns the session. All I/O runs inside the storage worker job. */
bool sc_load(sc_document_t *,const char *,uint16_t records);
uint8_t sc_profile(const sc_document_t *,uint16_t record);
bool sc_accept(sc_document_t *,uint16_t record,uint8_t profile);
unsigned sc_changes(const sc_document_t *);
bool sc_save(sc_document_t *);
bool sc_recover(sc_document_t *);
bool sc_mode_fields(uint8_t mode,const char **family,const char **speed);
uint8_t sc_make_mode(unsigned family,unsigned speed);
