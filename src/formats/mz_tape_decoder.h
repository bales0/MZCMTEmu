/* SPDX-License-Identifier: GPL-3.0-only
 * Protocol algorithms from bales0/MZ-SD2CMT2-Reborn, firmware 2.0.3,
 * commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb.
 * https://github.com/bales0/MZ-SD2CMT2-Reborn
 * Native ESP32-S3 foreground processing; no AVR peripheral code.
 */
#ifndef SD2CMT2_MZ_TAPE_DECODER_H
#define SD2CMT2_MZ_TAPE_DECODER_H

#include <stdbool.h>
#include <stdint.h>

#define MZ_TAPE_HEADER_BYTES 128U

typedef enum
{
    MZ_TAPE_DECODER_EVENT_NONE = 0,
    MZ_TAPE_DECODER_EVENT_HEADER_VALID,
    MZ_TAPE_DECODER_EVENT_DATA_BYTE,
    MZ_TAPE_DECODER_EVENT_BLOCK_VALID,
    MZ_TAPE_DECODER_EVENT_BLOCK_INVALID
} mz_tape_decoder_event_type_t;

typedef struct
{
    mz_tape_decoder_event_type_t type;
    uint8_t value;
    uint32_t byte_index;
    uint16_t calculated_checksum;
    uint16_t recorded_checksum;
    uint16_t leader_pulses;
    uint8_t copy_index;
} mz_tape_decoder_event_t;

void mz_tape_decoder_begin_header(void);
void mz_tape_decoder_start_data(uint32_t byte_count);
void mz_tape_decoder_start_recovery_data(uint32_t byte_count);
void mz_tape_decoder_break_signal(void);
void mz_tape_decoder_stop(void);
/* level is the physical external-CMT/MCU WRITE level. Sharp decoding uses
   physical LOW only; the function never probes or falls back to HIGH polarity.
   Returns true only when this interval produced a pending decoder event, so
   the foreground caller can skip an empty take_event() poll on the leader. */
bool mz_tape_decoder_feed_interval(uint16_t duration_units, uint8_t level);
bool mz_tape_decoder_take_event(mz_tape_decoder_event_t *event);
const uint8_t *mz_tape_decoder_get_header(void);
uint8_t *mz_tape_decoder_get_data_scratch(void);
uint16_t mz_tape_decoder_get_header_short_high_x8(void);
uint16_t mz_tape_decoder_get_header_long_high_x8(void);

/* Optional passive foreground observer. Does not consume pending events or
 * change decoding. The preflight instantiation owns a separate callback. */
typedef enum {MZ_OBSERVE_LEADER,MZ_OBSERVE_MARK,MZ_OBSERVE_HEADER,
              MZ_OBSERVE_VALID,MZ_OBSERVE_INVALID} mz_tape_observation_t;
typedef void (*mz_tape_observer_fn)(mz_tape_observation_t,bool header,void *);
void mz_tape_decoder_set_observer(mz_tape_observer_fn,void *);

#endif
