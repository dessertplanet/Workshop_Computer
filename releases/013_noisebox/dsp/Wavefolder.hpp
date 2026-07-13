// Wavefolder based on Teensy AudioEffectWaveFolder by Mark Tillotson
// Adapted for integer-only single-sample processing
// Input/output: 16-bit signed (-32768..32767)

#pragma once

#include <cstdint>

class Wavefolder {
public:
    // Extreme wavefolder with multiple fold stages and increased drive range
    // input_a: audio signal (16-bit signed) - typically the oscillator output
    // input_b: drive/amount signal (16-bit signed) - typically a DC bias or control signal
    // This enhanced version provides up to 64x folding capability with multiple stages
    inline int16_t process(int16_t input_a, int16_t input_b)
    {
        int32_t a12 = input_a;
        int32_t b12 = input_b;

        // STAGE 1: Initial aggressive scaling - up to 32 times input for more extreme folding
        // Increased from original 16x to 32x for more folds
        int32_t s1 = (a12 * b12 + 0x200) >> 10;  // Reduced shift for more gain
        
        // Multiple fold detection with finer granularity
        // Check for fold zones at multiple scales for more complex folding patterns
        bool flip1 = ((s1 + 0x4000) >> 15) & 1;
        bool flip2 = ((s1 + 0x2000) >> 14) & 1;
        
        // Apply primary folding
        s1 = 0xFFFF & (flip1 ? ~s1 : +s1);
        
        // STAGE 2: Secondary folding for even more extreme distortion
        // Apply additional fold based on amplitude for asymmetric folding
        if (abs(s1) > 0x6000) {  // If signal is still large after first fold
            bool flip3 = flip2 ^ ((s1 >> 13) & 1);
            s1 = flip3 ? (0x7FFF - (s1 & 0x7FFF)) : s1;
        }
        
        // STAGE 3: Micro-folding for high-frequency harmonics
        // Add small-scale folding for additional texture
        if (abs(s1) > 0x4000) {
            int16_t micro_fold = (s1 >> 3) & 0xFF;
            if (micro_fold > 0x80) {
                s1 ^= (micro_fold << 4);  // XOR for bitwise folding effect
            }
        }

        // Final saturation and clipping
        if (s1 > 32767) s1 = 32767;
        if (s1 < -32768) s1 = -32768;

        return static_cast<int16_t>(s1);
    }

    // Ultra-extreme processing mode with cascaded folding
    inline int16_t processExtreme(int16_t input_a, int16_t input_b, float intensity = 1.0f)
    {
        int32_t a12 = input_a;
        int32_t b12 = static_cast<int32_t>(input_b * intensity);
        
        // Clamp drive to prevent overflow
        if (b12 > 32767) b12 = 32767;
        if (b12 < -32768) b12 = -32768;

        // CASCADED FOLDING: Apply folding multiple times with different characteristics
        int32_t result = a12;
        
        for (int stage = 0; stage < 3; stage++) {
            // Variable scaling per stage
            int shift_amount = 9 + stage;  // 9, 10, 11 - progressively less aggressive
            int32_t scaled = (result * b12 + (1 << (shift_amount-1))) >> shift_amount;
            
            // Multi-level folding with different thresholds per stage
            int threshold_shift = 15 - stage;  // 15, 14, 13
            bool flip_primary = ((scaled + (1 << threshold_shift)) >> (threshold_shift + 1)) & 1;
            bool flip_secondary = ((scaled + (1 << (threshold_shift - 1))) >> threshold_shift) & 1;
            
            // Complex folding logic
            if (flip_primary) {
                scaled = ~scaled;
            }
            if (flip_secondary && stage > 0) {
                scaled = -scaled;
            }
            
            // Bit-level folding for additional harmonics
            if (stage == 2 && abs(scaled) > 0x2000) {
                scaled ^= (scaled >> 4) & 0x0FFF;
            }
            
            result = scaled & 0xFFFF;
        }
        
        // Final waveshaping for even more extreme character
        if (abs(result) > 0x5000) {
            result = (result > 0) ? 
                (0x7FFF - ((0x7FFF - result) >> 2)) :  // Soft limit positive
                (-0x8000 + ((0x8000 + result) >> 2)); // Soft limit negative
        }

        return static_cast<int16_t>(result);
    }

    // Convenience method: process with DC bias from knob value
    // input: audio signal (16-bit signed)
    // knob_0_to_1: control value 0.0 to 1.0 (matches P_resonoise: knob_2*0.2+0.03)
    inline int16_t processWithDC(int16_t input, float knob_0_to_1)
    {
        // Match P_resonoise DC amplitude calculation: knob_2*0.2+0.03
        float dc_amplitude = knob_0_to_1 * 0.2f + 0.03f;
        
        // Convert to 16-bit DC value (scale by max int16_t)
        int16_t dc_value = static_cast<int16_t>(dc_amplitude * 32767.0f);
        
        return process(input, dc_value);
    }

    // Extreme version with extended drive range
    inline int16_t processWithDCExtreme(int16_t input, float knob_0_to_1, float intensity = 2.0f)
    {
        // Extended DC amplitude range for more extreme folding
        float dc_amplitude = (knob_0_to_1 * 0.4f + 0.03f) * intensity;
        if (dc_amplitude > 1.0f) dc_amplitude = 1.0f;
        
        // Convert to 16-bit DC value
        int16_t dc_value = static_cast<int16_t>(dc_amplitude * 32767.0f);
        
        return processExtreme(input, dc_value, intensity);
    }

    // Alternative Q12 interface for integer-only control
    // input: audio signal (16-bit signed)
    // drive_q12: drive amount in Q12 format (0..4095 maps to P_resonoise range)
    inline int16_t processQ12(int16_t input, uint16_t drive_q12)
    {
        // Map drive_q12 (0..4095) to P_resonoise DC range (0.03..0.23)
        // dc_amplitude = (drive_q12/4095.0) * 0.2 + 0.03
        int32_t dc_scaled = ((static_cast<int32_t>(drive_q12) * 6554) >> 16) + 983; // 0.03*32767 ≈ 983
        if (dc_scaled > 32767) dc_scaled = 32767;
        
        int16_t dc_value = static_cast<int16_t>(dc_scaled);
        return process(input, dc_value);
    }

    // Extreme Q12 interface with extended range
    // drive_q12: drive amount in Q12 format (0..4095 maps to extended extreme range)
    inline int16_t processQ12Extreme(int16_t input, uint16_t drive_q12, uint16_t intensity_q12 = 2048)
    {
        // Map drive_q12 (0..4095) to extended DC range (0.03..0.8) for more extreme folding
        int32_t dc_scaled = ((static_cast<int32_t>(drive_q12) * 25231) >> 16) + 983; // 0.8*32767 ≈ 26214
        if (dc_scaled > 32767) dc_scaled = 32767;
        
        // Map intensity_q12 (0..4095) to intensity multiplier (0.0..4.0)
        float intensity = static_cast<float>(intensity_q12) / 1024.0f; // 4095/1024 ≈ 4.0
        
        int16_t dc_value = static_cast<int16_t>(dc_scaled);
        return processExtreme(input, dc_value, intensity);
    }
};


