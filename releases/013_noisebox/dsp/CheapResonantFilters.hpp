// Cheap resonant filter implementations optimized for performance
// All filters use int32 arithmetic and avoid expensive operations
// Based on README.md guidelines for ~20μs per sample constraint

#pragma once

#include <cstdint>

// 1. Simple One-Pole with Feedback (cheapest option)
// Resonance from delayed feedback - very efficient but limited resonance
class OnePoleResonant {
public:
    OnePoleResonant() { reset(); }

    void reset() { 
        y1 = 0; 
        delay1 = 0;
        delay2 = 0;
    }

    // Set cutoff frequency coefficient (0..32767 in Q15, higher = more high freq)
    void setCutoffQ15(int32_t f_q15) {
        if (f_q15 < 0) f_q15 = 0;
        if (f_q15 > 32767) f_q15 = 32767;
        cutoff_q15 = f_q15;
    }

    // Set resonance (0..32767 in Q15, higher = more resonance)
    void setResonanceQ15(int32_t res_q15) {
        if (res_q15 < 0) res_q15 = 0;
        if (res_q15 > 32000) res_q15 = 32000; // limit to prevent instability
        resonance_q15 = res_q15;
    }

    // Process one sample - extremely fast
    inline int16_t process(int16_t x) {
        // Add resonance feedback from delayed output
        int32_t feedback = (static_cast<int64_t>(delay2) * resonance_q15) >> 15;
        int32_t x_fb = x - static_cast<int16_t>(feedback);
        
        // Simple one-pole lowpass: y = x * f + y1 * (1-f)
        int32_t y = ((static_cast<int64_t>(x_fb) * cutoff_q15) >> 15) + 
                   ((static_cast<int64_t>(y1) * (32768 - cutoff_q15)) >> 15);
        
        // Update delay line for resonance
        delay2 = delay1;
        delay1 = y1;
        y1 = y;
        
        // Clamp and return
        if (y < -2048) y = -2048;
        if (y > 2047) y = 2047;
        return static_cast<int16_t>(y);
    }

private:
    int32_t y1;           // Previous output
    int32_t delay1, delay2; // Delay line for feedback
    int32_t cutoff_q15;   // Cutoff coefficient in Q15
    int32_t resonance_q15; // Resonance amount in Q15
};

// 2. Fixed Biquad Lowpass (proper implementation)
// Based on Audio EQ Cookbook with integer arithmetic
class BiquadResonant {
public:
    BiquadResonant() { reset(); }

    void reset() {
        x1 = x2 = y1 = y2 = 0;
        // Initialize to bypass (no filtering)
        b0 = 32768; b1 = 0; b2 = 0; // Q15: 1, 0, 0
        a1 = 0; a2 = 0;
    }

