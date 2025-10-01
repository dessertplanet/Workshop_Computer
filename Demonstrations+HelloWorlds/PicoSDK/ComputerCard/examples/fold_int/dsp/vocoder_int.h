#pragma once
#include <cstdint>
#include <cmath>
#include "fixedpoint_int.h"

namespace cc_dsp {

struct OnePoleLP {
    int32_t y = 0;
    int32_t a_q15 = 0; // coefficient alpha in Q15
    inline int32_t process(int32_t x_q15) {
        // y += a*(x - y)
        int32_t diff = x_q15 - y;
        y += static_cast<int32_t>((static_cast<int64_t>(a_q15) * diff) >> 15);
        return y;
    }
};

// 4-band simple vocoder state
struct VocoderState {
    bool inited = false;
    // per band: preprocessing LP for HP stage (low cutoff), then post LP (high cutoff)
    OnePoleLP pre_lp_mod[4];
    OnePoleLP post_lp_mod[4];
    OnePoleLP pre_lp_car[4];
    OnePoleLP post_lp_car[4];
    // envelope followers per band
    OnePoleLP env_lp[4];
    // cached config
    int prev_p2_bucket = -1;
    int32_t release_a_q15 = 0;
};

inline int32_t q15_from_float(double x) {
    long v = std::lround(x * 32768.0);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return static_cast<int32_t>(v);
}

inline int32_t alpha_from_hz(double fc, double fs) {
    // alpha = 1 - exp(-2*pi*fc/fs)
    double a = 1.0 - std::exp(-2.0 * M_PI * fc / fs);
    if (a < 0.0001) a = 0.0001;
    if (a > 0.9999) a = 0.9999;
    return q15_from_float(a);
}

inline void vocoder_update_coefs(VocoderState &st, int32_t p1_q15, int32_t p2_q15) {
    // p2: formant shift, map [0..1] to scale ~ 0.5x .. 2x
    int32_t p2_uni = p2_q15; if (p2_uni < 0) p2_uni = 0; if (p2_uni > Q15_ONE) p2_uni = Q15_ONE;
    double shift = std::pow(2.0, (static_cast<double>(p2_uni) / 32768.0 - 0.5) * 2.0);

    // base center freqs (Hz)
    const double base_cf[4] = { 300.0, 700.0, 1500.0, 3000.0 };
    const double fs = 48000.0;

    for (int i = 0; i < 4; ++i) {
        double cf = base_cf[i] * shift;
        // simple band edges
        double low = cf / std::sqrt(2.0);
        double high = cf * std::sqrt(2.0);
        int32_t a_low = alpha_from_hz(low, fs);
        int32_t a_high = alpha_from_hz(high, fs);
        st.pre_lp_mod[i].a_q15 = a_low;
        st.post_lp_mod[i].a_q15 = a_high;
        st.pre_lp_car[i].a_q15 = a_low;
        st.post_lp_car[i].a_q15 = a_high;
    }

    // p1: release time mapping 5ms..500ms → alpha
    double rel_ms = 5.0 + (static_cast<double>(p1_q15) / 32768.0) * (500.0 - 5.0);
    double a_env = 1.0 - std::exp(-1.0 / (rel_ms * 0.001 * fs));
    if (a_env < 0.00005) a_env = 0.00005;
    if (a_env > 0.2) a_env = 0.2;
    st.release_a_q15 = q15_from_float(a_env);
}

inline int32_t process_vocoder_q15(VocoderState &st,
                                   int32_t x1_q15, // carrier
                                   int32_t x2_q15, // modulator
                                   int32_t p1_q15, // release
                                   int32_t p2_q15  // formant shift
                                   ) {
    // Initialize once
    if (!st.inited) {
        vocoder_update_coefs(st, p1_q15, p2_q15);
        for (int i=0;i<4;++i) { st.pre_lp_mod[i].y = st.post_lp_mod[i].y = 0; st.pre_lp_car[i].y = st.post_lp_car[i].y = 0; st.env_lp[i].y = 0; st.env_lp[i].a_q15 = st.release_a_q15; }
        st.inited = true;
    }

    // Recompute coefficients if p2 bucket changes (quantize to 32 steps)
    int p2_bucket = static_cast<int>((static_cast<uint32_t>(p2_q15 < 0 ? 0 : (p2_q15 > Q15_ONE ? Q15_ONE : p2_q15)) * 32u) >> 15);
    if (p2_bucket != st.prev_p2_bucket) {
        vocoder_update_coefs(st, p1_q15, p2_q15);
        for (int i=0;i<4;++i) st.env_lp[i].a_q15 = st.release_a_q15;
        st.prev_p2_bucket = p2_bucket;
    }

    // Optional: choose carrier if one input is silent
    int32_t ax1 = x1_q15 >= 0 ? x1_q15 : -x1_q15;
    int32_t ax2 = x2_q15 >= 0 ? x2_q15 : -x2_q15;
    int32_t car = (ax1 >= 8) ? x1_q15 : x2_q15; // fallback to x2 if x1 near zero
    int32_t mod = (ax2 >= 8) ? x2_q15 : x1_q15;

    // Sum band contributions
    int64_t acc = 0;
    for (int i = 0; i < 4; ++i) {
        // Modulator bandpass: HP via pre-LP, then LP via post-LP
        int32_t lp_pre_m = st.pre_lp_mod[i].process(mod);
        int32_t hp_m = mod - lp_pre_m;
        int32_t bp_m = st.post_lp_mod[i].process(hp_m);
        // Envelope follower (rectify then 1-pole)
        int32_t rect = bp_m >= 0 ? bp_m : -bp_m;
        int32_t env = st.env_lp[i].process(rect);

        // Carrier bandpass
        int32_t lp_pre_c = st.pre_lp_car[i].process(car);
        int32_t hp_c = car - lp_pre_c;
        int32_t bp_c = st.post_lp_car[i].process(hp_c);

        // Apply envelope to carrier band
        int32_t band = static_cast<int32_t>((static_cast<int64_t>(bp_c) * env) >> 15);
        acc += band;
    }

    // Soft clip to Q15
    if (acc > Q15_MAX) acc = Q15_MAX; else if (acc < Q15_MIN) acc = Q15_MIN;
    return static_cast<int32_t>(acc);
}

} // namespace cc_dsp








