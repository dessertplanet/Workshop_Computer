#pragma once

#include <cstdint>
#include <cmath>
#include "dsp/WaveformOsc.hpp"
#include "dsp/StateVariableFilterInt.hpp"
#include "dsp/SVF_LUT_512.h"

class WhoKnowsAlgo {
public:
    static constexpr int kNumFilters = 4;

    WhoKnowsAlgo()
    {
        // Pulse source
        source_.setSampleRate(48000.0f);
        source_.setShape(WaveformOscillator::Shape::Square);
        source_.setAmplitudeQ12(4095);
        source_.setPulseWidthQ15(static_cast<uint16_t>(0.1f * 32768.0f + 0.5f));
        source_.setFrequencyHz(20.0f);

        // LFOs (triangle)
        const float lfoHz[kNumFilters] = { 21.0f, 70.0f, 90.0f, 77.0f };
        for (int i = 0; i < kNumFilters; ++i)
        {
            lfo_[i].setSampleRate(48000.0f);
            lfo_[i].setShape(WaveformOscillator::Shape::Triangle);
            lfo_[i].setAmplitudeQ12(4095);
            lfo_[i].setFrequencyHz(lfoHz[i]);
            f_q15_cur_[i] = knobNormToFq15_(baseKnobNorm_);
        }

        // Filters (bandpass, Q≈7 -> use Q9 LUT option)
        for (int i = 0; i < kNumFilters; ++i)
        {
            svf_[i].begin();
            svf_[i].setMode(StateVariableFilterIntLUT::Mode::Bandpass);
            svf_[i].setResonance(StateVariableFilterIntLUT::Resonance::Q9);
            svf_[i].setCutoffHz(baseCutoffHz_);
        }

        // Cache base knob pos (Hz -> LUT domain)
        baseKnobNorm_ = hzToKnobNorm_(baseCutoffHz_);
    }

    inline int32_t process(int32_t k1_0_to_4095, int32_t k2_0_to_4095)
    {
        if (k1_0_to_4095 < 0) k1_0_to_4095 = 0; else if (k1_0_to_4095 > 4095) k1_0_to_4095 = 4095;
        if (k2_0_to_4095 < 0) k2_0_to_4095 = 0; else if (k2_0_to_4095 > 4095) k2_0_to_4095 = 4095;

        // Control-rate: source frequency and filter octave span mapping
        if ((ctrlCounter_++ & (ctrlDiv_ - 1)) == 0)
        {
            const float k1 = static_cast<float>(k1_0_to_4095) * (1.0f / 4095.0f);
            const float pitch1 = k1 * k1;
            const float srcHz = 15.0f + pitch1 * 500.0f;
            source_.setFrequencyHz(srcHz);

            const float k2 = static_cast<float>(k2_0_to_4095) * (1.0f / 4095.0f);
            octaveSpan_ = 0.3f + 6.0f * k2;

            const float knobOctaveScale = octaveSpan_ * invTotalOctaves_;
            for (int i = 0; i < kNumFilters; ++i)
            {
                const int16_t l = lfo_[i].nextSample();
                const float lfoNorm = static_cast<float>(l) * (1.0f / 2048.0f);
                float knobNorm = baseKnobNorm_ + (lfoNorm * knobOctaveScale);
                if (knobNorm < 0.0f) knobNorm = 0.0f;
                if (knobNorm > 1.0f) knobNorm = 1.0f;
                f_q15_cur_[i] = knobNormToFq15_(knobNorm);
            }
        }

        const int16_t src = source_.nextSample();

        int32_t mix = 0;
        for (int i = 0; i < kNumFilters; ++i)
        {
            const int16_t y = svf_[i].processWithFMod(src, f_q15_cur_[i]);
            mix += y;
        }

        // Approx mixer gains ~0.8 each: scale combined sum by ~0.75
        mix = (mix * 3) >> 2;
        if (mix < -2048) mix = -2048;
        if (mix >  2047) mix =  2047;
        return mix;
    }

private:
    // Config and control cadence
    static constexpr uint32_t ctrlDiv_ = 8;
    uint32_t ctrlCounter_ = 0;

    // Base cutoff and mapping helpers
    float baseCutoffHz_ = 1000.0f;
    float baseKnobNorm_ = 0.0f;
    float octaveSpan_ = 0.3f;
    static constexpr float totalOctaves_ = 8.643856189774724f; // log2(8000/20)
    static constexpr float invTotalOctaves_ = 1.0f / totalOctaves_;

    static inline float hzToKnobNorm_(float hz)
    {
        if (hz < 20.0f) hz = 20.0f;
        if (hz > 8000.0f) hz = 8000.0f;
        const float num = std::log(hz / 20.0f);
        const float den = std::log(8000.0f / 20.0f);
        float t = (den > 0.0f) ? (num / den) : 0.5f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        return t;
    }

    static inline uint16_t knobNormToFq15_(float knobNorm)
    {
        if (knobNorm < 0.0f) knobNorm = 0.0f;
        if (knobNorm > 1.0f) knobNorm = 1.0f;
        const float pos = knobNorm * float(F_LUT_SIZE - 1);
        int idx = int(pos);
        float fracf = pos - float(idx);
        if (idx < 0) { idx = 0; fracf = 0.0f; }
        if (idx >= F_LUT_SIZE - 1) { idx = F_LUT_SIZE - 2; fracf = 1.0f; }
        const uint16_t a = F_LUT_512[idx];
        const uint16_t b = F_LUT_512[idx + 1];
        const uint16_t frac = (uint16_t)std::lrint(fracf * 65535.0f);
        const uint32_t diff = uint32_t(b) - uint32_t(a);
        return uint16_t(uint32_t(a) + ((diff * uint32_t(frac)) >> 16));
    }

    WaveformOscillator        source_;
    WaveformOscillator        lfo_[kNumFilters];
    StateVariableFilterIntLUT svf_[kNumFilters];
    uint16_t                  f_q15_cur_[kNumFilters];
};


