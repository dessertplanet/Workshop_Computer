// ResoNoise algorithm scaffold.
// Composes reusable DSP primitives (currently just WhiteNoise).

#pragma once

#include <cstdint>
#include "dsp/WhiteNoise.hpp"
#include "dsp/WaveformOsc.hpp"
#include "dsp/StateVariableFilterInt.hpp"  // <-- use LUT SVF
#include "dsp/Wavefolder.hpp"

class ResoNoiseAlgo {
public:
    ResoNoiseAlgo()
    {
        // Default LFO and Osc settings
        lfo.setSampleRate(48000.0f);
        lfo.setShape(WaveformOscillator::Shape::Sine);
        lfo.setFrequencyHz(0.5f);        // default, updated by X
        lfo.setAmplitudeQ12(4000);

        fmSine.setSampleRate(48000.0f);
        fmSine.setShape(WaveformOscillator::Shape::Sine);
        fmSine.setAmplitudeQ12(4095);

        modSquare.setSampleRate(48000.0f);
        modSquare.setShape(WaveformOscillator::Shape::Square);
        modSquare.setAmplitudeQ12(4095);

        // SVF (LUT version) — correct API
        svf.begin();  // no LUT building inside; just sets defaults & resets
        svf.setSampleRate(48000.0f); // informational
        svf.setMode(StateVariableFilterIntLUT::Mode::Lowpass);
        svf.setResonance(StateVariableFilterIntLUT::Resonance::Q9); // replaces setQ(9.0f)
        svf.setCutoffHz(8000.0f);  // static cutoff like Teensy P_resonoise
    }

    void reset(uint32_t seed) { noise.init(seed != 0 ? seed : 0x1u); }

    // Generate one sample. Controls map to P_resonoise style:
    // - x_q12: 0..4095 => primary "pitch" control (drives LFO and sine FM rate)
    // - y_q12: 0..4095 => bias for wavefolder (mapped to DC amplitude) and resonance
    // NOTE: Noise Plethora inverts knob readings! We must match this behavior.
    inline int16_t nextSample(uint16_t x_q12, uint16_t y_q12)
    {
        // Rarely reseed noise to vary texture with X
        seedAccumulator += static_cast<uint32_t>(x_q12);
        if ((reseedCounter++ & 0x0FFFu) == 0) {
            noise.init(baseSeed ^ seedAccumulator);
        }

        // Base noise voice (full amplitude)
        int16_t n = noise.nextSample(4095);

        // Control-rate updates (every 128 samples)
        if ((paramUpdateCounter++ & 0x7F) == 0) {
            // X → pitch (quadratic), match Plethora inversion
            const float x01 = 1.0f - (float)x_q12 * (1.0f / 4095.0f);
            const float pitch = x01 * x01;
            const float modHz  = 20.0f + pitch * 7777.0f;
            const float sineHz = 20.0f + pitch * 10000.0f;

            lfo.setFrequencyHz(modHz);
            fmSine.setFrequencyHz(sineHz);
            modSquare.setFrequencyHz(modHz);

            // Precompute FM depth (Q16.16), here using full sineHz (you had 25% note)
            sineHz_q16_16 = (int32_t)(sineHz * 65536.0f + 0.5f);
            fmDepth_q16_16 = sineHz_q16_16; // adjust if you want 25%: >> 2
        }

        // FM the sine with the square modulator
        const int16_t m = modSquare.nextSample(); // -2048..2047
        // fm_hz_q16_16 = (m/2048) * fmDepth_q16_16
        const int64_t fm_tmp = (int64_t)m * (int64_t)fmDepth_q16_16;
        const int32_t fm_q16_16 = (int32_t)(fm_tmp >> 11); // /2048
        const int16_t sine = fmSine.nextSample(fm_q16_16);

        // Wavefolder input: sine FM + DC from Y (Plethora inversion)
        const float y_norm = 1.0f - (float)y_q12 * (1.0f / 4095.0f);
        const float dc_amplitude = y_norm * 0.2f + 0.03f; // 0.03..0.23
        const int16_t dc_value = (int16_t)(dc_amplitude * 32767.0f);
        const int16_t folded = folder.process(sine, dc_value);

        // Route through filter (dual-input path like Teensy wiring)
        int16_t y = svf.process(n, folded); // correct LUT-SVF call

        // Scale output ~1.8x safely (int)
        int32_t ys = (int32_t)y * 9;   // *9/5 ≈ 1.8
        ys = ys / 5;
        if (ys < -2048) ys = -2048;
        if (ys >  2047) ys =  2047;
        return (int16_t)ys;
    }

    void setBaseSeed(uint32_t seed) { baseSeed = seed != 0 ? seed : 0x1u; }

private:
    WhiteNoise noise;
    WaveformOscillator lfo;       // reserved for future modulation
    WaveformOscillator fmSine;
    WaveformOscillator modSquare;
    StateVariableFilterIntLUT svf;
    Wavefolder folder;

    uint32_t paramUpdateCounter = 0;

    // Control-rate cached params (Q16.16 where noted)
    int32_t sineHz_q16_16 = (int32_t)(20.0f * 65536.0f);
    int32_t fmDepth_q16_16 = sineHz_q16_16 >> 2;

    uint32_t baseSeed = 0xA5A5F00Du;
    uint32_t seedAccumulator = 0;
    uint32_t reseedCounter = 0;
};
