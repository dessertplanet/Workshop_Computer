#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// Bit interleaving cross-modulation:
// Interleave bits from x1 and x2 based on parameter-controlled mask
inline int32_t process_interleave_q15(int32_t x1_q15, int32_t x2_q15, int32_t parameter_q15) {
    // Convert Q15 to int16
    int16_t x1_short = static_cast<int16_t>(x1_q15 > 32767 ? 32767 : (x1_q15 < -32768 ? -32768 : x1_q15));
    int16_t x2_short = static_cast<int16_t>(x2_q15 > 32767 ? 32767 : (x2_q15 < -32768 ? -32768 : x2_q15));
    
    // Create alternating bit mask based on parameter
    // parameter controls the interleave pattern: 0 = all x1, max = alternating, full = all x2
    uint32_t p_val = static_cast<uint32_t>(parameter_q15 < 0 ? 0 : parameter_q15);
    
    // Generate mask: alternate bits with frequency based on parameter
    // At p=0: mask = 0xFFFF (all x1)
    // At p=mid: mask = 0xAAAA (alternating)  
    // At p=max: mask = 0x0000 (all x2)
    uint16_t mask;
    if (p_val < 16384) {
        // Low range: transition from 0xFFFF to 0xAAAA
        mask = 0xAAAA | (0x5555 >> (p_val >> 12));
    } else {
        // High range: transition from 0xAAAA to 0x0000
        mask = 0xAAAA & ~(0x5555 << ((p_val - 16384) >> 12));
    }
    
    // Interleave: use mask to select bits
    uint16_t x1_u = static_cast<uint16_t>(x1_short);
    uint16_t x2_u = static_cast<uint16_t>(x2_short);
    uint16_t result_u = (x1_u & mask) | (x2_u & ~mask);
    
    int32_t result = static_cast<int32_t>(static_cast<int16_t>(result_u));
    
    if (result > Q15_MAX) result = Q15_MAX;
    if (result < Q15_MIN) result = Q15_MIN;
    
    return result;
}

} // namespace cc_dsp

