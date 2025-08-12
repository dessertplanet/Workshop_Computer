#pragma once

#include <cstdint>
#include <cmath>
#include "dsp/WaveformOsc.hpp"
#include "dsp/WhiteNoise.hpp"

// Port of Noise Plethora P_Rwalk_ModWave:
// - Builds a 256-sample arbitrary waveform via a 2D random walk of 256 points.
// - A saw carrier cross-modulates an arbitrary oscillator (FM input).
// Controls:
//  - k1 (0..4095): pitch mapping for the carrier, f = 10 + 50 * (k1/4095)^2
//  - k2 (0..4095): output amplitude for the arbitrary oscillator (0..full)
// Notes:
//  - Random walk and table updates are spread across control ticks to avoid
//    long single-cycle work (incremental batch update).
//  - FM depth is set relative to the arbitrary oscillator base frequency and
//    clamped to keep the effective increment positive.
class RwalkModWaveAlgo {
public:
    RwalkModWaveAlgo()
    {
        // Set up oscillators
        carrier_.setSampleRate(48000.0f);
        carrier_.setShape(WaveformOscillator::Shape::Saw);
        carrier_.setAmplitudeQ12(4095);
        carrier_.setFrequencyHz(60.0f);

        mod_.setSampleRate(48000.0f);
        mod_.setShape(WaveformOscillator::Shape::Arbitrary);
        mod_.setArbitraryWaveform(waveTable_);
        mod_.setAmplitudeQ12(4095);
        mod_.setFrequencyHz(f_mod_base_); // fixed base frequency like original (250 Hz)

        // Precompute FM scaling in Q16.16 and clamp cap
        depth_q16_16_ = static_cast<int32_t>(static_cast<float>(f_mod_base_) * 0.4f * 65536.0f + 0.5f);
        base_q16_16_  = static_cast<int32_t>(static_cast<float>(f_mod_base_) * 65536.0f + 0.5f);
        fmCap_q16_16_ = static_cast<int32_t>(0.8f * static_cast<float>(base_q16_16_));

        // Initialize random walk state
        for (int i = 0; i < 256; ++i)
        {
            // Cheap random direction (8-way) and position in [-L, L]
            dir_[i] = static_cast<uint8_t>(rand12_() & 7u);
            x_[i] = randMinus1To1_() * L_;
            y_[i] = randMinus1To1_() * L_;
            waveTable_[i] = 0;
        }
        // Build initial waveform
        for (int i = 0; i < 256; ++i) { waveTable_[i] = 0; }
    }

    // k1_0_to_4095 and k2_0_to_4095 are nominally 0..4095 (12-bit)
    inline int32_t process(int32_t k1_0_to_4095, int32_t k2_0_to_4095)
    {
        if (k1_0_to_4095 < 0) k1_0_to_4095 = 0; else if (k1_0_to_4095 > 4095) k1_0_to_4095 = 4095;
        if (k2_0_to_4095 < 0) k2_0_to_4095 = 0; else if (k2_0_to_4095 > 4095) k2_0_to_4095 = 4095;

        // Control-rate update (knob mapping, random walk, table rebuild)
        if ((ctrlCounter_++ & (ctrlDiv_ - 1)) == 0)
        {
            // Pitch mapping (quadratic) for carrier, calculated at control-rate
            const float k1 = static_cast<float>(k1_0_to_4095) * (1.0f / 4095.0f);
            const float pitch = k1 * k1;
            const float f_car = 10.0f + 50.0f * pitch;
            carrier_.setFrequencyHz(f_car);

            // Mod oscillator amplitude from k2 (0..4095)
            mod_.setAmplitudeQ12(static_cast<uint16_t>(k2_0_to_4095));

            // Incremental update: walk and rebuild a small batch each tick
            stepAndRebuildBatch_();
        }

        // Carrier drives FM input of arbitrary oscillator
        const int16_t car_s = carrier_.nextSample(); // -2048..2047

        // Map carrier sample to FM in Hz (Q16.16) using precomputed scaling
        int32_t fm_q16_16 = static_cast<int32_t>((static_cast<int64_t>(car_s) * depth_q16_16_) >> 11);
        // Clamp FM to avoid negative effective increments (keep within ±80% of base)
        if (fm_q16_16 < -fmCap_q16_16_) fm_q16_16 = -fmCap_q16_16_;
        if (fm_q16_16 >  fmCap_q16_16_)  fm_q16_16 =  fmCap_q16_16_;

        // Generate arbitrary oscillator output with FM applied
        int16_t out = mod_.nextSample(fm_q16_16);
        return static_cast<int32_t>(out);
    }

private:
    // Random helpers based on WhiteNoise PRNG output (fast integer path)
    inline uint16_t rand12_() { return static_cast<uint16_t>(static_cast<int32_t>(noise_.nextSample(4095)) + 2048); } // 0..4095
    inline float randMinus1To1_()
    {
        // Map 12-bit unsigned 0..4095 -> [-1,1] using a scale; bias to center
        uint16_t r = rand12_();
        // Convert to signed centered around 0 then scale to [-1,1]
        int32_t s = static_cast<int32_t>(r) - 2048; // -2048..2047
        return static_cast<float>(s) * (1.0f / 2048.0f);
    }

