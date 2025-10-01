#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// Integer Q15 bitcrusher adapted from Warps' ALGORITHM_BITCRUSHER
// p1: sets the crush amount (squared response across ~37 steps)
// p2: selects operation blend among [sum, bitwise OR, bitwise XOR, dynamic shift]

inline int16_t lerp_int16(int16_t a, int16_t b, int32_t frac_q15) {
    int32_t diff = static_cast<int32_t>(b) - static_cast<int32_t>(a);
    int32_t incr = static_cast<int32_t>(a) + static_cast<int32_t>((static_cast<int64_t>(diff) * frac_q15) >> 15);
    if (incr > 32767) incr = 32767; else if (incr < -32768) incr = -32768;
    return static_cast<int16_t>(incr);
}

inline int32_t process_bitcrusher_q15(int32_t x1_q15,
                                      int32_t x2_q15,
                                      int32_t p1_q15,
                                      int32_t p2_q15) {
    // Clamp source to int16 range
    int16_t s1 = static_cast<int16_t>(x1_q15 > Q15_MAX ? Q15_MAX : (x1_q15 < Q15_MIN ? Q15_MIN : x1_q15));
    int16_t s2 = static_cast<int16_t>(x2_q15 > Q15_MAX ? Q15_MAX : (x2_q15 < Q15_MIN ? Q15_MIN : x2_q15));

    // z = (p1^2) * steps, steps = 37
    const int steps = 37;
    int32_t p1_sq_q15 = static_cast<int32_t>((static_cast<int64_t>(p1_q15) * p1_q15) >> 15);
    int64_t z_q15 = static_cast<int64_t>(p1_sq_q15) * steps; // Q15 scaled by steps
    int32_t z_int = static_cast<int32_t>(z_q15 >> 15);       // 0..36
    if (z_int < 0) z_int = 0; else if (z_int >= steps) z_int = steps - 1;
    int32_t z_frac = static_cast<int32_t>(z_q15 & 0x7FFF);   // Q15 fractional

    // Build two adjacent masks in Q15 then cast to int16
    int32_t mask1_q15 = static_cast<int32_t>((static_cast<int64_t>(z_int) * Q15_ONE) / steps);
    int32_t mask2_q15 = static_cast<int32_t>((static_cast<int64_t>(z_int + 1) * Q15_ONE) / steps);
    int16_t mask1 = static_cast<int16_t>(mask1_q15 > Q15_MAX ? Q15_MAX : (mask1_q15 < Q15_MIN ? Q15_MIN : mask1_q15));
    int16_t mask2 = static_cast<int16_t>(mask2_q15 > Q15_MAX ? Q15_MAX : (mask2_q15 < Q15_MIN ? Q15_MIN : mask2_q15));

    // Apply OR mask and interpolate between adjacent masks
    int16_t s1_m1 = static_cast<int16_t>(s1 | mask1);
    int16_t s1_m2 = static_cast<int16_t>(s1 | mask2);
    int16_t s1_mod = lerp_int16(s1_m1, s1_m2, z_frac);

    int16_t s2_m1 = static_cast<int16_t>(s2 | mask1);
    int16_t s2_m2 = static_cast<int16_t>(s2 | mask2);
    int16_t s2_mod = lerp_int16(s2_m1, s2_m2, z_frac);

    // Prepare ops in Q15
    int32_t op0 = static_cast<int32_t>(s1_mod) + static_cast<int32_t>(s2_mod); // sum, may overflow int16
    // Clamp to int16 then convert to Q15
    if (op0 > 32767) op0 = 32767; else if (op0 < -32768) op0 = -32768;
    int32_t op0_q15 = op0;

    int16_t op1_s = static_cast<int16_t>(s1_mod | s2_mod);
    int32_t op1_q15 = static_cast<int32_t>(op1_s);

    int16_t op2_s = static_cast<int16_t>(s1_mod ^ s2_mod);
    int32_t op2_q15 = static_cast<int32_t>(op2_s);

    int32_t shift = static_cast<int32_t>(s2_mod) >> 12; // -8..+7 roughly
    if (shift < 0) {
        int16_t op3_s = static_cast<int16_t>(static_cast<int16_t>(s1_mod) >> (-shift));
        int32_t op3_q15 = static_cast<int32_t>(op3_s);
        // interpolate over p2 across 4 ops
        int32_t x_q15 = p2_q15 * 3; // 0..3
        int idx = x_q15 >> 15; if (idx < 0) idx = 0; else if (idx > 2) idx = 2;
        int32_t frac = x_q15 & 0x7FFF;
        int32_t ops[4] = { op0_q15, op1_q15, op2_q15, op3_q15 };
        int32_t a = ops[idx];
        int32_t b = ops[idx + 1];
        return a + static_cast<int32_t>((static_cast<int64_t>(b - a) * frac) >> 15);
    } else {
        int16_t op3_s = static_cast<int16_t>(static_cast<int16_t>(s1_mod) << shift);
        int32_t op3_q15 = static_cast<int32_t>(op3_s);
        int32_t x_q15 = p2_q15 * 3;
        int idx = x_q15 >> 15; if (idx < 0) idx = 0; else if (idx > 2) idx = 2;
        int32_t frac = x_q15 & 0x7FFF;
        int32_t ops[4] = { op0_q15, op1_q15, op2_q15, op3_q15 };
        int32_t a = ops[idx];
        int32_t b = ops[idx + 1];
        return a + static_cast<int32_t>((static_cast<int64_t>(b - a) * frac) >> 15);
    }
}

} // namespace cc_dsp








