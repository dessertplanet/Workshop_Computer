// dsp/MicroVerbMonoInt.hpp
#pragma once
#include <cstdint>
#include <cstring>

namespace dsp {

// ---- small Q15 helpers ----
static inline int32_t sat_q15(int32_t v){ if(v<-32768) return -32768; if(v>32767) return 32767; return v; }
static inline int16_t sat_q12_from_q15(int32_t q15){
    int32_t y = q15 >> 4; if (y < -2048) y = -2048; if (y > 2047) y = 2047; return (int16_t)y;
}
// rounded Q15 multiply
static inline int32_t mul_q15(int32_t a,int32_t b){
    int64_t p=(int64_t)a*b; int64_t adj=(p>=0)?(1ll<<14):((1ll<<14)-1); return (int32_t)((p+adj)>>15);
}

// ---- lean tunings (44.1k heritage; fine at 48k) ----
static constexpr int COMB1  = 1188;   // ~27 ms
static constexpr int COMB2  = 1536;   // ~35 ms
static constexpr int COMB3  = 1733;   // ~39 ms
static constexpr int APCORE = 225;    // ~5 ms
static constexpr int PREDELAY_MAX = 240; // up to ~5 ms @48k

// ---- primitives ----
template<int N>
struct CombQ15 {
    int16_t buf[N] = {};
    int idx = 0;
    int32_t store = 0;     // Q15
    int32_t fb = 27000;    // feedback (Q15)
    int32_t d1 = 16384;    // damp
    int32_t d2 = 16383;    // 1 - damp

    inline void set_feedback_q15(int32_t q){ if(q<0)q=0; if(q>32767)q=32767; fb=q; }
    inline void set_damp_q15(int32_t d){ if(d<0)d=0; if(d>32767)d=32767; d1=d; d2=32767-d; }
    inline void mute(){ std::memset(buf,0,sizeof(buf)); idx=0; store=0; }

    inline int32_t process(int32_t x){           // x: Q15, returns Q15
        int32_t y = (int32_t)buf[idx] << 1;      // int16 -> Q15 (keep headroom)
        store = sat_q15(mul_q15(y, d2) + mul_q15(store, d1));  // one-pole lowpass
        int32_t w = x + mul_q15(store, fb);      // feedback
        if (w < -32768) w = -32768; if (w > 32767) w = 32767;
        buf[idx] = (int16_t)(w >> 1);            // store with headroom
        if (++idx >= N) idx = 0;
        return y;
    }
};

template<int N>
struct AllpassQ15 {
    int16_t buf[N] = {};
    int idx = 0;
    int32_t fb = 16384; // ~0.5

    inline void set_feedback_q15(int32_t q){ if(q<-32768)q=-32768; if(q>32767)q=32767; fb=q; }
    inline void mute(){ std::memset(buf,0,sizeof(buf)); idx=0; }

    inline int32_t process(int32_t x){           // x: Q15, returns Q15
        int32_t b = (int32_t)buf[idx] << 1;
        int32_t y = sat_q15(b - x);              // -x + b
        int32_t w = x + mul_q15(b, fb);          // x + b*fb
        if (w < -32768) w = -32768; if (w > 32767) w = 32767;
        buf[idx] = (int16_t)(w >> 1);
        if (++idx >= N) idx = 0;
        return y;
    }
};

struct PredelayQ15 {
    int16_t buf[PREDELAY_MAX] = {};
    int idx = 0;
    int len = 0; // 0..PREDELAY_MAX

    inline void set_ms(float ms, float fs){
        int L = (int)(ms * 0.001f * fs + 0.5f);
        if (L < 0) L = 0; if (L > PREDELAY_MAX) L = PREDELAY_MAX;
        len = L; idx = 0;
        std::memset(buf,0,sizeof(buf));
    }
    inline int32_t process(int32_t x){ // Q15 -> Q15
        if (len == 0) return x;
        int32_t y = (int32_t)buf[idx] << 1;
        buf[idx] = (int16_t)(x >> 1);
        if (++idx >= len) idx = 0;
        return y;
    }
};

// ---- MicroVerbMonoInt ----
class MicroVerbMonoInt {
public:
    MicroVerbMonoInt(){ setDefaults(); mute(); }

    // Control-rate setters (float ok; precompute Q15)
    void setRoomSize(float v){ // 0..1 → ~0.25..0.95 feedback
        if(v<0)v=0; if(v>1)v=1;
        int32_t q = toQ15(0.25f + v*0.70f);
        room_q15 = q;
        c1.set_feedback_q15(q); c2.set_feedback_q15(q); c3.set_feedback_q15(q);
    }
    void setDamp(float v){ // 0..1 (higher = darker)
        if(v<0)v=0; if(v>1)v=1;
        int32_t q = toQ15(v);
        c1.set_damp_q15(q); c2.set_damp_q15(q); c3.set_damp_q15(q);
    }
    void setWet(float v){ if(v<0)v=0; if(v>1)v=1; wet_q15 = toQ15(v); }
    void setDry(float v){ if(v<0)v=0; if(v>1)v=1; dry_q15 = toQ15(v); }
    void setPredelayMs(float ms, float fs=48000.0f){ pre.set_ms(ms, fs); }

    // Integer (Q15) setters if desired
    void setRoomSizeQ15(int32_t q){ room_q15=clampQ15(q); c1.set_feedback_q15(q); c2.set_feedback_q15(q); c3.set_feedback_q15(q); }
    void setDampQ15(int32_t q){ q=clampQ15(q); c1.set_damp_q15(q); c2.set_damp_q15(q); c3.set_damp_q15(q); }
    void setWetQ15(int32_t q){ wet_q15=clampQ15(q); }
    void setDryQ15(int32_t q){ dry_q15=clampQ15(q); }

    void mute(){ c1.mute(); c2.mute(); c3.mute(); ap.mute(); pre.idx=0; }

    // Mono in → mono out (12-bit signed)
    inline int16_t process(int16_t in12){
        // promote to Q15 and apply small input gain
        int32_t x = (int32_t)in12 << 4;
        x = mul_q15(x, input_gain_q15);

        // pre-delay
        x = pre.process(x);

        // 3 combs in parallel
        int32_t acc = 0;
        acc += c1.process(x);
        acc += c2.process(x);
        acc += c3.process(x);

        // 1 allpass for diffusion
        int32_t wet = ap.process(acc);

        // Wet/dry mix (mono)
        int32_t y_q15 = sat_q15( mul_q15(((int32_t)in12<<4), dry_q15) + mul_q15(wet, wet_q15) );
        return sat_q12_from_q15(y_q15);
    }

private:
    // building blocks
    CombQ15<COMB1>  c1;
    CombQ15<COMB2>  c2;
    CombQ15<COMB3>  c3;
    AllpassQ15<APCORE> ap;
    PredelayQ15 pre;

    // params (Q15)
    static constexpr int32_t input_gain_q15 = 8096;   // ~0.03
    int32_t room_q15  = 30000;  // ~0.915
    int32_t wet_q15   = 9830;   // ~0.30
    int32_t dry_q15   = 19660;  // ~0.60

    static inline int32_t clampQ15(int32_t q){ if(q<0)q=0; if(q>32767) q=32767; return q; }
    static inline int32_t toQ15(float v){ if(v<0)v=0; if(v>1)v=1; return (int32_t)(v*32767.0f + 0.5f); }

    inline void setDefaults(){
        setRoomSize(0.75f);
        setDamp(0.55f);
        setWet(0.30f);
        setDry(0.65f);
        setPredelayMs(2.0f);
    }
};

} // namespace dsp
