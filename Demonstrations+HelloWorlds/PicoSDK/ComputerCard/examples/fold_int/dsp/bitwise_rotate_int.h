#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// Bit rotation cross-modulation:
// Rotate bits of x1 by amount determined by x2 and parameter
inline int32_t process_rotate_q15(int32_t x1_q15, int32_t x2_q15, int32_t parameter_q15) {
    // Convert Q15 to uint16 for rotation
    int16_t x1_short = static_cast<int16_t>(x1_q15 > 32767 ? 32767 : (x1_q15 < -32768 ? -32768 : x1_q15));
    uint16_t x1_u = static_cast<uint16_t>(x1_short);
    
    // Rotation amount: combine x2 and parameter
    // Use lower 4 bits of (x2 >> 11) + (parameter >> 12) for rotation (0-15 bits)
    int32_t rot_source = (x2_q15 >> 11) + (parameter_q15 >> 12);
    uint32_t rotation = static_cast<uint32_t>(rot_source) & 0x0F;
    
    // Rotate left
    uint16_t rotated = static_cast<uint16_t>((x1_u << rotation) | (x1_u >> (16 - rotation)));
    
    // Convert back to signed
    int32_t mod_q15 = static_cast<int32_t>(static_cast<int16_t>(rotated));
    
    // Blend with original: original + (rotated - original) * parameter * 0.5
    int32_t diff = mod_q15 - x1_q15;
    int32_t half_param = parameter_q15 >> 1;
    int32_t param_diff = mul_q15(diff, half_param);
    int32_t result = x1_q15 + param_diff;
    
    if (result > Q15_MAX) result = Q15_MAX;
    if (result < Q15_MIN) result = Q15_MIN;
    
    return result;
}

} // namespace cc_dsp

