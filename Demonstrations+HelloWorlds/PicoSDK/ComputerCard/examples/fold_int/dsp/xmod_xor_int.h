#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// XOR cross-modulation (Q1.15) from Warps' ALGORITHM_XOR:
// Convert to int16, XOR them, blend with sum based on parameter
inline int32_t process_xor_q15(int32_t x1_q15, int32_t x2_q15, int32_t parameter_q15) {
    // Convert Q15 to int16 (clamp to -32768..32767)
    int16_t x1_short = static_cast<int16_t>(x1_q15 > 32767 ? 32767 : (x1_q15 < -32768 ? -32768 : x1_q15));
    int16_t x2_short = static_cast<int16_t>(x2_q15 > 32767 ? 32767 : (x2_q15 < -32768 ? -32768 : x2_q15));
    
    // XOR the bits
    int16_t xor_result = x1_short ^ x2_short;
    int32_t mod_q15 = static_cast<int32_t>(xor_result);
    
    // Sum = (x1 + x2) * 0.7
    int32_t sum = x1_q15 + x2_q15;
    if (sum > Q15_MAX) sum = Q15_MAX;
    if (sum < Q15_MIN) sum = Q15_MIN;
    
    // sum *= 0.7 (using ~22938 / 32768 = 0.7)
    constexpr int32_t Q15_0P7 = 22938;
    sum = mul_q15(sum, Q15_0P7);
    
    // return sum + (mod - sum) * parameter
    int32_t diff = mod_q15 - sum;
    int32_t param_diff = mul_q15(diff, parameter_q15);
    int32_t result = sum + param_diff;
    
    if (result > Q15_MAX) result = Q15_MAX;
    if (result < Q15_MIN) result = Q15_MIN;
    
    return result;
}

} // namespace cc_dsp


