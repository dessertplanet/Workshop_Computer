#pragma once

#include <stdint.h>

// LUT sizes chosen to balance accuracy vs flash.
// - Sin/cos: quarter-wave table (0..pi/2) with 1024 segments (1025 samples)
// - Atan: atan(r) for r in [0,1] with 1024 segments (1025 samples)
// - Log2: log2(1 + i/256) for i=0..256 in Q16
// - Exp2: 2^(i/256) for i=0..256 in Q16

#define FM_SIN_Q30_LUT_BITS 10
#define FM_SIN_Q30_LUT_SIZE (1u << FM_SIN_Q30_LUT_BITS)

#define FM_ATAN_Q16_LUT_BITS 10
#define FM_ATAN_Q16_LUT_SIZE (1u << FM_ATAN_Q16_LUT_BITS)

#define FM_LOG2_Q16_LUT_SIZE 256u
#define FM_EXP2_Q16_LUT_SIZE 256u

// Q1.30 values for sin(theta), theta = i*(pi/2)/1024
extern const int32_t fm_sin_q30_quarter_lut[FM_SIN_Q30_LUT_SIZE + 1u];

// Q16 radians for atan(r), r = i/1024
extern const int32_t fm_atan_q16_lut[FM_ATAN_Q16_LUT_SIZE + 1u];

// Q16 values for log2(1 + i/256), i=0..256
extern const int32_t fm_log2_q16_lut[FM_LOG2_Q16_LUT_SIZE + 1u];

// Q16 values for 2^(i/256), i=0..256
extern const uint32_t fm_exp2_q16_lut[FM_EXP2_Q16_LUT_SIZE + 1u];
