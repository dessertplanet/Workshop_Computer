// Port scaffold for Noise Plethora P_existencelsPain
// Topology (as in the original Teensy plugin):
//   - One Sample & Hold (noise-like) source feeds N parallel state-variable filters
//   - N slow triangle LFOs modulate each filter's cutoff in octaves
//   - Bandpass outputs from the N filters are mixed to the final output
// Controls:
//   - k1 (0..4095): drives the S&H clock frequency (pitch1 = (k1/4095)^2)
//   - k2 (0..4095): sets octave span of cutoff modulation per filter (octaves = k2*3 + 0.3)
// Notes:
//   - Only DSP building blocks from ../dsp are used (WaveformOscillator, StateVariableFilterIntLUT)
//   - Follow init patterns used by other algos in this folder

#pragma once

// ====== Includes & dependencies ======
#include <cstdint>
#include <cmath>
#include "dsp/WaveformOsc.hpp"
#include "dsp/StateVariableFilterInt.hpp"
#include "dsp/SVF_LUT_512.h" // direct LUT access for fast knob->f mapping

// ====== Algorithm class ======
class ExistencelsPain {
public:
    // Number of parallel LFO/filter mod paths
    static constexpr int kNumMods = 4;

    // ----- Construction & init -----
    // Mirrors other algos: set sample rates, shapes, default amplitudes/frequencies
    ExistencelsPain()
    {
        // Sample & Hold source (noise-like)
        source_.setSampleRate(48000.0f);
        source_.setShape(WaveformOscillator::Shape::SampleHold);
        source_.setAmplitudeQ12(4095);
        source_.setFrequencyHz(5.0f); // will be driven by k1 mapping each sample

        // Triangle LFOs for cutoff modulation (init frequencies per original)
        // If kNumMods > 4, repeat/extend pattern; if < 4, use first N
        const float initLfoHzBase[4] = { 11.0f, 70.0f, 23.0f, 0.01f };
        for (int i = 0; i < kNumMods; ++i)
        {
            lfo_[i].setSampleRate(48000.0f);
            lfo_[i].setShape(WaveformOscillator::Shape::Triangle);
            lfo_[i].setAmplitudeQ12(4095);
            // Use base pattern, wrap if more than 4
            lfo_[i].setFrequencyHz(initLfoHzBase[i % 4]);
        }

        // State-variable filters (bandpass outputs mixed)
        for (int i = 0; i < kNumMods; ++i)
        {
            svf_[i].begin();
            svf_[i].setMode(StateVariableFilterIntLUT::Mode::Bandpass);
            svf_[i].setResonance(StateVariableFilterIntLUT::Resonance::Q6); // maps loosely to resonance(5)
            // Base cutoff will be set per-sample from LFOs (octave modulation)
            svf_[i].setCutoffHz(baseCutoffHz_);
        }

        // Precompute base knob position once (avoid per-sample log)
        baseKnobNorm_ = hzToKnobNorm_(baseCutoffHz_);

        // Init control-rate caches
        for (int i = 0; i < kNumMods; ++i) {
            f_q15_cur_[i] = knobNormToFq15_(baseKnobNorm_);
            lfoHold_[i] = 0;
        }
    }

