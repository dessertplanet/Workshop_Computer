#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// Digital ring modulation (Q1.15) from Warps' ALGORITHM_DIGITAL_RING_MODULATION:
// ring = 4.0 * x_1 * x_2 * (1.0 + parameter * 8.0);
// return ring / (1.0 + |ring|);
inline int32_t process_digital_ring_q15(int32_t x1_q15,
                                        int32_t x2_q15,
                                        int32_t parameter_q15) {
    // ring = 4 * x1 * x2 * (1 + parameter * 8)
    int32_t prod = mul_q15(x1_q15, x2_q15); // Q15
    int32_t gain_q15 = Q15_ONE + (parameter_q15 << 3); // 1 + parameter * 8 in Q15
    int32_t ring = mul_q15(prod, gain_q15); // Q15
    ring = ring << 2; // multiply by 4
    
    // Soft limit: ring / (1 + |ring|)
    int32_t abs_ring = ring >= 0 ? ring : -ring;
    int32_t denom = Q15_ONE + abs_ring;
    int64_t num = static_cast<int64_t>(ring) << 15;
    if (denom == 0) return 0;
    int32_t result = static_cast<int32_t>(num / denom);
    
    return result;
}

} // namespace cc_dsp


