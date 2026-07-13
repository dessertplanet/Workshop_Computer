#pragma once

#include <cstdint>
#include "../dsp/WaveformOsc.hpp"

// Port of P_arrayOnTheRocks: a sine carrier FM'd by an arbitrary waveform oscillator.
// Controls:
//  - k1 (0..4095): pitch control via pitch = (k1/4095)^2
//      carrier: f = 100 + pitch*500
//      mod:     f = 10  + pitch*10000
//  - k2 (0..4095): carrier amplitude 0..1
// Waveform: 256-sample int16 table provided at construction.
class ArrayOnTheRocks {
public:
    // Default uses a copied version of the Teensy myWaveform[] with "test" entries set to 0.
    ArrayOnTheRocks()
    {
        mod_.setSampleRate(48000.0f);
        mod_.setShape(WaveformOscillator::Shape::Arbitrary);
        mod_.setArbitraryWaveform(kDefaultWaveform256);
        mod_.setAmplitudeQ12(4095);
        mod_.setFrequencyHz(250.0f);

        car_.setSampleRate(48000.0f);
        car_.setShape(WaveformOscillator::Shape::Sine);
        car_.setAmplitudeQ12(4095);
        car_.setFrequencyHz(500.0f);
    }

    // Caller may provide their own 256-sample int16 table
    explicit ArrayOnTheRocks(const int16_t* arbitrary256)
    {
        mod_.setSampleRate(48000.0f);
        mod_.setShape(WaveformOscillator::Shape::Arbitrary);
        mod_.setArbitraryWaveform(arbitrary256);
        mod_.setAmplitudeQ12(4095); // full-scale for FM source
        mod_.setFrequencyHz(250.0f);

        car_.setSampleRate(48000.0f);
        car_.setShape(WaveformOscillator::Shape::Sine);
        car_.setAmplitudeQ12(4095);
        car_.setFrequencyHz(500.0f);
    }

    inline int32_t process(int32_t k1_4095, int32_t k2_4095)
    {
        if (k1_4095 < 0) k1_4095 = 0; else if (k1_4095 > 4095) k1_4095 = 4095;
        if (k2_4095 < 0) k2_4095 = 0; else if (k2_4095 > 4095) k2_4095 = 4095;

        float k1 = static_cast<float>(k1_4095) * (1.0f / 4095.0f);
        float k2 = static_cast<float>(k2_4095) * (1.0f / 4095.0f);
        float pitch = k1 * k1;

        float f_mod = 10.0f + pitch * 10000.0f;
        float f_car = 100.0f + pitch * 500.0f;
        mod_.setFrequencyHz(f_mod);
        car_.setFrequencyHz(f_car);

        // Compute carrier (sine) sample; its effective contribution scales with k2
        int16_t car_sample = car_.nextSample();   // -2048..2047

        // FM the arbitrary oscillator by the carrier: map car_sample to Hz in Q16.16
        // FM depth grows with k2, but base output remains present even at k2=0
        // fm_hz = (car/2048) * ((0.25 + 6*k2) * f_car)
        float depth_mult = 0.25f + 6.0f * k2;     // 0.25..6.25
        int32_t fcar_q16_16 = static_cast<int32_t>(f_car * 65536.0f + 0.5f);
        int32_t depth_q16_16 = static_cast<int32_t>(depth_mult * static_cast<float>(fcar_q16_16));
        // Apply k2 as additional linear scaler on FM depth (like amplitude to FM input)
        int32_t k2_q16_16 = static_cast<int32_t>(k2 * 65536.0f + 0.5f);
        int64_t depth_scaled = (static_cast<int64_t>(depth_q16_16) * k2_q16_16) >> 16;
        int32_t fm_q16_16 = static_cast<int32_t>((static_cast<int64_t>(car_sample) * depth_scaled) >> 11);

        // Generate arbitrary modulator output with FM applied
        int16_t mod_sample = mod_.nextSample(fm_q16_16);

        // Add ring modulation between mod and carrier; mix rises with k2
        int32_t ring = (static_cast<int32_t>(mod_sample) * static_cast<int32_t>(car_sample)) >> 11; // ~12-bit
        if (ring < -4096) ring = -4096; if (ring > 4095) ring = 4095;
        int32_t mix_q15 = static_cast<int32_t>(k2 * 32767.0f + 0.5f); // 0..32767
        int32_t inv_q15 = 32767 - mix_q15;
        int32_t mixed = (static_cast<int32_t>(mod_sample) * inv_q15 + ring * mix_q15) >> 15;

        // No overall amplitude gating by k2 to ensure constant audibility
        if (mixed < -2048) mixed = -2048;
        if (mixed >  2047) mixed =  2047;
        return mixed;
    }

private:
    WaveformOscillator mod_;
    WaveformOscillator car_;

    // Copied from P_arrayOnTheRocks.hpp, with 'test' placeholders replaced by 0.
    static constexpr int16_t kDefaultWaveform256[256] = {
        0, 1895, 3748, 5545, 7278, 8934, 10506, 11984, 13362, 14634,
        0, 16840, 17769, 18580, 19274, 19853, 20319, 20678, 20933, 21093,
        21163, 21153, 21072, 20927, 20731, 20492, 20221, 0, 19625, 19320,
        19022, 18741, 18486, 18263, 18080, 17942, 17853, 17819, 17841, 17920,
        18058, 18254, 18507, 18813, 19170, 19573, 20017, 20497, 21006, 0,
        0, 0, 0, 23753, 24294, 24816, 25314, 25781, 26212, 26604,
        26953, 0, 0, 27718, 27876, 27986, 0, 0, 0, 27989,
        27899, 27782, 27644, 27490, 0, 0, 0, 0, 0, 26582,
        26487, 26423, 0, 0, 0, 0, 0, 26812, 27012, 27248,
        27514, 27808, 28122, 0, 28787, 0, 29451, 29762, 30045, 30293,
        0, 30643, 30727, 30738, 30667, 0, 30254, 29897, 0, 28858,
        28169, 27363, 26441, 25403, 24251, 22988, 21620, 20150, 18587, 16939,
        15214, 13423, 11577, 9686, 7763, 5820, 3870, 1926, 0, -1895,
        -3748, -5545, -7278, -8934, -10506, 0, -13362, -14634, -15794, -16840,
        -17769, -18580, -19274, -19853, -20319, -20678, -20933, -21093, -21163, -21153,
        -21072, -20927, 0, -20492, -20221, -19929, -19625, -19320, -19022, -18741,
        0, -18263, -18080, -17942, -17853, 0, -17841, -17920, -18058, -18254,
        -18507, -18813, -19170, -19573, 0, -20497, -21006, -21538, -22085, -22642,
        -23200, -23753, 0, 0, -25314, -25781, -26212, -26604, -26953, -27256,
        -27511, -27718, 0, 0, 0, -28068, -28047, 0, -27899, -27782,
        -27644, -27490, -27326, 0, -26996, -26841, -26701, -26582, -26487, 0,
        -26392, -26397, -26441, -26525, 0, -26812, 0, -27248, -27514, -27808,
        -28122, -28451, 0, 0, 0, -29762, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, -28169, -27363,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0
    };
};


