#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// Comparator8 algorithm (Q1.15) from Warps' ALGORITHM_COMPARATOR8
// 8 different comparison/combination modes
inline int32_t process_comparator8_q15(int32_t mod_q15, int32_t car_q15, int32_t parameter_q15) {
    // x = parameter * 6.995
    constexpr int32_t Q15_6P995 = 229230; // 6.995 * Q15_ONE
    int32_t x_scaled = mul_q15(parameter_q15, Q15_6P995);
    
    int32_t x_integral = x_scaled >> 15;
    if (x_integral > 6) x_integral = 6; // Clamp to 0-6 range
    
    int32_t x_fractional = x_scaled - (x_integral << 15);
    
    int32_t y_1, y_2;
    
    if (x_integral == 0) {
        y_1 = mod_q15 + car_q15;
        y_2 = mod_q15 - car_q15;
    } else if (x_integral == 1) {
        y_1 = mod_q15 - car_q15;
        y_2 = mul_q15(mod_q15, car_q15);
    } else if (x_integral == 2) {
        y_1 = mul_q15(mod_q15, car_q15);
        // min(mod, car)
        y_2 = (mod_q15 < car_q15) ? mod_q15 : car_q15;
    } else if (x_integral == 3) {
        y_1 = (mod_q15 < car_q15) ? mod_q15 : car_q15;
        // max(mod, car)
        y_2 = (mod_q15 > car_q15) ? mod_q15 : car_q15;
    } else if (x_integral == 4) {
        y_1 = (mod_q15 > car_q15) ? mod_q15 : car_q15;
        // min(|mod|, |car|) * sign(mod)
        int32_t abs_mod = mod_q15 >= 0 ? mod_q15 : -mod_q15;
        int32_t abs_car = car_q15 >= 0 ? car_q15 : -car_q15;
        int32_t min_abs = (abs_mod < abs_car) ? abs_mod : abs_car;
        y_2 = (mod_q15 >= 0) ? min_abs : -min_abs;
    } else if (x_integral == 5) {
        int32_t abs_mod = mod_q15 >= 0 ? mod_q15 : -mod_q15;
        int32_t abs_car = car_q15 >= 0 ? car_q15 : -car_q15;
        int32_t min_abs = (abs_mod < abs_car) ? abs_mod : abs_car;
        y_1 = (mod_q15 >= 0) ? min_abs : -min_abs;
        // max(|mod|, |car|) * sign(mod)
        int32_t max_abs = (abs_mod > abs_car) ? abs_mod : abs_car;
        y_2 = (mod_q15 >= 0) ? max_abs : -max_abs;
    } else { // x_integral == 6
        int32_t abs_mod = mod_q15 >= 0 ? mod_q15 : -mod_q15;
        int32_t abs_car = car_q15 >= 0 ? car_q15 : -car_q15;
        int32_t max_abs = (abs_mod > abs_car) ? abs_mod : abs_car;
        y_1 = (mod_q15 >= 0) ? max_abs : -max_abs;
        y_2 = mod_q15;
    }
    
    // Clamp y_1 and y_2
    if (y_1 > Q15_MAX) y_1 = Q15_MAX;
    if (y_1 < Q15_MIN) y_1 = Q15_MIN;
    if (y_2 > Q15_MAX) y_2 = Q15_MAX;
    if (y_2 < Q15_MIN) y_2 = Q15_MIN;
    
    // Interpolate between y_1 and y_2
    int32_t diff = y_2 - y_1;
    int32_t interp = mul_q15(diff, x_fractional);
    int32_t result = y_1 + interp;
    
    if (result > Q15_MAX) result = Q15_MAX;
    if (result < Q15_MIN) result = Q15_MIN;
    
    return result;
}

} // namespace cc_dsp


