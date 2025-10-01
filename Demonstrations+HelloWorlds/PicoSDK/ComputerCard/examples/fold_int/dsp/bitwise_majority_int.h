#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// Majority logic cross-modulation:
// For each bit position, output 1 if majority of {x1, x2, threshold} are 1
inline int32_t process_majority_q15(int32_t x1_q15, int32_t x2_q15, int32_t parameter_q15) {
    // Convert Q15 to int16
    int16_t x1_short = static_cast<int16_t>(x1_q15 > 32767 ? 32767 : (x1_q15 < -32768 ? -32768 : x1_q15));
    int16_t x2_short = static_cast<int16_t>(x2_q15 > 32767 ? 32767 : (x2_q15 < -32768 ? -32768 : x2_q15));
    
    // Create threshold pattern based on parameter
    // parameter controls the "voting bias"
    int16_t threshold = static_cast<int16_t>(parameter_q15 > 32767 ? 32767 : (parameter_q15 < -32768 ? -32768 : parameter_q15));
    
    // Majority logic: bit is 1 if at least 2 of {x1, x2, threshold} have that bit as 1
    uint16_t x1_u = static_cast<uint16_t>(x1_short);
    uint16_t x2_u = static_cast<uint16_t>(x2_short);
    uint16_t th_u = static_cast<uint16_t>(threshold);
    
    // Majority: (a AND b) OR (a AND c) OR (b AND c)
    uint16_t majority = (x1_u & x2_u) | (x1_u & th_u) | (x2_u & th_u);
    
    int32_t result = static_cast<int32_t>(static_cast<int16_t>(majority));
    
    if (result > Q15_MAX) result = Q15_MAX;
    if (result < Q15_MIN) result = Q15_MIN;
    
    return result;
}

} // namespace cc_dsp

