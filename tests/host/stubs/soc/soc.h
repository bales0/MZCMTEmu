#pragma once
#include <stdint.h>
void test_reg_write(uint32_t reg, uint32_t mask);
#define REG_WRITE(reg, value) test_reg_write(reg, value)
