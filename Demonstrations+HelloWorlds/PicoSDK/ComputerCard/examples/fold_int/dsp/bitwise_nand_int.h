#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// Bitwise NAND cross-modulation:
// Convert to int16, NAND them (inverted AND), blend with inverted sum
inline int32_t process_nand_q15(int32_t x1_q15, int32_t x2_q15, int32_t parameter_q15) {
    // Convert Q15 to int16 (clamp to -32768..32767)
    int16_t x1_short = static_cast<int16_t>(x1_q15 > 32767 ? 32767 : (x1_q15 < -32768 ? -32768 : x1_q15));
    int16_t x2_short = static_cast<int16_t>(x2_q15 > 32767 ? 32767 : (x2_q15 < -32768 ? -32768 : x2_q15));
    
    // NAND the bits (NOT AND)
    int16_t nand_result = ~(x1_short & x2_short);
    int32_t mod_q15 = static_cast<int32_t>(nand_result);
    
    // Inverted sum = -(x1 + x2) * 0.5
    int32_t sum = x1_q15 + x2_q15;
    if (sum > Q15_MAX) sum = Q15_MAX;
    if (sum < Q15_MIN) sum = Q15_MIN;
    sum = -sum;
    
    // sum *= 0.5
    sum = sum >> 1;
    
    // return sum + (mod - sum) * parameter
    int32_t diff = mod_q15 - sum;
    int32_t param_diff = mul_q15(diff, parameter_q15);
    int32_t result = sum + param_diff;
    
    if (result > Q15_MAX) result = Q15_MAX;
    if (result < Q15_MIN) result = Q15_MIN;
    
    return result;
}

} // namespace cc_dsp

