// Reusable waveform oscillator for ComputerCard (48 kHz)
// Shapes: Sine (LUT + linear interpolation), Triangle, Saw, Square (with pulse width)

#pragma once

#include <cstdint>
#include <cmath>

class WaveformOscillator {
public:
    enum class Shape { Sine, Triangle, Saw, Square, SampleHold, Arbitrary };

    WaveformOscillator()
    {
        setSampleRate(48000.0f);
        setShape(Shape::Sine);
        setAmplitudeQ12(4095);
        setPulseWidthQ15(16384); // 0.5 duty
        setFrequencyHz(1.0f);
        initSineLUT();
    }

    void setSampleRate(float fs_hz)
    {
        if (fs_hz <= 0.0f) fs_hz = 48000.0f;
        sampleRate = fs_hz;
        hzToPhase = static_cast<float>(4294967296.0 / sampleRate); // 2^32 / fs
        // Integer path for FM: precompute rounded 2^32 / fs as unsigned 32-bit
        double k = 4294967296.0 / static_cast<double>(sampleRate);
        if (k < 0.0) k = 0.0;
        if (k > 4294967295.0) k = 4294967295.0;
        hzToPhaseU32 = static_cast<uint32_t>(k + 0.5);
    }

    void setShape(Shape s) { shape = s; }
    void setAmplitudeQ12(uint16_t a_q12)
    {
        if (a_q12 > 4095) a_q12 = 4095;
        amplitudeQ12 = a_q12;
    }

    // 0..32767 => 0..~1.0 duty; default 16384 ~= 0.5
    void setPulseWidthQ15(uint16_t pw_q15) { pulseWidthQ15 = pw_q15; }

    // Provide a 256-sample arbitrary waveform table (Teensy-compatible length)
    // Values are expected in int16_t range (-32768..32767). They will be scaled to 12-bit output.
    void setArbitraryWaveform(const int16_t* table256)
    {
        arbTable = table256;
    }

    void setFrequencyHz(float hz)
    {
        if (hz < 0.0f) hz = 0.0f;
        basePhaseInc = static_cast<uint32_t>(hz * hzToPhase);
    }

    void resetPhase(uint32_t phase = 0) { phaseAcc = phase; }

