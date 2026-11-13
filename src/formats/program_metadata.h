/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "formats/file_format.h"
#include "play/loader_mode.h"
typedef enum {PROGRAM_PROTOCOL_UNKNOWN,PROGRAM_SHARP,PROGRAM_SPECTRUM,PROGRAM_CPM} program_protocol_t;
typedef enum {PROGRAM_SOURCE_NONE,PROGRAM_MZF_HEADER,PROGRAM_TAPE_DECODE,PROGRAM_TAP_HEADER,PROGRAM_LOADER_EMBEDDED,PROGRAM_FILENAME} program_source_t;
typedef enum {PROGRAM_WAITING,PROGRAM_LOADER_ONLY,PROGRAM_HEADER_VALID,PROGRAM_PAYLOAD_VALID,PROGRAM_UNKNOWN,PROGRAM_INVALID} program_confidence_t;
enum {PROGRAM_TITLE=1U,PROGRAM_TYPE=2U,PROGRAM_SIZE=4U,PROGRAM_LOAD=8U,PROGRAM_EXEC=16U,PROGRAM_AUTOSTART=32U,PROGRAM_RECORDED_MODE=64U,PROGRAM_TRANSFER_MODE=128U};
typedef struct {
 uint32_t valid,declared_size,received_bytes;
 uint16_t load,exec,autostart;
 uint32_t record,records;
 uint8_t file_type,protocol,source,confidence,chain_stage;
 file_format_t format;
 loader_mode_t transfer_loader_mode,recorded_loader_mode;
 char title[32];
} program_metadata_t;
/* Atomic word publication makes snapshots data-race-free, including strings.
 * One foreground writer; no ISR ever touches this store. No dynamic memory. */
typedef struct {uint32_t seq,words[(sizeof(program_metadata_t)+3)/4];} program_metadata_store_t;
void program_metadata_reset(program_metadata_t *,file_format_t);
void program_metadata_publish(program_metadata_store_t *,const program_metadata_t *);
void program_metadata_snapshot(const program_metadata_store_t *,program_metadata_t *);
void program_metadata_sharp(program_metadata_t *,const uint8_t header[128],program_source_t);
bool program_metadata_source_sharp(program_metadata_t *,const uint8_t header[128],const uint8_t *first_payload,size_t);
loader_mode_t program_metadata_copier_mode(unsigned profile);
bool program_metadata_zx(program_metadata_t *,const uint8_t header[19]);
const char *program_metadata_type(const program_metadata_t *);
