#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// XOR with rotation cross-modulation:
// XOR x1 with bit-rotated version of x2
inline int32_t process_xor_rotate_q15(int32_t x1_q15, int32_t x2_q15, int32_t parameter_q15) {
    // Convert Q15 to int16
    int16_t x1_short = static_cast<int16_t>(x1_q15 > 32767 ? 32767 : (x1_q15 < -32768 ? -32768 : x1_q15));
    int16_t x2_short = static_cast<int16_t>(x2_q15 > 32767 ? 32767 : (x2_q15 < -32768 ? -32768 : x2_q15));
    
    // Rotation amount based on parameter (0-15 bits)
    uint32_t rotation = (static_cast<uint32_t>(parameter_q15 < 0 ? 0 : parameter_q15) >> 11) & 0x0F;
    
    // Rotate x2
    uint16_t x2_u = static_cast<uint16_t>(x2_short);
    uint16_t x2_rotated = static_cast<uint16_t>((x2_u << rotation) | (x2_u >> (16 - rotation)));
    
    // XOR x1 with rotated x2
    uint16_t x1_u = static_cast<uint16_t>(x1_short);
    uint16_t xor_result = x1_u ^ x2_rotated;
    
    int32_t mod_q15 = static_cast<int32_t>(static_cast<int16_t>(xor_result));
    
    // Blend with sum
    int32_t sum = x1_q15 + x2_q15;
    if (sum > Q15_MAX) sum = Q15_MAX;
    if (sum < Q15_MIN) sum = Q15_MIN;
    
    // sum *= 0.7
    constexpr int32_t Q15_0P7 = 22938;
    sum = mul_q15(sum, Q15_0P7);
    
    // Blend based on parameter
    int32_t diff = mod_q15 - sum;
    int32_t param_diff = mul_q15(diff, parameter_q15);
    int32_t result = sum + param_diff;
    
    if (result > Q15_MAX) result = Q15_MAX;
    if (result < Q15_MIN) result = Q15_MIN;
    
    return result;
}

} // namespace cc_dsp