    // Optional FM in 16.16 fixed (Hz). Pass 0 for none.
    inline int16_t nextSample(int32_t fm_hz_q16_16 = 0)
    {
        // Advance phase
        uint32_t inc = basePhaseInc;
        if (fm_hz_q16_16 != 0)
        {
            // Integer FM: inc_add = fm_hz_q16_16 * (2^32/fs) >> 16
            int64_t inc_add = (static_cast<int64_t>(fm_hz_q16_16) * static_cast<int64_t>(hzToPhaseU32)) >> 16;
            int64_t tmp = static_cast<int64_t>(inc) + inc_add;
            if (tmp < 0) tmp = 0;
            if (tmp > 0xFFFFFFFFll) tmp = 0xFFFFFFFFll;
            inc = static_cast<uint32_t>(tmp);
        }
        lastPhaseAcc = phaseAcc;
        phaseAcc += inc;

        int32_t s12 = 0; // -2048..2047
        switch (shape)
        {
        case Shape::Sine:
        {
            // 512-point LUT with linear interpolation to 12-bit output
            constexpr unsigned tableBits = 9;             // 512 entries
            constexpr unsigned fracBits = 32 - tableBits; // 23 fractional bits
            uint32_t index = phaseAcc >> fracBits;        // top 9 bits
            uint32_t frac = (phaseAcc & ((1u << fracBits) - 1));
            uint32_t r16 = frac >> (fracBits - 16);       // to 16-bit fraction

            int32_t s1 = sineLUT[index];
            int32_t s2 = sineLUT[(index + 1) & (tableSize - 1)];
            int32_t interp = static_cast<int32_t>(((int64_t)s2 * r16 + (int64_t)s1 * (65536 - r16)) >> 16);
            // interp already approx -2048..2047
            if (interp < -2048) interp = -2048;
            if (interp > 2047) interp = 2047;
            s12 = interp;
            break;
        }
        case Shape::Saw:
        {
            // Direct signed conversion: top 12 bits as signed -2048..2047
            s12 = static_cast<int32_t>(phaseAcc >> 20) - 2048;
            break;
        }
        case Shape::Triangle:
        {
            // Triangle from saw
            int32_t r = static_cast<int32_t>(phaseAcc >> 20) & 0x0FFF; // 0..4095
            // Fold to triangle 0..2047..0 then center to -..+
            int32_t tri = (r < 2048) ? r : (4095 - r); // 0..2047
            s12 = (tri << 1) - 2048; // -2048..2046 (approx)
            break;
        }
        case Shape::Square:
        {
            // Compare high bits to pulse width threshold
            uint16_t ph_q15 = static_cast<uint16_t>(phaseAcc >> 17); // 0..32767 (Q15)
            bool high = ph_q15 < pulseWidthQ15;
            s12 = high ? 1024 : -1024; // 50% amplitude square; amplitude scales below
            break;
        }
        case Shape::SampleHold:
        {
            // Sample and hold - update held value on zero crossing (rising edge of phase)
            bool phase_wrapped = (phaseAcc < lastPhaseAcc);
            if (phase_wrapped)
            {
                // Generate new random value when phase wraps (similar to Teensy implementation)
                // Use simple LFSR for pseudo-random values
                sampleHoldLFSR = (sampleHoldLFSR >> 1) ^ (-(sampleHoldLFSR & 1) & 0xB400u);
                if (sampleHoldLFSR == 0) sampleHoldLFSR = 1; // prevent stuck at zero
                
                // Map LFSR to 12-bit signed range
                int32_t lfsr_val = static_cast<int32_t>(sampleHoldLFSR & 0x0FFF); // 0..4095
                sampleHoldValue = lfsr_val - 2048; // -2048..2047
            }
            s12 = sampleHoldValue;
            break;
        }
        case Shape::Arbitrary:
        {
            // 256-point arbitrary table with linear interpolation
            if (arbTable == nullptr)
            {
                // Fallback to square if not set
                uint16_t ph_q15 = static_cast<uint16_t>(phaseAcc >> 17);
                bool high = ph_q15 < pulseWidthQ15;
                s12 = high ? 1024 : -1024;
                break;
            }
            constexpr unsigned tableBits = 8;              // 256 entries
            constexpr unsigned fracBits = 32 - tableBits;   // 24 fractional bits
            uint32_t index = phaseAcc >> fracBits;          // top 8 bits
            uint32_t frac = (phaseAcc & ((1u << fracBits) - 1));
            uint32_t r16 = frac >> (fracBits - 16);         // to 16-bit fraction

            int32_t s1 = static_cast<int32_t>(arbTable[index]);
            int32_t s2 = static_cast<int32_t>(arbTable[(index + 1) & 0xFFu]);
            int32_t interp16 = static_cast<int32_t>(((int64_t)s2 * r16 + (int64_t)s1 * (65536 - r16)) >> 16);
            // Scale 16-bit to 12-bit by >>4 and clamp
            int32_t v12 = interp16 >> 4; // approx -2048..2047 if table is full-scale
            if (v12 < -2048) v12 = -2048;
            if (v12 >  2047) v12 =  2047;
            s12 = v12;
            break;
        }
        default:
        {
            // Fallback to square wave
            uint16_t ph_q15 = static_cast<uint16_t>(phaseAcc >> 17);
            bool high = ph_q15 < pulseWidthQ15;
            s12 = high ? 1024 : -1024;
            break;
        }
        }

        // Apply amplitude in Q12 - optimized for common case of full amplitude
        if (amplitudeQ12 == 4095) {
            // Skip multiplication for full amplitude (4095/4096 ≈ 1.0)
            return static_cast<int16_t>(s12);
        } else {
            // Use 32-bit multiplication instead of 64-bit for better performance
            int32_t y = (s12 * static_cast<int32_t>(amplitudeQ12)) >> 12;
            if (y < -2048) y = -2048;
            if (y > 2047) y = 2047;
            return static_cast<int16_t>(y);
        }
    }

private:
    // ---- Sine LUT ----
    static constexpr unsigned tableSize = 512;
    static inline int16_t sineLUT[tableSize];
    static inline bool sineInited = false;

    static inline void initSineLUT()
    {
        if (sineInited) return;
        for (unsigned i = 0; i < tableSize; ++i)
        {
            // Scale to 12-bit range
            float angle = (2.0f * 3.14159265358979323846f * static_cast<float>(i)) / static_cast<float>(tableSize);
            float sv = std::sinf(angle); // -1..1
            int32_t v = static_cast<int32_t>(sv * 2047.0f);
            if (v < -2048) v = -2048;
            if (v > 2047) v = 2047;
            sineLUT[i] = static_cast<int16_t>(v);
        }
        sineInited = true;
    }

    float sampleRate = 48000.0f;
    float hzToPhase = static_cast<float>(4294967296.0 / 48000.0); // 2^32 / fs
    uint32_t hzToPhaseU32 = static_cast<uint32_t>(4294967296.0 / 48000.0 + 0.5); // rounded integer 2^32/fs
    uint32_t phaseAcc = 0;
    uint32_t lastPhaseAcc = 0;
    uint32_t basePhaseInc = 0;
    uint16_t amplitudeQ12 = 4095;
    uint16_t pulseWidthQ15 = 16384;
    Shape shape = Shape::Sine;
    const int16_t* arbTable = nullptr;
    
    // Sample and Hold state
    uint16_t sampleHoldLFSR = 1; // LFSR for random values
    int16_t sampleHoldValue = 0; // Current held value
};


