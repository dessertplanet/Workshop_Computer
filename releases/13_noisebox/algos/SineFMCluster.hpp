#pragma once

#include <cstdint>
#include "../dsp/WaveformOsc.hpp"

// Port of P_sineFMcluster: 4 triangle carriers FM'd by 4 sine modulators.
// Controls:
//  - k1 (0..4095): pitch -> f1 = 300 + (k1/4095)^2 * 8000, other freqs are ratios
//  - k2 (0..4095): FM index: 0.1 .. 1.0 equivalent (controls deviation)
// Output: mixed sum of 4 carriers, soft-scaled to 12-bit range
class SineFMCluster {
public:
    SineFMCluster()
    {
        for (int i = 0; i < 4; ++i)
        {
            carriers_[i].setSampleRate(48000.0f);
            carriers_[i].setShape(WaveformOscillator::Shape::Triangle);
            carriers_[i].setAmplitudeQ12(4095);
            carriers_[i].setFrequencyHz(400.0f);

            modulators_[i].setSampleRate(48000.0f);
            modulators_[i].setShape(WaveformOscillator::Shape::Sine);
            modulators_[i].setAmplitudeQ12(4095); // keep full, use index as FM depth
            modulators_[i].setFrequencyHz(1000.0f);
        }
    }

    inline int32_t process(int32_t k1_4095, int32_t k2_4095)
    {
        // Clamp and normalize controls
        if (k1_4095 < 0) k1_4095 = 0; else if (k1_4095 > 4095) k1_4095 = 4095;
        if (k2_4095 < 0) k2_4095 = 0; else if (k2_4095 > 4095) k2_4095 = 4095;
        float k1 = static_cast<float>(k1_4095) * (1.0f / 4095.0f);
        float k2 = static_cast<float>(k2_4095) * (1.0f / 4095.0f);

        // Pitch mapping
        float pitch = k1 * k1; // pow(knob_1, 2)
        float f1 = 300.0f + pitch * 8000.0f;
        float f2 = f1 * 1.227f;
        float f3 = f2 * 1.24f;
        float f4 = f3 * 1.17f;
        const float f[4] = {f1,f2,f3,f4};

        // Modulator frequencies: indexFreq = 0.333 times carrier
        const float indexFreq = 0.333f;
        for (int i = 0; i < 4; ++i)
        {
            carriers_[i].setFrequencyHz(f[i]);
            modulators_[i].setFrequencyHz(f[i] * indexFreq);
        }

        // FM index mapped to deviation as fraction of base frequency
        // index (0.1..1.0) in Teensy -> here: deviation = index * 0.8 * f_i
        float index = k2 * 0.8f + 0.1f; // 0.1..1.0

        int32_t mix = 0;
        for (int i = 0; i < 4; ++i)
        {
            int16_t m = modulators_[i].nextSample(); // -2048..2047
            float depth_hz = index * 0.8f * f[i];
            int32_t depth_q16_16 = static_cast<int32_t>(depth_hz * 65536.0f + 0.5f);
            int32_t fm_q16_16 = static_cast<int32_t>((static_cast<int64_t>(m) * depth_q16_16) >> 11);
            // Clamp FM so effective increment stays positive and bounded (avoid stalls/lockups)
            int32_t base_q16_16 = static_cast<int32_t>(f[i] * 65536.0f + 0.5f);
            int32_t cap = static_cast<int32_t>(0.8f * static_cast<float>(base_q16_16));
            if (fm_q16_16 < -cap) fm_q16_16 = -cap;
            if (fm_q16_16 >  cap) fm_q16_16 =  cap;
            int16_t c = carriers_[i].nextSample(fm_q16_16);
            mix += c;
        }

        // Soft scale: 4 signals summed; divide by 2 to fit in 12-bit range
        mix = mix >> 1;
        // Clamp to 12-bit signed
        if (mix < -2048) mix = -2048;
        if (mix >  2047) mix =  2047;
        return mix;
    }

private:
    WaveformOscillator modulators_[4];
    WaveformOscillator carriers_[4];
};


