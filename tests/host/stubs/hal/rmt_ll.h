#pragma once
#include <stdbool.h>
#include "soc/rmt_struct.h"
#define RMT_LL_GET(attr) (RMT_LL_##attr)
#define RMT_LL_INST_NUM 1
void test_rmt_clock_gate(bool enable);
static inline void rmt_ll_enable_group_clock(rmt_dev_t *dev,bool enable) {(void)dev;test_rmt_clock_gate(enable);}