    // Set cutoff frequency using normalized frequency (0..16383 in Q15)
    // freq_norm = freq_hz / (sample_rate/2), then * 32768
    void setCutoffQ15(int32_t freq_norm_q15) {
        if (freq_norm_q15 < 100) freq_norm_q15 = 100;
        if (freq_norm_q15 > 16000) freq_norm_q15 = 16000;
        
        // Convert Q15 normalized freq to omega: omega = pi * freq_norm
        // Use lookup table approximation for sin/cos (for now, use simple approx)
        // omega ≈ (freq_norm_q15 * 3.14159) / 32768
        
        // Simple approximation: omega ≈ freq_norm_q15 * 0.000096 (3.14159/32768)
        // For integer math, use: omega_q15 ≈ (freq_norm_q15 * 3142) >> 15
        int32_t omega_q15 = (static_cast<int64_t>(freq_norm_q15) * 3142) >> 15; // ~pi * freq_norm
        
        // Approximate sin(omega) and cos(omega) for small angles
        // sin(x) ≈ x for small x, cos(x) ≈ 1 - x²/2
        int32_t sin_omega = omega_q15; // sin ≈ omega for small angles
        int32_t cos_omega = 32768 - ((static_cast<int64_t>(omega_q15) * omega_q15) >> 16); // cos ≈ 1 - omega²/2
        
        // alpha = sin(omega) / (2 * Q) = sin_omega / (2 * Q)
        // Since resonance_q15 = 1/Q, alpha = sin_omega * resonance_q15 / 2
        int32_t alpha = (static_cast<int64_t>(sin_omega) * resonance_q15) >> 16; // /2 built into calculation
        
        // Biquad lowpass coefficients:
        // b0 = (1 - cos_omega) / 2
        // b1 = 1 - cos_omega = 2 * b0  
        // b2 = b0
        // a0 = 1 + alpha
        // a1 = -2 * cos_omega
        // a2 = 1 - alpha
        
        int32_t one_minus_cos = 32768 - cos_omega; // 1 - cos in Q15
        int32_t b0_raw = one_minus_cos >> 1; // (1 - cos) / 2
        int32_t b1_raw = one_minus_cos;      // 1 - cos
        int32_t b2_raw = b0_raw;             // same as b0
        
        int32_t a0 = 32768 + alpha;          // 1 + alpha in Q15
        int32_t a1_raw = -(cos_omega << 1);  // -2 * cos
        int32_t a2_raw = 32768 - alpha;      // 1 - alpha
        
        // Normalize by a0: divide all coefficients by a0
        // Use 64-bit arithmetic to avoid overflow
        int64_t a0_64 = a0;
        b0 = static_cast<int32_t>((static_cast<int64_t>(b0_raw) << 15) / a0_64);
        b1 = static_cast<int32_t>((static_cast<int64_t>(b1_raw) << 15) / a0_64);
        b2 = static_cast<int32_t>((static_cast<int64_t>(b2_raw) << 15) / a0_64);
        a1 = static_cast<int32_t>((static_cast<int64_t>(a1_raw) << 15) / a0_64);
        a2 = static_cast<int32_t>((static_cast<int64_t>(a2_raw) << 15) / a0_64);
    }

    void setResonanceQ15(int32_t q_inv_q15) {
        if (q_inv_q15 < 1000) q_inv_q15 = 1000;   // Min Q ~= 32
        if (q_inv_q15 > 25000) q_inv_q15 = 25000; // Max Q ~= 1.3
        resonance_q15 = q_inv_q15;
    }

    inline int16_t process(int16_t x) {
        // Biquad: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
        int64_t y = (static_cast<int64_t>(b0) * x) + 
                   (static_cast<int64_t>(b1) * x1) + 
                   (static_cast<int64_t>(b2) * x2) - 
                   (static_cast<int64_t>(a1) * y1) - 
                   (static_cast<int64_t>(a2) * y2);
        
        y >>= 15; // Convert from Q30 to Q15
        
        // Update delay line
        x2 = x1; x1 = x;
        y2 = y1; y1 = static_cast<int32_t>(y);
        
        // Clamp to 12-bit output
        if (y < -2048) y = -2048;
        if (y > 2047) y = 2047;
        return static_cast<int16_t>(y);
    }

private:
    int32_t x1, x2, y1, y2; // Delay line
    int32_t b0, b1, b2;     // Feedforward coefficients (Q15)
    int32_t a1, a2;         // Feedback coefficients (Q15)
    int32_t resonance_q15;  // 1/Q in Q15 format
};

// 3. Moog Ladder Approximation (best resonance, highest cost but still much cheaper than SVF)
// Single-stage approximation of Moog ladder filter
class MoogLadderApprox {
public:
    MoogLadderApprox() { reset(); }

    void reset() {
        stage1 = stage2 = stage3 = stage4 = 0;
    }

    // Set cutoff as frequency coefficient (0..32767 in Q15)
    void setCutoffQ15(int32_t f_q15) {
        if (f_q15 < 50) f_q15 = 50;
        if (f_q15 > 8000) f_q15 = 8000;
        cutoff_q15 = f_q15;
    }

