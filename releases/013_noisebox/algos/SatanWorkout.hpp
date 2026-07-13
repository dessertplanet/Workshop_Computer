#pragma once

#include <cstdint>
#include <cmath>
#include "dsp/WaveformOsc.hpp"
#include "dsp/WhiteNoise.hpp"
#include "MicroVerbInt.hpp"

// Port of Noise Plethora P_satanWorkout using available DSP blocks only.
// Topology (approximation of the Teensy patch-cord graph):
//   pink(noise) -> PWM (pulse width modulation) -> MicroVerb -> out
// Controls:
//   - k1 (0..4095): PWM oscillator frequency = 8 + pow(k1/4095, 2) * 6000 Hz
//   - k2 (0..4095): MicroVerb roomsize ≈ clamp(0.001 + 4*k2, 0..1)
// Notes:
//   - We approximate pink noise by low-passing white noise with a 1-pole IIR
//     implemented in fixed-point as recommended in README (integer bias).
//   - Reverb here uses dsp::MicroVerbMonoInt. We set wet=1 and dry=0. Damping kept
//     moderately low for a bright sound.
class SatanWorkoutAlgo {
public:
    SatanWorkoutAlgo()
    {
        // PWM oscillator setup
        pwm_.setSampleRate(48000.0f);
        pwm_.setShape(WaveformOscillator::Shape::Square); // pulse equivalent
        pwm_.setAmplitudeQ12(4095);
        pwm_.setPulseWidthQ15(16384); // 50%
        pwm_.setFrequencyHz(8.0f);

        // Reverb setup: full wet, no dry
        verb_.setWet(1.0f);
        verb_.setDry(0.0f);
        verb_.setDamp(0.2f);   // moderately bright
        verb_.setRoomSize(0.2f);
        verb_.setPredelayMs(2.0f, 48000.0f);

        // Noise/pink init
        noise_.init(0x12345u);
        pinkState_q19_() = 0; // force zeroed
    }

    // Generate one 12-bit sample. k1/k2: 0..4095
    inline int32_t process(int32_t k1_0_to_4095, int32_t k2_0_to_4095)
    {
        // Clamp controls
        if (k1_0_to_4095 < 0) k1_0_to_4095 = 0; else if (k1_0_to_4095 > 4095) k1_0_to_4095 = 4095;
        if (k2_0_to_4095 < 0) k2_0_to_4095 = 0; else if (k2_0_to_4095 > 4095) k2_0_to_4095 = 4095;

        // Control-rate updates (every 64 samples)
        if ((ctrlCounter_++ & 0x3F) == 0)
        {
            const float k1 = static_cast<float>(k1_0_to_4095) * (1.0f / 4095.0f);
            const float pitch1 = k1 * k1; // pow2 mapping
            const float f_hz = 8.0f + pitch1 * 6000.0f;
            pwm_.setFrequencyHz(f_hz);

            const float k2 = static_cast<float>(k2_0_to_4095) * (1.0f / 4095.0f);
            float room = 0.001f + 4.0f * k2; // as per original; clamp to [0..1]
            if (room < 0.0f) room = 0.0f; if (room > 1.0f) room = 1.0f;
            verb_.setRoomSize(room);
        }

        // Pink-ish modulation source from low-passed white noise (fixed-point)
        // White in: 12-bit signed [-2048..2047]
        int32_t w12 = static_cast<int32_t>(noise_.nextSample(4095));

        // One-pole LPF in Q19 accumulator domain for precision
        // y = a*y + (1-a)*x, with x pre-amplified by 2^7 (see README section on IIRs)
        // a_Q12 near 0.990 for slow PWM drift
        static constexpr int32_t a_Q12 = 4050;          // ~0.9897
        static constexpr int32_t one_Q12 = 4096;
        const int32_t x_q19 = (w12 << 7);               // promote to ~Q19
        int32_t y_q19 = pinkState_q19_();               // previous state
        // y = (a*y + (1-a)*x) >> 12  (keep in Q19)
        int64_t acc = (static_cast<int64_t>(a_Q12) * y_q19)
                    + (static_cast<int64_t>(one_Q12 - a_Q12) * x_q19);
        y_q19 = static_cast<int32_t>(acc >> 12);
        pinkState_q19_() = y_q19;

        // Back to ~12-bit domain
        int32_t pink12 = (y_q19 >> 7);                  // ~-2048..2047
        if (pink12 < -2048) pink12 = -2048;             // safety
        if (pink12 >  2047) pink12 =  2047;

        // Map pink12 -> PWM pulse width around 50%
        // width_q15 = 0.5 + depth * pink_norm, with pink_norm in [-1,1]
        const int32_t pink_q15 = (pink12 << 4);         // Q15 ~[-32768..32752]
        static constexpr int32_t half_q15 = 16384;      // 0.5 in Q15
        static constexpr int32_t depth_q15 = 9830;      // ~0.3 depth
        // mul_q15(pink_q15, depth_q15) ≈ Q15
        int32_t mod_q15 = mul_q15_(pink_q15, depth_q15);
        int32_t width_q15 = half_q15 + mod_q15;
        // clamp to ~[0.03..0.97] to avoid too-narrow pulses
        if (width_q15 < 983) width_q15 = 983;           // ~0.03
        if (width_q15 > 31805) width_q15 = 31805;       // ~0.97
        pwm_.setPulseWidthQ15(static_cast<uint16_t>(width_q15));

        // Generate one PWM sample and send through MicroVerb (mono)
        const int16_t dry = pwm_.nextSample();
        int16_t wet = verb_.process(dry);

        // Mono output
        int32_t mono = static_cast<int32_t>(wet * 8.0f);
        if (mono < -2048) mono = -2048; if (mono > 2047) mono = 2047;
        return mono;
    }

private:
    // Minimal Q15 multiply with rounding (same convention as dsp::FreeverbInt)
    static inline int32_t mul_q15_(int32_t a, int32_t b)
    {
        int64_t p = static_cast<int64_t>(a) * static_cast<int64_t>(b);
        int64_t adj = (p >= 0) ? (1ll << 14) : ((1ll << 14) - 1);
        return static_cast<int32_t>((p + adj) >> 15);
    }

    // Accessor to ensure a single definition and avoid static init order issues
    static inline int32_t& pinkState_q19_()
    {
        static int32_t s = 0;
        return s;
    }

    WaveformOscillator pwm_;
    WhiteNoise noise_;
    dsp::MicroVerbMonoInt verb_;
    uint32_t ctrlCounter_ = 0;
};


