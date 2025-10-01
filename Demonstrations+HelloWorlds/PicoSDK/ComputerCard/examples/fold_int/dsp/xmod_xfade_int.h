#pragma once
#include <cstdint>
#include "fixedpoint_int.h"

namespace cc_dsp {

// Linear crossfade: out = x1*(1-p) + x2*p, p in Q15 [0..1]
inline int32_t process_xfade_q15(int32_t x1_q15,
                                 int32_t x2_q15,
                                 int32_t p_q15) {
    if (p_q15 < 0) p_q15 = 0; else if (p_q15 > Q15_ONE) p_q15 = Q15_ONE;
    int32_t one_minus_p = Q15_ONE - p_q15;
    int32_t a = static_cast<int32_t>((static_cast<int64_t>(x1_q15) * one_minus_p) >> 15);
    int32_t b = static_cast<int32_t>((static_cast<int64_t>(x2_q15) * p_q15) >> 15);
    int32_t y = a + b;
    if (y > Q15_MAX) y = Q15_MAX; else if (y < Q15_MIN) y = Q15_MIN;
    return y;
}

} // namespace cc_dsp








