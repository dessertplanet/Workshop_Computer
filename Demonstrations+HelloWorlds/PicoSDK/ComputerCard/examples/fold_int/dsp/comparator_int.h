#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// Comparator algorithm (Q1.15) from Warps' ALGORITHM_COMPARATOR
// Morphs between 4 different comparison modes based on parameter
inline int32_t process_comparator_q15(int32_t mod_q15, int32_t car_q15, int32_t parameter_q15) {
    // x = parameter * 2.995, then split into integral and fractional parts
    // In Q15: 2.995 * Q15_ONE = 98139
    constexpr int32_t Q15_2P995 = 98139;
    int32_t x_scaled = mul_q15(parameter_q15, Q15_2P995); // Q15
    
    // Get integral part (0, 1, 2, or 3)
    int32_t x_integral = x_scaled >> 15; // Divide by Q15_ONE to get integer
    if (x_integral > 2) x_integral = 2; // Clamp to 0-2 range
    
    // Get fractional part for interpolation
    int32_t x_fractional = x_scaled - (x_integral << 15); // remainder in Q15
    
    // Compute the 4 comparison modes
    int32_t direct = (mod_q15 < car_q15) ? mod_q15 : car_q15;
    
    int32_t abs_mod = mod_q15 >= 0 ? mod_q15 : -mod_q15;
    int32_t abs_car = car_q15 >= 0 ? car_q15 : -car_q15;
    
    int32_t window = (abs_mod > abs_car) ? mod_q15 : car_q15;
    int32_t window_2 = (abs_mod > abs_car) ? abs_mod : -abs_car;
    
    constexpr int32_t Q15_THRESHOLD = 1638; // 0.05 in Q15
    int32_t threshold = (car_q15 > Q15_THRESHOLD) ? car_q15 : mod_q15;
    
    // Select between modes based on x_integral and interpolate
    int32_t sequence[4] = { direct, threshold, window, window_2 };
    int32_t a = sequence[x_integral];
    int32_t b = sequence[x_integral + 1];
    
    // Linear interpolation: a + (b - a) * x_fractional
    int32_t diff = b - a;
    int32_t interp = mul_q15(diff, x_fractional);
    int32_t result = a + interp;
    
    return result;
}

} // namespace cc_dsp


