#pragma once
#include <cstdint>
#include <cmath>
#include "SVF_LUT_512.h"   // provides: F_LUT_512[], KnobMap_512[], q_ch_q15_Q{3,6,9,12}, F_LUT_SIZE==512

// ================================================================
// Integer (Q15) State Variable Filter (Chamberlin form) using prebuilt LUTs
// - Audio-rate path is integer-only (Q15).
// - Cutoff uses F_LUT_512[] and KnobMap_512[] from SVF_LUT_512.h
// - Fixed resonances Q in {3,6,9,12} via q_ch = 1/Q in Q15 (constants in LUT header)
// - LUT was generated for FS=48k, FMIN=20 Hz, FMAX=8 kHz (must match your LUT file).
// ================================================================
class StateVariableFilterIntLUT {
public:
    enum class Mode { Lowpass, Bandpass, Highpass, Notch };
    enum class Resonance { Q3, Q6, Q9, Q12 };

    StateVariableFilterIntLUT() = default;

    // One-time init (no LUT building needed anymore)
    void begin() {
        setMode(Mode::Lowpass);
        setResonance(Resonance::Q6);
        setSampleRate(48000.0f); // informational only
        reset();
    }

    void setSampleRate(float fs) { sampleRate_ = (fs > 0.0f) ? fs : 48000.0f; }
    void setMode(Mode m)         { mode_ = m; }

    void setResonance(Resonance r) {
        resonance_ = r;
        switch (r) {
            case Resonance::Q3:  q_ch_q15_ = q_ch_q15_Q3;  break;
            case Resonance::Q6:  q_ch_q15_ = q_ch_q15_Q6;  break;
            case Resonance::Q9:  q_ch_q15_ = q_ch_q15_Q9;  break;
            case Resonance::Q12: q_ch_q15_ = q_ch_q15_Q12; break;
        }
    }

    // Control-rate: integer-only knob mapping (0..4095)
    inline void setCutoffFromKnob(uint16_t knob012) {
        f_q15_ = f_from_knob_q15_(knob012);
    }

    // Control-rate: Hz mapping via the LUT (float here is fine; not in hot path)
    // NOTE: Assumes LUT was built for FMIN=20, FMAX=8000, F_LUT_SIZE=512.
    void setCutoffHz(float fc) {
        if (fc < 20.0f)   fc = 20.0f;
        if (fc > 8000.0f) fc = 8000.0f;
        const float logSpan = std::log(8000.0f / 20.0f);
        const float pos = (std::log(fc / 20.0f) / logSpan) * float(F_LUT_SIZE - 1);
        int idx = int(pos);
        float fracf = pos - float(idx);
        if (idx < 0) { idx = 0; fracf = 0.0f; }
        if (idx >= F_LUT_SIZE - 1) { idx = F_LUT_SIZE - 2; fracf = 1.0f; }
        const uint16_t a = F_LUT_512[idx];
        const uint16_t b = F_LUT_512[idx + 1];
        const uint16_t frac = (uint16_t)std::lrint(fracf * 65535.0f);
        f_q15_ = lerp16_u16_(a, b, frac);
    }

    void reset() { low_q15_ = 0; band_q15_ = 0; }

    // ----- Audio-rate: process one 12-bit sample (−2048..+2047)
    inline int16_t process(int16_t x12) {
        return processWithFMod(x12, f_q15_);
    }

    // Audio-rate: process with *knob* cutoff modulation (0..4095), integer-only
    inline int16_t processWithKnobMod(int16_t x12, uint16_t knob012) {
        const uint16_t f_mod_q15 = f_from_knob_q15_(knob012);
        return processWithFMod(x12, f_mod_q15);
    }

    // Audio-rate: process with explicit f coefficient (Q15 0..~65534)
    inline int16_t processWithFMod(int16_t x12, uint16_t f_mod_q15) {
        // Convert input to Q15
        int32_t x = int32_t(x12) << 4; // −2048..+2047 -> ~−32768..+32752

        // Clamp f
        uint32_t f = f_mod_q15;
        if (f > 65534u) f = 65534u;

        // Chamberlin SVF (Q15)
        // low += f * band
        int32_t f_band = int32_t( (int64_t(f) * int64_t(band_q15_)) >> 15 );
        low_q15_ = sat_q15_(low_q15_ + f_band);

        // high = x - low - q * band
        int32_t q_band = int32_t( (int64_t(q_ch_q15_) * int64_t(band_q15_)) >> 15 );
        int32_t high_q15 = sat_q15_(x - low_q15_ - q_band);

        // band += f * high
        int32_t f_high = int32_t( (int64_t(f) * int64_t(high_q15)) >> 15 );
        band_q15_ = sat_q15_(band_q15_ + f_high);

        int32_t out_q15 = 0;
        switch (mode_) {
            case Mode::Lowpass:  out_q15 = low_q15_;                      break;
            case Mode::Bandpass: out_q15 = band_q15_;                     break;
            case Mode::Highpass: out_q15 = high_q15;                      break;
            case Mode::Notch:    out_q15 = sat_q15_(high_q15 + low_q15_); break;
        }

        // Back to 12-bit
        int32_t y12 = out_q15 >> 4;
        if (y12 < -2048) y12 = -2048;
        if (y12 >  2047) y12 =  2047;
        return (int16_t)y12;
    }

    // Dual-input mix (Teensy-style) then filter
    inline int16_t process(int16_t in1, int16_t in2) {
        int32_t mixed = int32_t(in1) + int32_t(in2);
        if (mixed >  2047) mixed =  2047;
        if (mixed < -2048) mixed = -2048;
        return process(int16_t(mixed));
    }

private:
    // pull sizes from LUT header
    static constexpr int F_LUT_SIZE = ::F_LUT_SIZE;

    // State & params
    Mode     mode_       = Mode::Lowpass;
    Resonance resonance_ = Resonance::Q6;
    float    sampleRate_ = 48000.0f; // informational
    int32_t  q_ch_q15_   = q_ch_q15_Q6;
    uint16_t f_q15_      = 0;        // current f
    int32_t  low_q15_    = 0;
    int32_t  band_q15_   = 0;

    // Helpers
    static inline int32_t sat_q15_(int32_t v) {
        if (v < -32768) return -32768;
        if (v >  32767) return  32767;
        return v;
    }
    static inline uint16_t lerp16_u16_(uint16_t a, uint16_t b, uint16_t frac) {
        const uint32_t diff = uint32_t(b) - uint32_t(a);
        return uint16_t( uint32_t(a) + ((diff * uint32_t(frac)) >> 16) );
    }
    static inline uint16_t f_from_knob_q15_(uint16_t knob012) {
        const ::KnobIdxFrac m = KnobMap_512[knob012 & 0x0FFF];
        const uint16_t a = F_LUT_512[m.idx];
        const uint16_t b = F_LUT_512[m.idx + 1];
        return lerp16_u16_(a, b, m.frac);
    }
};
