#pragma once
#include <cstdint>
#include "fixedpoint_int.h"
#include "comparator_int.h"
#include "chebyshev_int.h"

namespace cc_dsp {

// Comparator + Chebyshev combo as in Warps' ALGORITHM_COMPARATOR_CHEBYSCHEV
// x = Comparator(mod, car, p1)
// y = Chebyshev(x, 0, 1, p2)  (we feed x as x1, zero x2; p1=1.0 to select higher degree fully)
// then scale by ~0.8
inline int32_t process_comparator_cheby_q15(int32_t mod_q15,
                                            int32_t car_q15,
                                            int32_t p1_q15,
                                            int32_t p2_q15) {
    int32_t comp = process_comparator_q15(mod_q15, car_q15, p1_q15);
    // For the Chebyshev stage: use comp as x1, 0 as x2; p1=1.0 so we are at higher degree side fully
    int32_t cheb = process_chebyshev_q15(comp, 0, Q15_ONE, p2_q15);
    // scale by 0.8
    int32_t out = mul_q15(cheb, Q15_0P8);
    return out;
}

} // namespace cc_dsp








