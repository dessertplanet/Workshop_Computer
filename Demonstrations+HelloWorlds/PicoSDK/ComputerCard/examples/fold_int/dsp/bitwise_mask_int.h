#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// Bit mask modulation:
// Create moving bit mask to crossfade between x1 and x2 in bit domain
inline int32_t process_bitmask_q15(int32_t x1_q15, int32_t x2_q15, int32_t parameter_q15) {
    // Convert Q15 to int16
    int16_t x1_short = static_cast<int16_t>(x1_q15 > 32767 ? 32767 : (x1_q15 < -32768 ? -32768 : x1_q15));
    int16_t x2_short = static_cast<int16_t>(x2_q15 > 32767 ? 32767 : (x2_q15 < -32768 ? -32768 : x2_q15));
    
    // Create a sliding bit mask based on parameter
    // parameter = 0: all bits from x1 (mask = 0xFFFF)
    // parameter = max: all bits from x2 (mask = 0x0000)
    uint32_t p_val = static_cast<uint32_t>(parameter_q15 < 0 ? 0 : parameter_q15);
    
    // Number of bits to keep from x1 (16 down to 0)
    uint32_t bits_from_x1 = 16 - (p_val >> 11); // 0-32767 -> 16-0 bits
    
    // Create mask: high bits from x1, low bits from x2
    uint16_t mask = (bits_from_x1 >= 16) ? 0xFFFF : ((bits_from_x1 == 0) ? 0x0000 : (0xFFFF << (16 - bits_from_x1)));
    
    // Apply mask
    uint16_t x1_u = static_cast<uint16_t>(x1_short);
    uint16_t x2_u = static_cast<uint16_t>(x2_short);
    uint16_t result_u = (x1_u & mask) | (x2_u & ~mask);
    
    int32_t result = static_cast<int32_t>(static_cast<int16_t>(result_u));
    
    if (result > Q15_MAX) result = Q15_MAX;
    if (result < Q15_MIN) result = Q15_MIN;
    
    return result;
}

} // namespace cc_dsp

