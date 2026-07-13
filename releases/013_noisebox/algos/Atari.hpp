#pragma once

#include <cstdint>
#include "../dsp/WaveformOsc.hpp"

// Port of Noise Plethora's Atari plugin using the local WaveformOscillator.
// Reference: P_Atari.hpp
// - Two modulators cross-modulate each other
// - Shapes: mod1 = square (with DC offset in original), mod2 = pulse (square here)
// - Frequencies: f1 = 10 + (k1^2)*50, f2 = 10 + k2*200
// - FM depth on mod1 depends on k2: depth = k2*8 + 3 (mapped to Hz scaling here)
// - Output is mod2's audio
class Atari {
public:
    Atari()
    {
        // Initialize oscillators
        mod1_.setSampleRate(48000.0f);
        mod2_.setSampleRate(48000.0f);

        mod1_.setShape(WaveformOscillator::Shape::Square);
        mod2_.setShape(WaveformOscillator::Shape::Square); // Pulse in original; Square with 50% duty here

        mod1_.setAmplitudeQ12(4095);
        mod2_.setAmplitudeQ12(4095);

        // Reasonable initial frequencies
        mod1_.setFrequencyHz(60.0f);
        mod2_.setFrequencyHz(60.0f);

        prev_mod1_out_ = 0;
        prev_mod2_out_ = 0;
    }

    // k1_4095, k2_4095: 0..4095
    // Returns 12-bit signed sample in -2048..2047 (int32 for consistency with other algos)
    inline int32_t process(int32_t k1_4095, int32_t k2_4095)
    {
        // Clamp incoming controls to 0..4095 to avoid CV sum overflow/underflow
        if (k1_4095 < 0) k1_4095 = 0; else if (k1_4095 > 4095) k1_4095 = 4095;
        if (k2_4095 < 0) k2_4095 = 0; else if (k2_4095 > 4095) k2_4095 = 4095;

        // Knob normalization
        float k1 = static_cast<float>(k1_4095) * (1.0f / 4095.0f);
        float k2 = static_cast<float>(k2_4095) * (1.0f / 4095.0f);

        // Frequency mapping (match original)
        float pitch1 = k1 * k1;                         // pow(knob_1, 2)
        float f1 = 10.0f + pitch1 * 50.0f;              // waveformMod1.frequency(10+(pitch1*50))
        float f2 = 10.0f + k2 * 200.0f;                 // waveformMod2.frequency(10+(knob_2*200))

        mod1_.setFrequencyHz(f1);
        mod2_.setFrequencyHz(f2);

        // FM depth for mod1 depends on k2: (knob_2*8 + 3)
        // Map that depth to Hz in Q16.16 with stronger scaling for audibility
        float depth_index = (k2 * 8.0f) + 3.0f;         // ~3..11
        int32_t fm1_scale = static_cast<int32_t>(depth_index * 512.0f); // stronger coupling

        // Emulate waveformMod1.offset(1) by adding a DC bias when feeding mod2
        // Square amplitude here is ~±1024; add 1024 -> 0..2048 (unipolar)
        int32_t mod1_unipolar = static_cast<int32_t>(prev_mod1_out_) + 1024;
        if (mod1_unipolar < 0) mod1_unipolar = 0; else if (mod1_unipolar > 2048) mod1_unipolar = 2048;

        // Cross-modulation: mod2 -> mod1 (variable depth), mod1 -> mod2 (with DC bias, strong depth)
        int32_t fm1_q16_16 = static_cast<int32_t>(prev_mod2_out_) * fm1_scale;
        int32_t fm2_q16_16 = mod1_unipolar * 2048; // strong positive FM like offset(1)

        // Generate new samples
        int16_t y1 = mod1_.nextSample(fm1_q16_16);
        int16_t y2 = mod2_.nextSample(fm2_q16_16);

        // Store for next sample's cross-modulation
        prev_mod1_out_ = y1;
        prev_mod2_out_ = y2;

        // Output is mod2 in the original plugin
        int32_t out = static_cast<int32_t>(y2);
        if (out > 2047) out = 2047;
        if (out < -2048) out = -2048;
        return out;
    }

private:
    WaveformOscillator mod1_; // waveformMod1
    WaveformOscillator mod2_; // waveformMod2 (pulse in original)

    int16_t prev_mod1_out_;
    int16_t prev_mod2_out_;
};


