#pragma once

#include <cstdint>
#include "../dsp/WaveformOsc.hpp"
#include "../dsp/WhiteNoise.hpp"

// Port of Noise Plethora "basurilla" plugin.
// Topology:
//   noise × wave1  ->\
//   noise × wave2   --> mix -> out
//   noise × wave3  ->/
// Controls:
//   k1 (0..4095): pitch shaping for the three modulators (pitch = (k1/4095)^2)
//   k2 (0..4095): pulse widths and (in original) noise amplitude (effectively full-scale)
class Basurilla {
public:
    Basurilla()
    {
        // Initialize common samplerate and shapes
        for (int i = 0; i < 3; ++i)
        {
            waves_[i].setSampleRate(48000.0f);
            waves_[i].setShape(WaveformOscillator::Shape::Square); // Pulse equivalent
            waves_[i].setAmplitudeQ12(4095);
            waves_[i].setPulseWidthQ15(16384); // 50%
        }

        // Initial frequencies roughly inspired by original (will be set each call)
        waves_[0].setFrequencyHz(110.0f);
        waves_[1].setFrequencyHz(10.0f);
        waves_[2].setFrequencyHz(10.0f);
    }

    // Generate one sample. k1/k2 are 0..4095
    inline int32_t process(int32_t k1_4095, int32_t k2_4095)
    {
        // Clamp controls
        if (k1_4095 < 0) k1_4095 = 0; else if (k1_4095 > 4095) k1_4095 = 4095;
        if (k2_4095 < 0) k2_4095 = 0; else if (k2_4095 > 4095) k2_4095 = 4095;

        // Normalize to 0..1
        float k1 = static_cast<float>(k1_4095) * (1.0f / 4095.0f);
        float k2 = static_cast<float>(k2_4095) * (1.0f / 4095.0f);

        // Pitch mapping
        float pitch = k1 * k1; // pow(knob_1, 2)

        // Frequency mappings (faithful to original intent)
        float f1 = pitch * 100.0f + 10.0f;   // waveform1.frequency(pitch*100+10)
        float f2 = pitch * 0.1f;             // waveform2.frequency(pitch*0.1)
        float f3 = pitch * 0.7f - 500.0f;    // waveform3.frequency(pitch*0.7-500)
        if (f2 < 0.0f) f2 = 0.0f;            // guard low frequencies
        if (f3 < 0.0f) f3 = 0.0f;

        waves_[0].setFrequencyHz(f1);
        waves_[1].setFrequencyHz(f2);
        waves_[2].setFrequencyHz(f3);

        // Pulse width mappings
        // waveform1.pulseWidth(knob_2*0.95)
        // waveform2.pulseWidth(knob_2*0.5+0.2)
        // waveform3.pulseWidth(knob_2*0.5)
        auto toQ15 = [](float duty01) -> uint16_t {
            if (duty01 < 0.0f) duty01 = 0.0f;
            if (duty01 > 0.999969f) duty01 = 0.999969f; // avoid full 100%
            return static_cast<uint16_t>(duty01 * 32768.0f + 0.5f);
        };
        waves_[0].setPulseWidthQ15(toQ15(k2 * 0.95f));
        waves_[1].setPulseWidthQ15(toQ15(k2 * 0.5f + 0.2f));
        waves_[2].setPulseWidthQ15(toQ15(k2 * 0.5f));

        // Original sets noise amplitude to (2 - k2), which saturates to full-scale.
        // Use full amplitude for parity.
        int16_t n = noise_.nextSample(4095);

        // Build three amplitude-modulated branches: noise × unipolar(wave)
        // Convert each wave from ±1024-ish to 0..2048 gate via bias
        // Then multiply and scale back to 12-bit
        int32_t sum = 0;
        for (int i = 0; i < 3; ++i)
        {
            int16_t w = waves_[i].nextSample(); // ~±1024 for square
            int32_t gate = static_cast<int32_t>(w) + 1024; // 0..2048
            if (gate < 0) gate = 0; else if (gate > 2048) gate = 2048;
            // product: n(-2048..2047) * gate(0..2048) -> scale by >>11 to return ~12-bit
            int32_t branch = (static_cast<int32_t>(n) * gate) >> 11;
            sum += branch;
        }

        // Mix with soft scaling to avoid clipping: average of three
        // sum range roughly [-3*2048, 3*2047]; divide by 3
        int32_t mixed = (sum * 21845) >> 16; // ≈ sum/3

        // Clamp to 12-bit signed
        if (mixed > 2047) mixed = 2047;
        if (mixed < -2048) mixed = -2048;
        return mixed;
    }

private:
    WaveformOscillator waves_[3];
    WhiteNoise noise_;
};


