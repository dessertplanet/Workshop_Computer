#pragma once

#include <cstdint>
#include <cmath>
#include "dsp/WaveformOsc.hpp"
#include "dsp/WhiteNoise.hpp"
#include "algos/MicroVerbInt.hpp"

// Port of Noise Plethora P_BasuraTotal without reverb.
// Behavior:
//  - Square oscillator whose frequency is randomly switched between 0 Hz and
//    base = 200 + (k1/4095)^2 * 5000 Hz, at intervals proportional to (k2/4095)^2.
//  - Original used micros()-based timing and reinitialized the waveform each trigger;
//    we emulate this with a sample counter and phase reset.
class BasuraTotalAlgo {
public:
    BasuraTotalAlgo()
    {
        osc_.setSampleRate(48000.0f);
        osc_.setShape(WaveformOscillator::Shape::Square);
        osc_.setAmplitudeQ12(4095);
        osc_.setPulseWidthQ15(16384); // ~50%
        osc_.setFrequencyHz(0.0f);
        osc_.resetPhase(0);

        // MicroVerb init (small room feel)
        verb_.setRoomSize(0.75f);
        verb_.setDamp(0.55f);
        verb_.setWet(1.0f);
        verb_.setDry(0.0f);
        verb_.setPredelayMs(2.0f, 48000.0f);
    }

    // Generate one 12-bit sample. k1/k2: 0..4095
    inline int32_t process(int32_t k1_0_to_4095, int32_t k2_0_to_4095)
    {
        if (k1_0_to_4095 < 0) k1_0_to_4095 = 0; else if (k1_0_to_4095 > 4095) k1_0_to_4095 = 4095;
        if (k2_0_to_4095 < 0) k2_0_to_4095 = 0; else if (k2_0_to_4095 > 4095) k2_0_to_4095 = 4095;

        const float k1 = static_cast<float>(k1_0_to_4095) * (1.0f / 4095.0f);
        const float k2 = static_cast<float>(k2_0_to_4095) * (1.0f / 4095.0f);

        const float pitch1 = k1 * k1;
        const float pitch2 = k2 * k2;

        // Base frequency mapping from original
        const float baseHz = 200.0f + pitch1 * 5000.0f;

        // Control-rate timing: original used 100000 * pitch2 microseconds.
        // Convert to samples at 48 kHz: 100000 us = 0.1 s => 4800 samples.
        // intervalSamples in [0..4800]. Ensure at least 1 to avoid stall.
        int32_t intervalSamples = static_cast<int32_t>(4800.0f * pitch2 + 0.5f);
        if (intervalSamples < 1) intervalSamples = 1;

        // Trigger update when countdown expires
        if (--counter_ <= 0)
        {
            counter_ = intervalSamples;

            // Use existing white noise generator to decide gate (approx 50/50)
            // Sign bit as boolean: >= 0 -> 1, < 0 -> 0
            const bool on = (noise_.nextSample(4095) >= 0);
            const float f = on ? baseHz : 0.0f;

            // Emulate Teensy begin() at each click: set shape and reset phase
            osc_.setShape(WaveformOscillator::Shape::Square);
            osc_.setFrequencyHz(f);
            osc_.resetPhase(0);
        }
        // Dry synth sample
        const int16_t dry = static_cast<int16_t>(osc_.nextSample());
        // Process through MicroVerb (mono)
        int16_t wet = verb_.process(dry);
        int32_t mono = static_cast<int32_t>(wet);
        return mono;
    }

private:
    WaveformOscillator osc_;
    int32_t counter_ = 1;
    WhiteNoise noise_;
    dsp::MicroVerbMonoInt verb_;
};


