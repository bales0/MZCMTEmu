#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct fake_cap_timer *mcpwm_cap_timer_handle_t;
typedef struct fake_cap_channel *mcpwm_cap_channel_handle_t;
#define MCPWM_CAPTURE_CLK_SRC_DEFAULT 1
#define MCPWM_CAP_EDGE_POS 0
#define MCPWM_CAP_EDGE_NEG 1
typedef struct {int group_id,clk_src;uint32_t resolution_hz;} mcpwm_capture_timer_config_t;
typedef struct {int gpio_num,intr_priority;uint32_t prescale;struct {unsigned pos_edge:1,neg_edge:1;} flags;} mcpwm_capture_channel_config_t;
typedef struct {uint32_t cap_value;int cap_edge;} mcpwm_capture_event_data_t;
typedef struct {bool (*on_cap)(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t *,void *);} mcpwm_capture_event_callbacks_t;
esp_err_t mcpwm_new_capture_timer(const mcpwm_capture_timer_config_t *,mcpwm_cap_timer_handle_t *);
esp_err_t mcpwm_del_capture_timer(mcpwm_cap_timer_handle_t);
esp_err_t mcpwm_capture_timer_enable(mcpwm_cap_timer_handle_t);
esp_err_t mcpwm_capture_timer_disable(mcpwm_cap_timer_handle_t);
esp_err_t mcpwm_capture_timer_start(mcpwm_cap_timer_handle_t);
esp_err_t mcpwm_capture_timer_stop(mcpwm_cap_timer_handle_t);
esp_err_t mcpwm_capture_timer_get_resolution(mcpwm_cap_timer_handle_t,uint32_t *);
esp_err_t mcpwm_new_capture_channel(mcpwm_cap_timer_handle_t,const mcpwm_capture_channel_config_t *,mcpwm_cap_channel_handle_t *);
esp_err_t mcpwm_del_capture_channel(mcpwm_cap_channel_handle_t);
esp_err_t mcpwm_capture_channel_enable(mcpwm_cap_channel_handle_t);
esp_err_t mcpwm_capture_channel_disable(mcpwm_cap_channel_handle_t);
esp_err_t mcpwm_capture_channel_register_event_callbacks(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_callbacks_t *,void *);
esp_err_t mcpwm_capture_channel_trigger_soft_catch(mcpwm_cap_channel_handle_t);
esp_err_t mcpwm_capture_get_latched_value(mcpwm_cap_channel_handle_t,uint32_t *);
