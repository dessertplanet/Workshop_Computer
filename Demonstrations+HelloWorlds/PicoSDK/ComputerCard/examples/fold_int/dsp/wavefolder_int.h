#pragma once
#include <cstdint>
#include "fixedpoint_int.h"
#include "ring_mod_analog_int.h"
#include "ring_mod_digital_int.h"
#include "ring_mod_blend_int.h"
#include "xmod_xor_int.h"
#include "comparator_int.h"
#include "comparator8_int.h"
#include "chebyshev_int.h"
#include "comparator_cheby_int.h"
#include "bitcrusher_int.h"
#include "xmod_xfade_int.h"
#include "freq_shifter_int.h"
#include "bitwise_and_int.h"
#include "bitwise_or_int.h"
#include "bitwise_nand_int.h"
#include "bitwise_rotate_int.h"
#include "bitwise_interleave_int.h"
#include "bitwise_mask_int.h"
#include "bitwise_xor_rotate_int.h"
#include "bitwise_majority_int.h"

namespace cc_dsp {

// Q1.15 fixed-point helpers and fold algorithm

constexpr int32_t Q15_OFFSET_0P02 = 655;     // 0.02 in Q15

inline int32_t fold_reflect_q15(int32_t x) {
    while (x > Q15_MAX) {
        x = (Q15_MAX << 1) - x;
    }
    while (x < Q15_MIN) {
        x = (Q15_MIN * 2) - x;
    }
    return x;
}

// conversions now provided by fixedpoint_int.h

// Algorithms we can select between via the main knob
enum class Algorithm : uint8_t {
    Fold = 0,
    AnalogRing = 1,
    DigitalRing = 2,
    RingBlend = 3,
    Xor = 4,
    Comparator = 5,
    Comparator8 = 6,
    Chebyshev = 7,
    ComparatorChebyshev = 8,
    Bitcrusher = 9,
    Xfade = 10,
    FreqShifter = 11,
    BitwiseAnd = 12,
    BitwiseOr = 13,
    BitwiseNand = 14,
    BitwiseRotate = 15,
    BitwiseInterleave = 16,
    BitwiseMask = 17,
    BitwiseXorRotate = 18,
    BitwiseMajority = 19,
    Count
};

// Two-parameter fold (p1, p2), adapted from Warps' ALGORITHM_FOLD
inline int32_t process_fold_q15(int32_t x1_q15, int32_t x2_q15,
                                int32_t p1_q15, int32_t p2_q15) {
    // sum = x1 + x2 + 0.25 * x1 * x2
    int32_t sum = x1_q15 + x2_q15;
    int32_t prod_quarter_q15 = static_cast<int32_t>((static_cast<int64_t>(x1_q15) * x2_q15) >> 17);
    sum += prod_quarter_q15;

    // gain = 0.02 + p1
    int32_t gain_q15 = p1_q15 + Q15_OFFSET_0P02;
    if (gain_q15 < 0) gain_q15 = 0;
    int32_t driven = static_cast<int32_t>((static_cast<int64_t>(sum) * gain_q15) >> 15);

    // add offset
    int32_t driven_off = driven + p2_q15;

    // reflection folding as LUT approximation
    return fold_reflect_q15(driven_off);
}

inline int32_t process_algorithm_q15(Algorithm algo,
                                     int32_t x1_q15, int32_t x2_q15,
                                     int32_t p1_q15, int32_t p2_q15) {
    switch (algo) {
        case Algorithm::Fold:
            return process_fold_q15(x1_q15, x2_q15, p1_q15, p2_q15);
        case Algorithm::AnalogRing:
            // Use x1 as modulator, x2 as carrier; parameter from p1
            return process_analog_ring_q15(x1_q15, x2_q15, p1_q15);
        case Algorithm::DigitalRing:
            // Digital ring uses both inputs; parameter from p1
            return process_digital_ring_q15(x1_q15, x2_q15, p1_q15);
        case Algorithm::RingBlend:
            // Blend: p1 blends, p2 drives sub-algorithms
            return process_ring_blend_q15(x1_q15, x2_q15, p1_q15, p2_q15);
        case Algorithm::Xor:
            return process_xor_q15(x1_q15, x2_q15, p1_q15);
        case Algorithm::Comparator:
            return process_comparator_q15(x1_q15, x2_q15, p1_q15);
        case Algorithm::Comparator8:
            return process_comparator8_q15(x1_q15, x2_q15, p1_q15);
        case Algorithm::Chebyshev:
            return process_chebyshev_q15(x1_q15, x2_q15, p1_q15, p2_q15);
        case Algorithm::ComparatorChebyshev:
            return process_comparator_cheby_q15(x1_q15, x2_q15, p1_q15, p2_q15);
        case Algorithm::Bitcrusher:
            return process_bitcrusher_q15(x1_q15, x2_q15, p1_q15, p2_q15);
        case Algorithm::Xfade:
            return process_xfade_q15(x1_q15, x2_q15, p1_q15);
        case Algorithm::FreqShifter: {
            static FreqShifterState fs;
            return process_freq_shifter_q15(fs, x1_q15, x2_q15, p1_q15, p2_q15);
        }
        case Algorithm::BitwiseAnd:
            return process_and_q15(x1_q15, x2_q15, p1_q15);
        case Algorithm::BitwiseXorRotate:
            return process_xor_rotate_q15(x1_q15, x2_q15, p1_q15);
        case Algorithm::BitwiseMajority:
            return process_majority_q15(x1_q15, x2_q15, p1_q15);
        default:
            return process_fold_q15(x1_q15, x2_q15, p1_q15, p2_q15);
    }
}

} // namespace cc_dsp


