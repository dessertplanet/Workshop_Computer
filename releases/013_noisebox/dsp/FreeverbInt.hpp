#pragma once
#include <cstdint>
#include <cstring>

namespace dsp {

// ---- small Q15 helpers ----
static inline int32_t sat_q15(int32_t v){ if(v<-32768) return -32768; if(v>32767) return 32767; return v; }
static inline int16_t sat_q12_from_q15(int32_t q15){
    int32_t y = q15 >> 4; if (y < -2048) y = -2048; if (y > 2047) y = 2047; return (int16_t)y;
}
static inline int32_t mul_q15(int32_t a,int32_t b){ // rounded (a*b)>>15
    int64_t p=(int64_t)a*b; int64_t adj=(p>=0)?(1ll<<14):((1ll<<14)-1); return (int32_t)((p+adj)>>15);
}

// ---- Tunings (trimmed) ----
static constexpr int stereo_spread = 23;
static constexpr int combL_tunings[5]    = { 1188, 1277, 1356, 1491, 1617 };
static constexpr int allpassL_tunings[3] = { 556,  441,  341  };

// ---- Delay primitives ----
template<int N>
struct CombQ15 {
    int16_t buf[N] = {};
    int idx = 0;
    int32_t filterstore = 0; // Q15
    int32_t feedback = 0;    // Q15
    int32_t damp1 = 0, damp2 = 32767;

    inline void set_feedback_q15(int32_t fb){ feedback = fb; }
    inline void set_damp_q15(int32_t d){ if(d<0)d=0; if(d>32767)d=32767; damp1=d; damp2=32767-d; }
    inline void mute(){ std::memset(buf,0,sizeof(buf)); idx=0; filterstore=0; }

    inline int32_t process(int32_t x){
        int32_t y = (int32_t)buf[idx] << 1; // int16→Q15
        int32_t fs = mul_q15(y, damp2) + mul_q15(filterstore, damp1);
        filterstore = sat_q15(fs);
        int32_t w = x + mul_q15(filterstore, feedback);
        if (w < -32768) w = -32768; if (w > 32767) w = 32767;
        buf[idx] = (int16_t)(w >> 1);
        if (++idx >= N) idx = 0;
        return y;
    }
};

template<int N>
struct AllpassQ15 {
    int16_t buf[N] = {};
    int idx = 0;
    int32_t feedback = 16384; // ~0.5

    inline void set_feedback_q15(int32_t fb){ if(fb<-32768)fb=-32768; if(fb>32767)fb=32767; feedback=fb; }
    inline void mute(){ std::memset(buf,0,sizeof(buf)); idx=0; }

    inline int32_t process(int32_t x){
        int32_t b = (int32_t)buf[idx] << 1;
        int32_t y = sat_q15(b - x);
        int32_t w = x + mul_q15(b, feedback);
        if (w < -32768) w = -32768; if (w > 32767) w = 32767;
        buf[idx] = (int16_t)(w >> 1);
        if (++idx >= N) idx = 0;
        return y;
    }
};

// ---- FreeverbLiteInt (mono path using 3 combs + 2 allpasses) ----
class FreeverbLiteInt {
public:
    FreeverbLiteInt(){ setDefaults(); mute(); }

    // Control-rate setters
    void setRoomSize(float v){ if(v<0)v=0; if(v>1)v=1; roomsize_q15 = toQ15(0.28f + v*0.69f); refreshCombFeedbacks(); }
    void setDamp(float v){ if(v<0)v=0; if(v>1)v=1; damp_q15 = toQ15(v); applyDampAll(); }
    void setWet(float v){ if(v<0)v=0; if(v>1)v=1; wet_q15 = toQ15(v); updateWetGains(); }
    void setWidth(float v){ if(v<0)v=0; if(v>1)v=1; width_q15 = toQ15(v); updateWetGains(); }
    void setDry(float v){ if(v<0)v=0; if(v>1)v=1; dry_q15 = toQ15(v); }
    void setFreeze(bool on){
        freeze_ = on;
        if (freeze_){
            roomsize_q15 = 32700; damp_q15 = 0;
            applyDampAll();
            refreshCombFeedbacks();
            input_gain_q15 = 0;
        } else {
            input_gain_q15 = fixed_gain_q15;
        }
    }

    // Q15 setters if you prefer
    void setRoomSizeQ15(int32_t q){ roomsize_q15 = clampQ15(q); refreshCombFeedbacks(); }
    void setDampQ15(int32_t q){ damp_q15 = clampQ15(q); applyDampAll(); }
    void setWetQ15(int32_t q){ wet_q15 = clampQ15(q); updateWetGains(); }
    void setWidthQ15(int32_t q){ width_q15 = clampQ15(q); updateWetGains(); }
    void setDryQ15(int32_t q){ dry_q15 = clampQ15(q); }