    // ----- Audio tick -----
    // Generate one 12-bit sample. k1/k2 are expected in 0..4095 (clamped internally).
    // This scaffold outlines steps; detailed math filled in in implementation step.
    inline int32_t process(int32_t k1_0_to_4095, int32_t k2_0_to_4095)
    {
        // 1) Clamp and normalize controls (0..1)
        if (k1_0_to_4095 < 0) k1_0_to_4095 = 0; else if (k1_0_to_4095 > 4095) k1_0_to_4095 = 4095;
        if (k2_0_to_4095 < 0) k2_0_to_4095 = 0; else if (k2_0_to_4095 > 4095) k2_0_to_4095 = 4095;
        const float k1 = static_cast<float>(k1_0_to_4095) * (1.0f / 4095.0f);
        const float k2 = static_cast<float>(k2_0_to_4095) * (1.0f / 4095.0f);

        // 2) Map k1 -> S&H clock frequency (per original: 50 + pitch^2 * 5000)
        //    pitch = k1^2 for musical feel
        const float pitch = k1 * k1;
        const float srcHz = 50.0f + pitch * 5000.0f;
        source_.setFrequencyHz(srcHz);

        // 3) Map k2 -> octave span for cutoff modulation (oct = 0.3 + 3*k2)
        const float octaveSpan = 0.3f + 3.0f * k2;

        // 4) Per-filter cutoff modulation from its LFO:
        //    We avoid per-sample log mapping by working in the LUT's normalized knob domain.
        //    The LUT spans 20..8000 Hz, i.e. totalOctaves = log2(8000/20).
        //    A delta of D octaves corresponds to D/totalOctaves in knob-normalized units.

        // Scale to convert octaves -> knob units (control-rate)
        // Update control-rate parameters every ctrlDiv_ samples
        if ((ctrlCounter_++ & (ctrlDiv_ - 1)) == 0)
        {
            const float knobOctaveScale = octaveSpan * invTotalOctaves_;
            for (int i = 0; i < kNumMods; ++i)
            {
                // Triangle LFO at control-rate; hold value between updates
                const int16_t l = lfo_[i].nextSample();
                lfoHold_[i] = l;
                const float lfoNorm = static_cast<float>(l) * (1.0f / 2048.0f);
                float knobNorm = baseKnobNorm_ + (lfoNorm * knobOctaveScale);
                if (knobNorm < 0.0f) knobNorm = 0.0f;
                if (knobNorm > 1.0f) knobNorm = 1.0f;
                f_q15_cur_[i] = knobNormToFq15_(knobNorm);
            }
            // Also update S&H frequency at control-rate
            const float pitch = k1 * k1;
            const float srcHz = 50.0f + pitch * 5000.0f;
            source_.setFrequencyHz(srcHz);
        }

        // Generate shared input once
        const int16_t src = source_.nextSample();

        int32_t mix = 0;
        for (int i = 0; i < kNumMods; ++i)
        {
            // Process bandpass using held cutoff f value in Q15
            int16_t y = svf_[i].processWithFMod(src, f_q15_cur_[i]);
            mix += y;
        }

        // Soft scale mixed output from N parallel filters
        if constexpr (kNumMods > 0) {
            mix = mix / kNumMods;
        }
        if (mix < -2048) mix = -2048;
        if (mix >  2047) mix =  2047;
        return mix;
    }

private:
    // ----- Configuration -----
    // Base cutoff (center frequency) before octave modulation
    float baseCutoffHz_ = 1000.0f; // loosely matches Teensy default when not explicitly set
    float baseKnobNorm_ = 0.0f;    // normalized 0..1 mapping of base cutoff into LUT domain
    // Control-rate update cadence (must be power of two for mask)
    static constexpr uint32_t ctrlDiv_ = 8; // update every 8 samples (~6 kHz)
    uint32_t ctrlCounter_ = 0;

    // ----- DSP blocks -----
    WaveformOscillator        source_;   // Sample & Hold noise-like source
    WaveformOscillator        lfo_[kNumMods];   // Triangle modulators for cutoff
    StateVariableFilterIntLUT svf_[kNumMods];   // Bandpass filters
    // Control-rate caches
    uint16_t f_q15_cur_[kNumMods];
    int16_t  lfoHold_[kNumMods];

    // ----- Helpers -----
    // Convert a 12-bit bipolar sample (−2048..+2047) to normalized float in [−1, +1]
    static inline float norm12(int16_t s)
    {
        // 2048 -> 1.0, -2048 -> approx -1.0
        return static_cast<float>(s) * (1.0f / 2048.0f);
    }

    // Precomputed constants
    static constexpr float totalOctaves_ = 8.643856189774724f; // log2(8000/20) = log2(400)
    static constexpr float invTotalOctaves_ = 1.0f / totalOctaves_;

    // Map Hz -> knob normalized 0..1 for the LUT domain
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

    // Map knobNorm in [0,1] directly to f_q15 via the LUT with linear interpolation
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
};