    // Optimized random walk: use direction as a single byte, update in-place, avoid float array copies
    inline void stepAndRebuildBatch_()
    {
        constexpr int kBatchSize = 32; // 256 / 32 = 8 control ticks per full refresh
        const float dL = L_;
        int start = static_cast<int>(walkHead_);
        for (int j = 0; j < kBatchSize; ++j)
        {
            int i = (start + j) & 0xFF; // 0..255

            // Instead of storing vx/vy, just store direction index and update it
            // Pick new direction (reuse previous direction with some probability for smoother walk)
            uint8_t prev_dir = dir_[i];
            uint16_t r = rand12_();
            uint8_t new_dir;
            // 2/3 chance to keep previous direction, 1/3 to pick new
            if ((r & 0x3) != 0) {
                new_dir = prev_dir;
            } else {
                new_dir = static_cast<uint8_t>(r & 0x7u);
            }
            dir_[i] = new_dir;

            float dx = v0_ * kDirX[new_dir];
            float dy = v0_ * kDirY[new_dir];

            float xn = x_[i] + dx;
            float yn = y_[i] + dy;

            // Fast periodic boundary conditions
            if (xn < -dL + 100.0f) xn += 100.0f; else if (xn > dL) xn -= 100.0f;
            if (yn < 0.01f) yn += dL; else if (yn > dL) yn -= dL;

            x_[i] = xn;
            y_[i] = yn;

            // Sparse table: ~1/6 probability to write scaled x; else zero
            // Use lower bits of r for dice to avoid extra random call
            int dice = static_cast<int>((r >> 3) % 6u);
            if (dice == 0)
            {
                float xn_norm = xn / L_; // ~[-1,1]
                // Clamp using fmin/fmax for speed
                xn_norm = fmaxf(-1.0f, fminf(1.0f, xn_norm));
                int32_t v16 = static_cast<int32_t>(xn_norm * 32767.0f);
                // Clamp to int16_t
                if (v16 < -32768) v16 = -32768;
                if (v16 >  32767) v16 =  32767;
                waveTable_[i] = static_cast<int16_t>(v16);
            }
            else
            {
                waveTable_[i] = 0;
            }
        }
        walkHead_ = static_cast<uint8_t>((start + kBatchSize) & 0xFF);
    }

    // State
    WaveformOscillator carrier_;
    WaveformOscillator mod_;
    WhiteNoise noise_;

    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float L_ = 20000.0f; // box size (matches original scale)
    static constexpr float v0_ = 10.0f;   // step size
    static constexpr float f_mod_base_ = 250.0f;

    float x_[256] = {0};
    float y_[256] = {0};
    uint8_t dir_[256] = {0}; // direction index for each walker
    int16_t waveTable_[256] = {0};

    // Precomputed FM scaling and clamp (Q16.16)
    int32_t depth_q16_16_ = 0;
    int32_t base_q16_16_ = 0;
    int32_t fmCap_q16_16_ = 0;

    // 8-direction unit vectors (floats) to avoid trig at runtime
    static constexpr float kInvSqrt2 = 0.7071067811865475f;
    static constexpr float kDirX[8] = { 1.0f,  kInvSqrt2,  0.0f, -kInvSqrt2, -1.0f, -kInvSqrt2,  0.0f,  kInvSqrt2 };
    static constexpr float kDirY[8] = { 0.0f,  kInvSqrt2,  1.0f,  kInvSqrt2,  0.0f, -kInvSqrt2, -1.0f, -kInvSqrt2 };

    // Control-rate divider
    static constexpr uint32_t ctrlDiv_ = 128; // update ~375 Hz at 48 kHz
    uint32_t ctrlCounter_ = 0;

    // Rolling head for incremental updates
    uint8_t walkHead_ = 0;
};