    // Set resonance (0..32767 in Q15, higher = more resonance)
    void setResonanceQ15(int32_t res_q15) {
        if (res_q15 < 0) res_q15 = 0;
        if (res_q15 > 31000) res_q15 = 31000; // Prevent instability
        resonance_q15 = res_q15;
    }

    inline int16_t process(int16_t x) {
        // Moog ladder approximation with 4 one-pole stages + feedback
        // Feedback from output to input
        int32_t feedback = (static_cast<int64_t>(stage4) * resonance_q15) >> 15;
        int32_t input = x - static_cast<int16_t>(feedback);
        
        // Four cascaded one-pole lowpass stages
        // stage = input * f + stage * (1-f)
        int32_t inv_cutoff = 32768 - cutoff_q15;
        
        stage1 = ((static_cast<int64_t>(input) * cutoff_q15) >> 15) + 
                ((static_cast<int64_t>(stage1) * inv_cutoff) >> 15);
                
        stage2 = ((static_cast<int64_t>(stage1) * cutoff_q15) >> 15) + 
                ((static_cast<int64_t>(stage2) * inv_cutoff) >> 15);
                
        stage3 = ((static_cast<int64_t>(stage2) * cutoff_q15) >> 15) + 
                ((static_cast<int64_t>(stage3) * inv_cutoff) >> 15);
                
        stage4 = ((static_cast<int64_t>(stage3) * cutoff_q15) >> 15) + 
                ((static_cast<int64_t>(stage4) * inv_cutoff) >> 15);
        
        // Clamp and return final stage
        int32_t output = stage4;
        if (output < -2048) output = -2048;
        if (output > 2047) output = 2047;
        return static_cast<int16_t>(output);
    }

private:
    int32_t stage1, stage2, stage3, stage4; // Four filter stages
    int32_t cutoff_q15;    // Cutoff frequency coefficient in Q15
    int32_t resonance_q15; // Resonance amount in Q15
};

// 4. Ultra-Fast Resonant Filter (compromise between quality and speed)
// Uses simplified resonance calculation with minimal delay
class UltraFastResonant {
public:
    UltraFastResonant() { reset(); }

    void reset() { y1 = y2 = 0; }

    void setCutoffQ15(int32_t f_q15) {
        if (f_q15 < 100) f_q15 = 100;
        if (f_q15 > 16000) f_q15 = 16000;
        cutoff_q15 = f_q15;
    }

    void setResonanceQ15(int32_t res_q15) {
        if (res_q15 < 0) res_q15 = 0;
        if (res_q15 > 30000) res_q15 = 30000;
        resonance_q15 = res_q15;
    }

    // Extremely fast - just 3 multiplies and some adds/shifts
    inline int16_t process(int16_t x) {
        // Two-pole filter with simplified resonance
        // y[n] = x*f + y1*(2-f-res) - y2*(1-f)
        int32_t f = cutoff_q15;
        int32_t coeff1 = (2 << 15) - f - ((static_cast<int64_t>(resonance_q15) * f) >> 15);
        int32_t coeff2 = (1 << 15) - f;
        
        int32_t y = ((static_cast<int64_t>(x) * f) >> 15) +
                   ((static_cast<int64_t>(y1) * coeff1) >> 15) -
                   ((static_cast<int64_t>(y2) * coeff2) >> 15);
        
        // Update delay line
        y2 = y1;
        y1 = y;
        
        // Clamp output
        if (y < -2048) y = -2048;
        if (y > 2047) y = 2047;
        return static_cast<int16_t>(y);
    }

private:
    int32_t y1, y2;        // Two delay elements
    int32_t cutoff_q15;    // Cutoff coefficient in Q15
    int32_t resonance_q15; // Resonance amount in Q15
};
