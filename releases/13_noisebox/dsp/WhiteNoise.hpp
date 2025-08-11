// Reusable 12-bit white noise generator using xorshift32 PRNG

#pragma once

#include <cstdint>

class WhiteNoise {
public:
    WhiteNoise() { init(0x1u); }

    void init(uint32_t seed)
    {
        if (seed == 0) seed = 0x1u; // avoid zero state
        state = seed;
    }

    // amplitude_q12: 0..4095 (Q12). 0 = silent, 4095 ~= full-scale
    inline int16_t nextSample(uint16_t amplitude_q12)
    {
        // xorshift32
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;

        // Take upper 12 bits for whitened 12-bit sample, map to signed [-2048, 2047]
        int32_t u12 = static_cast<int32_t>((x >> 20) & 0x0FFFu); // 0..4095
        int32_t s12 = u12 - 2048; // -2048..2047

        // Apply amplitude in Q12 and clamp
        int32_t y = static_cast<int32_t>((static_cast<int64_t>(s12) * amplitude_q12) >> 12);
        if (y < -2048) y = -2048;
        if (y > 2047) y = 2047;
        return static_cast<int16_t>(y);
    }

private:
    uint32_t state = 1;
};


