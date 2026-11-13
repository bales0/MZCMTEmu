#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct fake_rmt_channel *rmt_channel_handle_t;
typedef struct fake_rmt_encoder *rmt_encoder_handle_t;
typedef union { struct {uint32_t duration0:15,level0:1,duration1:15,level1:1;};uint32_t val;} rmt_symbol_word_t;
typedef struct {size_t num_symbols;} rmt_tx_done_event_data_t;
typedef size_t (*rmt_encode_simple_cb_t)(const void *,size_t,size_t,size_t,rmt_symbol_word_t *,bool *,void *);
typedef struct {rmt_encode_simple_cb_t callback;void *arg;size_t min_chunk_size;} rmt_simple_encoder_config_t;
typedef struct {int gpio_num,clk_src;uint32_t resolution_hz;size_t mem_block_symbols,trans_queue_depth;int intr_priority;struct {unsigned invert_out:1,with_dma:1,allow_pd:1,init_level:1;}flags;} rmt_tx_channel_config_t;
typedef struct {int loop_count;struct {unsigned eot_level:1,queue_nonblocking:1;}flags;} rmt_transmit_config_t;
typedef struct {bool (*on_trans_done)(rmt_channel_handle_t,const rmt_tx_done_event_data_t *,void *);} rmt_tx_event_callbacks_t;
#define RMT_CLK_SRC_DEFAULT 1
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *,rmt_channel_handle_t *);
esp_err_t rmt_del_channel(rmt_channel_handle_t);
esp_err_t rmt_new_simple_encoder(const rmt_simple_encoder_config_t *,rmt_encoder_handle_t *);
esp_err_t rmt_del_encoder(rmt_encoder_handle_t);
esp_err_t rmt_tx_register_event_callbacks(rmt_channel_handle_t,const rmt_tx_event_callbacks_t *,void *);
esp_err_t rmt_tx_switch_gpio(rmt_channel_handle_t,int,bool);
esp_err_t rmt_enable(rmt_channel_handle_t);
esp_err_t rmt_disable(rmt_channel_handle_t);
esp_err_t rmt_transmit(rmt_channel_handle_t,rmt_encoder_handle_t,const void *,size_t,const rmt_transmit_config_t *);
