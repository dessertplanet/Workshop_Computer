#pragma once

#include <cstdint>
#include "../dsp/WaveformOsc.hpp"
#include "MicroVerbInt.hpp"

// Integer-optimized port of Noise Plethora P_S_H:
// - Source: Sample & Hold waveform generator
// - Effect: MicroVerb (fixed params)
// - Mix: out = (1 - k2) * dry + (4*k2) * wet, k2 in [0..1]
// Controls (0..4095):
//  - k1: oscillator frequency = 15 Hz + 5000 Hz * (k1/4095)
//  - k2: dry/wet cross-mix with 4x wet gain (clamped)
class SampleHoldReverbAlgo {
public:
    SampleHoldReverbAlgo()
    {
        // Sample & Hold oscillator setup
        shOsc_.setSampleRate(48000.0f);
        shOsc_.setShape(WaveformOscillator::Shape::SampleHold);
        shOsc_.setAmplitudeQ12(4095);
        shOsc_.setFrequencyHz(200.0f);

        // MicroVerb setup: pure wet output (we'll mix externally)
        reverb_.setDry(0.0f);
        reverb_.setWet(1.0f);
        reverb_.setDamp(1.0f);
        reverb_.setRoomSize(0.5f);
        reverb_.setPredelayMs(2.0f, 48000.0f);
    }

    inline int16_t nextSample(uint16_t k1_0_to_4095, uint16_t k2_0_to_4095)
    {
        // Control-rate parameter updates to minimize float work
        if ((ctrlCounter_++ & 0x7F) == 0)
        {
            // freq = 15 + 5000*(k1/4095)
            float k1 = static_cast<float>(k1_0_to_4095) * (1.0f / 4095.0f);
            float freq = 15.0f + 5000.0f * k1;
            if (freq < 0.0f) freq = 0.0f;
            shOsc_.setFrequencyHz(freq);
        }

        // Source
        const int16_t dry_s = shOsc_.nextSample(); // -2048..2047

        // Reverb (pure wet mono)
        int16_t wet = reverb_.process(dry_s);

        // Integer cross-mix
        // dry_gain = (4095 - k2); wet_gain = min(4*k2, 4095)  [Q12 gains]
        int32_t k2c = static_cast<int32_t>(k2_0_to_4095);
        if (k2c < 0) k2c = 0; else if (k2c > 4095) k2c = 4095;
        int32_t dry_gain_q12 = 4095 - k2c;
        int32_t wet_gain_q12 = k2c << 2; // *4
        if (wet_gain_q12 > 4095) wet_gain_q12 = 4095;

        int32_t dry_mix = (static_cast<int32_t>(dry_s) * dry_gain_q12) >> 12;
        int32_t wet_mix = (static_cast<int32_t>(wet) * wet_gain_q12) >> 12;
        int32_t out = dry_mix + wet_mix;
        if (out < -2048) out = -2048;
        if (out >  2047) out =  2047;
        return static_cast<int16_t>(out);
    }

private:
    WaveformOscillator shOsc_;
    dsp::MicroVerbMonoInt reverb_;
    uint32_t ctrlCounter_ = 0;
};


