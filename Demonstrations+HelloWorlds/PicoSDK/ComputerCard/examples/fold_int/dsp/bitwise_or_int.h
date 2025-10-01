#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// Bitwise OR cross-modulation:
// Convert to int16, OR them, blend with product based on parameter
inline int32_t process_or_q15(int32_t x1_q15, int32_t x2_q15, int32_t parameter_q15) {
    // Convert Q15 to int16 (clamp to -32768..32767)
    int16_t x1_short = static_cast<int16_t>(x1_q15 > 32767 ? 32767 : (x1_q15 < -32768 ? -32768 : x1_q15));
    int16_t x2_short = static_cast<int16_t>(x2_q15 > 32767 ? 32767 : (x2_q15 < -32768 ? -32768 : x2_q15));
    
    // OR the bits
    int16_t or_result = x1_short | x2_short;
    int32_t mod_q15 = static_cast<int32_t>(or_result);
    
    // Product = x1 * x2
    int32_t prod = mul_q15(x1_q15, x2_q15);
    
    // return prod + (mod - prod) * parameter
    int32_t diff = mod_q15 - prod;
    int32_t param_diff = mul_q15(diff, parameter_q15);
    int32_t result = prod + param_diff;
    
    if (result > Q15_MAX) result = Q15_MAX;
    if (result < Q15_MIN) result = Q15_MIN;
    
    return result;
}

} // namespace cc_dsp

