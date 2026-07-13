// FMTest: two sine operators FM-ing each other
// - Kx controls pitch of operator A
// - Ky controls pitch of operator B
// - FM depth fixed at 50% of each operator's own base frequency

#pragma once

#include <cstdint>
#include "dsp/WaveformOsc.hpp"

class FMTestAlgo {
public:
    FMTestAlgo()
    {
        for (int i = 0; i < 2; ++i)
        {
            op[i].setSampleRate(48000.0f);
            op[i].setShape(WaveformOscillator::Shape::Square);
            op[i].setAmplitudeQ12(4095);
            op[i].setFrequencyHz(220.0f);
        }
    }

    inline int16_t nextSample(uint16_t x_q12, uint16_t y_q12)
    {
        // Control-rate update (every 128 samples): map knobs to base freqs and FM scales
        if ((ctrlCounter++ & 0x7F) == 0)
        {
            // Map 0..4095 -> 0..1, square for musical feel
            float x01 = static_cast<float>(x_q12) * (1.0f / 4095.0f);
            float y01 = static_cast<float>(y_q12) * (1.0f / 4095.0f);
            float px = x01 * x01;
            float py = y01 * y01;

            // Base frequency ranges (Hz)
            float fA = 20.0f + px * 2000.0f; // 20..2020 Hz
            float fB = 20.0f + py * 2000.0f; // 20..2020 Hz

            baseHz[0] = fA; baseHz[1] = fB;
            op[0].setFrequencyHz(fA);
            op[1].setFrequencyHz(fB);

            // FM depth = 50% of base frequency
            for (int i = 0; i < 2; ++i)
            {
                float depth_hz = 0.5f * baseHz[i];
                if (depth_hz < 0.0f) depth_hz = 0.0f;
                fmScale_q16_16[i] = static_cast<int32_t>(depth_hz * 65536.0f + 0.5f);
                // Clamp so net increment never flips sign (±49% of base)
                float cap_hz = 0.49f * baseHz[i];
                maxFm_q16_16[i] = static_cast<int32_t>(cap_hz * 65536.0f + 0.5f);
            }
        }

        // Normalize previous outputs to Q16.16 in [-1,1] using *32 (since 65536/2048 ~= 32)
        int32_t a_in_q16_16 = static_cast<int32_t>(prevSample[1]) * 32; // B modulates A
        int32_t b_in_q16_16 = static_cast<int32_t>(prevSample[0]) * 32; // A modulates B
        if (a_in_q16_16 < -65536) a_in_q16_16 = -65536; if (a_in_q16_16 > 65536) a_in_q16_16 = 65536;
        if (b_in_q16_16 < -65536) b_in_q16_16 = -65536; if (b_in_q16_16 > 65536) b_in_q16_16 = 65536;

        // Convert to FM in Hz (Q16.16), clamp to cap
        int32_t fmA_q16_16 = static_cast<int32_t>((static_cast<int64_t>(a_in_q16_16) * fmScale_q16_16[0]) >> 16);
        int32_t fmB_q16_16 = static_cast<int32_t>((static_cast<int64_t>(b_in_q16_16) * fmScale_q16_16[1]) >> 16);
        if (fmA_q16_16 < -maxFm_q16_16[0]) fmA_q16_16 = -maxFm_q16_16[0];
        if (fmA_q16_16 >  maxFm_q16_16[0]) fmA_q16_16 =  maxFm_q16_16[0];
        if (fmB_q16_16 < -maxFm_q16_16[1]) fmB_q16_16 = -maxFm_q16_16[1];
        if (fmB_q16_16 >  maxFm_q16_16[1]) fmB_q16_16 =  maxFm_q16_16[1];

        // Generate
        int16_t a = op[0].nextSample(fmA_q16_16);
        int16_t b = op[1].nextSample(fmB_q16_16);
        prevSample[0] = a;
        prevSample[1] = b;

        // Mix: average to stay within 12-bit
        int32_t s = (static_cast<int32_t>(a) + static_cast<int32_t>(b)) >> 1;
        if (s < -2048) s = -2048; if (s > 2047) s = 2047;
        return static_cast<int16_t>(s);
    }

private:
    WaveformOscillator op[2];
    uint32_t ctrlCounter = 0;
    float baseHz[2] = {220.0f, 220.0f};
    int32_t fmScale_q16_16[2] = {0,0};
    int32_t maxFm_q16_16[2] = {0,0};
    int16_t prevSample[2] = {0,0};
};


