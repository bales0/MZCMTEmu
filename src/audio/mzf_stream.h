#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "audio/sharp_stream.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t sample_rate,total_seconds,total_samples,baud_rate; } mzf_stream_info_t;
bool mzf_stream_open(const char *path,mzf_stream_info_t *info);
void mzf_stream_close(void);
bool mzf_stream_is_eof(void);
bool mzf_stream_has_error(void);
uint32_t mzf_stream_get_samples_emitted(void);
void mzf_stream_set_ultrafast_enabled(bool enabled);
bool mzf_stream_get_ultrafast_enabled(void);
bool mzf_stream_is_ultrafast_ready(void);
bool mzf_stream_is_ultrafast_active(void);
bool mzf_stream_begin_ultrafast(void);
void mzf_stream_service_ultrafast(void);
uint32_t mzf_stream_get_ultrafast_total_bytes(void);
uint32_t mzf_stream_get_ultrafast_sent_bytes(void);
uint32_t mzf_stream_get_baud_rate(void);
bool mzf_stream_next_pulse(uint8_t *level,uint32_t *duration_us);
bool mzf_stream_set_loader_mode(loader_mode_t mode);
loader_mode_t mzf_stream_get_loader_mode(void);
const sharp_stream_info_t *mzf_stream_details(void);
bool mzf_stream_select(const char *path,uint16_t record);
bool mzf_stream_select_mode(const char *path,uint16_t record,loader_mode_t mode);
void mzf_stream_clear_selection(void);
bool mzf_stream_at_boundary(void);
uint32_t mzf_stream_boundary_delay_us(bool motor_low_seen);
bool mzf_stream_advance(void);
void mzf_stream_pause_ul(bool pause);

#ifdef __cplusplus
}
#endif
