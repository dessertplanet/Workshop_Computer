#pragma once

#include <cstdint>
#include "../dsp/WaveformOsc.hpp"

// Port of P_pwCluster: 6 pulse oscillators with clustered frequencies and global pulse width control
class PwCluster {
public:
    static constexpr int MAX_OSCILLATORS = 5;

    PwCluster()
    {
        for (int i = 0; i < MAX_OSCILLATORS; ++i)
        {
            oscs_[i].setSampleRate(48000.0f);
            oscs_[i].setShape(WaveformOscillator::Shape::Square); // pulse equivalent
            // masterVolume ≈ 0.7
            oscs_[i].setAmplitudeQ12(2866); // 0.7 * 4095 ≈ 2866
            oscs_[i].setFrequencyHz(100.0f);
            oscs_[i].setPulseWidthQ15(16384); // 50%
        }

        ctrlCounter_ = 0;
        lastK1_ = 0xFFFF;
        lastK2_ = 0xFFFF;
    }

    // k1_4095: base pitch; k2_4095: pulse width control (inverted like Teensy DC mapping)
    inline int32_t process(int32_t k1_4095, int32_t k2_4095)
    {
        // Update parameters at control rate or when inputs change
        if ((ctrlCounter_++ & 0x7F) == 0 || k1_4095 != lastK1_ || k2_4095 != lastK2_)
        {
            lastK1_ = k1_4095;
            lastK2_ = k2_4095;

            // Normalize knobs
            float k1 = static_cast<float>(k1_4095) * (1.0f / 4095.0f);
            float k2 = static_cast<float>(k2_4095) * (1.0f / 4095.0f);

            // P_pwCluster: pitch1 = pow(knob_1, 2)
            float pitch1 = k1 * k1;
            float f1 = 40.0f + pitch1 * 8000.0f;
            float f2 = f1 * 1.227f;
            float f3 = f2 * 1.24f;
            float f4 = f3 * 1.17f;
            float f5 = f4 * 1.2f;
            float f6 = f5 * 1.3f;
            float f[6] = {f1, f2, f3, f4, f5, f6};

            // Set frequencies with clamping
            for (int i = 0; i < MAX_OSCILLATORS; ++i)
            {
                float fi = f[i];
                if (fi < 10.0f) fi = 10.0f;
                if (fi > 12000.0f) fi = 12000.0f;
                oscs_[i].setFrequencyHz(fi);
            }

            // Teensy code: dc1.amplitude(1 - knob_2*0.97)
            // Map to pulse width: pw = 1 - 0.97*k2, clamp to [0.03..0.97]
            float pw = 1.0f - (0.97f * k2);
            if (pw < 0.03f) pw = 0.03f;
            if (pw > 0.97f) pw = 0.97f;
            uint16_t pw_q15 = static_cast<uint16_t>(pw * 32768.0f + 0.5f);
            for (int i = 0; i < MAX_OSCILLATORS; ++i)
            {
                oscs_[i].setPulseWidthQ15(pw_q15);
            }
        }

        // Generate and mix
        int32_t mix = 0;
        for (int i = 0; i < MAX_OSCILLATORS; ++i)
        {
            mix += static_cast<int32_t>(oscs_[i].nextSample());
        }

        // Soft clip/clamp to 12-bit range
        if (mix < -2048) mix = -2048;
        if (mix >  2047) mix =  2047;
        return mix;
    }

private:
    WaveformOscillator oscs_[MAX_OSCILLATORS];
    uint32_t ctrlCounter_;
    int32_t lastK1_;
    int32_t lastK2_;
};


