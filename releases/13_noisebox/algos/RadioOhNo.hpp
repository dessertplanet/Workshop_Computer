// RadioOhNo algorithm (inspired by Noise Plethora P_radioOhNo)
// Four pulse/square oscillators with:
// - Kx: squared mapping to base freqs
// - Ky: controls FM index via DC injected to each osc's FM input
// - Cross FM between (osc1 <-> osc2) and (osc3 <-> osc4)
// - FM depth matches Teensy frequencyModulation(0.5) = 0.5 octaves

#pragma once

#include <cstdint>
#include "dsp/WaveformOsc.hpp"

class RadioOhNoAlgo {
public:
    RadioOhNoAlgo()
    {
        // Configure oscillators
        for (int i = 0; i < 4; ++i)
        {
            osc[i].setSampleRate(48000.0f);
            osc[i].setShape(WaveformOscillator::Shape::Square);
            osc[i].setAmplitudeQ12(4095); // full-scale for audibility
            osc[i].setPulseWidthQ15(0.8f * 32768.0f); // 50%
            osc[i].setFrequencyHz(500.0f);
        }
    }

    inline int16_t nextSample(uint16_t x_q12, uint16_t y_q12)
    {
        // Control-rate update of base freqs and FM scales (every 128 samples)
        if ((ctrlCounter++ & 0x7F) == 0)
        {
            float x01 = static_cast<float>(x_q12) * (1.0f / 4095.0f);
            float pitch = x01 * x01; // pow2

            float f1 = 2500.0f * pitch + 20.0f;
            float f2 = 1120.0f - 1100.0f * pitch; if (f2 < 20.0f) f2 = 20.0f;
            float f3 = 2900.0f * pitch + 20.0f;
            float f4 = 8020.0f - (8000.0f * pitch + 20.0f); if (f4 < 20.0f) f4 = 20.0f;

            baseHz[0] = f1; baseHz[1] = f2; baseHz[2] = f3; baseHz[3] = f4;
            osc[0].setFrequencyHz(f1);
            osc[1].setFrequencyHz(f2);
            osc[2].setFrequencyHz(f3);
            osc[3].setFrequencyHz(f4);

            // FM depth = 0.5 octaves (like Teensy frequencyModulation(0.5))
            // 0.5 octaves = base_freq * (2^0.5 - 1) ≈ base_freq * 0.414
            for (int i = 0; i < 4; ++i)
            {
                float octaves = 5.0f; // equivalent to frequencyModulation(0.5)
                float depth_hz = baseHz[i] * (powf(2.0f, octaves) - 1.0f);
                if (depth_hz < 0.0f) depth_hz = 0.0f;
                fmScale_q16_16[i] = static_cast<int32_t>(depth_hz * 65536.0f + 0.5f);
                // Cap at 80% to prevent negative frequencies
                float cap_hz = 0.8f * baseHz[i];
                maxFm_q16_16[i] = static_cast<int32_t>(cap_hz * 65536.0f + 0.5f);
            }

            // Precompute Ky DC FM component in Q16.16 (0..1)
            y_dc_q16_16 = static_cast<int32_t>((static_cast<int64_t>(y_q12) * 65536 + 2047) / 4095);
            if (y_dc_q16_16 < 0) y_dc_q16_16 = 0; if (y_dc_q16_16 > 65536) y_dc_q16_16 = 65536;
        }

        // Build FM inputs from previous samples (normalized) plus Ky DC; pairwise cross-FM
        // prevSample normalized to Q16.16: prev * 32 (since 65536/2048 = 32)
        int32_t nrm[4];
        for (int i = 0; i < 4; ++i) nrm[i] = static_cast<int32_t>(prevSample[i]) * 32;

        int32_t fm_in_q16_16[4];
        fm_in_q16_16[0] = nrm[1] + y_dc_q16_16;
        fm_in_q16_16[1] = nrm[0] + y_dc_q16_16;
        fm_in_q16_16[2] = nrm[3] + y_dc_q16_16;
        fm_in_q16_16[3] = nrm[2] + y_dc_q16_16;
        for (int i = 0; i < 4; ++i)
        {
            if (fm_in_q16_16[i] < -65536) fm_in_q16_16[i] = -65536;
            if (fm_in_q16_16[i] >  65536) fm_in_q16_16[i] =  65536;
        }

        // Generate and mix
        int16_t out_s[4];
        int32_t s = 0;
        for (int i = 0; i < 4; ++i)
        {
            int32_t fm_hz_q16_16 = static_cast<int32_t>((static_cast<int64_t>(fm_in_q16_16[i]) * fmScale_q16_16[i]) >> 16);
            // Clamp FM to keep net increment positive and avoid stall
            int32_t cap = maxFm_q16_16[i];
            if (fm_hz_q16_16 < -cap) fm_hz_q16_16 = -cap;
            if (fm_hz_q16_16 >  cap) fm_hz_q16_16 =  cap;
            out_s[i] = osc[i].nextSample(fm_hz_q16_16);
            s += out_s[i];
        }
        // update prevs
        for (int i = 0; i < 4; ++i) prevSample[i] = out_s[i];
        // Soft scale to 12-bit
        // Sum of 4 squares at full-scale ~ +/-4096; divide by 2 to fit in range
        s = s >> 1; // /2
        if (s < -2048) s = -2048;
        if (s > 2047)  s = 2047;
        return static_cast<int16_t>(s);
    }

private:
    WaveformOscillator osc[4];
    uint32_t ctrlCounter = 0;
    float baseHz[4] = {500,500,500,500};
    int32_t fmScale_q16_16[4] = {0,0,0,0};
    int32_t maxFm_q16_16[4] = {0,0,0,0};
    int16_t prevSample[4] = {0,0,0,0};
    int32_t y_dc_q16_16 = 0; // 0..65536
};