    void mute(){
        combL0.mute(); combL1.mute(); combL2.mute(); combL3.mute(); combL4.mute();
        combR0.mute(); combR1.mute(); combR2.mute(); combR3.mute(); combR4.mute();
        allpassL0.mute(); allpassL1.mute(); allpassL2.mute();
        allpassR0.mute(); allpassR1.mute(); allpassR2.mute();
    }

    // Mono-in → mono-out; 12-bit signed I/O
    inline int16_t process(int16_t in12){
        const int32_t x_q15 = (int32_t)in12 << 4;
        const int32_t xin   = mul_q15(x_q15, input_gain_q15);

        // parallel combs (mono path, 3 combs for extreme test)
        int32_t accL = 0;
        accL += combL0.process(xin);
        accL += combL1.process(xin);
        accL += combL2.process(xin);

        // serial allpasses (mono path, 2 stages)
        int32_t yL = allpassL0.process(accL);
        yL = allpassL1.process(yL);

        // wet/dry mix (mono)
        const int32_t dry_in = mul_q15(x_q15, dry_q15);
        const int32_t out_q15 = sat_q15(dry_in + mul_q15(yL, wet_q15));
        return sat_q12_from_q15(out_q15);
    }

private:
    // concrete delays
    CombQ15<combL_tunings[0]> combL0; CombQ15<combL_tunings[1]> combL1;
    CombQ15<combL_tunings[2]> combL2; CombQ15<combL_tunings[3]> combL3;
    CombQ15<combL_tunings[4]> combL4;

    CombQ15<combL_tunings[0] + stereo_spread> combR0;
    CombQ15<combL_tunings[1] + stereo_spread> combR1;
    CombQ15<combL_tunings[2] + stereo_spread> combR2;
    CombQ15<combL_tunings[3] + stereo_spread> combR3;
    CombQ15<combL_tunings[4] + stereo_spread> combR4;

    AllpassQ15<allpassL_tunings[0]> allpassL0;
    AllpassQ15<allpassL_tunings[1]> allpassL1;
    AllpassQ15<allpassL_tunings[2]> allpassL2;

    AllpassQ15<allpassL_tunings[0] + stereo_spread> allpassR0;
    AllpassQ15<allpassL_tunings[1] + stereo_spread> allpassR1;
    AllpassQ15<allpassL_tunings[2] + stereo_spread> allpassR2;

    // Params (Q15)
    static constexpr int32_t fixed_gain_q15 = 492; // ~0.015
    int32_t input_gain_q15 = fixed_gain_q15;

    int32_t roomsize_q15 = toQ15(0.55f);
    int32_t damp_q15     = toQ15(0.5f);
    int32_t wet_q15      = toQ15(0.35f);
    int32_t width_q15    = toQ15(1.0f);
    int32_t dry_q15      = toQ15(0.7f);

    int32_t wet1_q15 = 0, wet2_q15 = 0;
    bool freeze_ = false;

    // utils
    static inline int32_t clampQ15(int32_t q){ if(q<0)q=0; if(q>32767) q=32767; return q; }
    static inline int32_t toQ15(float v){ if(v<0)v=0; if(v>1)v=1; return (int32_t)(v*32767.0f + 0.5f); }

    inline void applyDampAll(){
        combL0.set_damp_q15(damp_q15); combL1.set_damp_q15(damp_q15);
        combL2.set_damp_q15(damp_q15); combL3.set_damp_q15(damp_q15);
        combL4.set_damp_q15(damp_q15);
        combR0.set_damp_q15(damp_q15); combR1.set_damp_q15(damp_q15);
        combR2.set_damp_q15(damp_q15); combR3.set_damp_q15(damp_q15);
        combR4.set_damp_q15(damp_q15);
    }

    inline void refreshCombFeedbacks(){
        combL0.set_feedback_q15(roomsize_q15); combR0.set_feedback_q15(roomsize_q15);
        combL1.set_feedback_q15(roomsize_q15); combR1.set_feedback_q15(roomsize_q15);
        combL2.set_feedback_q15(roomsize_q15); combR2.set_feedback_q15(roomsize_q15);
        combL3.set_feedback_q15(roomsize_q15); combR3.set_feedback_q15(roomsize_q15);
        combL4.set_feedback_q15(roomsize_q15); combR4.set_feedback_q15(roomsize_q15);
    }

    inline void updateWetGains(){
        const int32_t half = 16384;
        int32_t w_over2 = width_q15 >> 1;
        wet1_q15 = mul_q15(wet_q15, sat_q15(w_over2 + half));
        int32_t one_minus_width_over2 = (32767 - width_q15) >> 1;
        wet2_q15 = mul_q15(wet_q15, one_minus_width_over2);
    }

    inline void setDefaults(){
        applyDampAll();
        refreshCombFeedbacks();
        updateWetGains();
    }
};

} // namespace dsp
