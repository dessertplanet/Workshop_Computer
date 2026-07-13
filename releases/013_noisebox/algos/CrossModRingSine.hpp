#pragma once

#include <cstdint>
#include "../dsp/WaveformOsc.hpp"

class CrossModRingSine {
public:
    CrossModRingSine() {
        // Initialize oscillators to match original:
        // sine_fm1.frequency(1100); sine_fm1.amplitude(1);
        // sine_fm2.frequency(1367); sine_fm2.amplitude(1);
        
        osc1_.setSampleRate(48000.0f);
        osc2_.setSampleRate(48000.0f);
        
        osc1_.setShape(WaveformOscillator::Shape::Sine);
        osc2_.setShape(WaveformOscillator::Shape::Sine);
        
        // amplitude(1) = full amplitude
        osc1_.setAmplitudeQ12(4095);
        osc2_.setAmplitudeQ12(4095);
        
        // Set initial frequencies as per original
        osc1_.setFrequencyHz(1100.0f);
        osc2_.setFrequencyHz(1367.0f);
        
        // Initialize previous outputs for cross-modulation
        prev_osc1_out_ = 0;
        prev_osc2_out_ = 0;
    }
    
    int32_t process(int32_t k1_4095, int32_t k2_4095) {
        // Map knobs to frequencies using original's approach:
        // sine_fm1.frequency(100+(pitch1*8000));  where pitch1 = pow(knob_1, 2)
        // sine_fm2.frequency(60+(pitch2*3000));   where pitch2 = pow(knob_2, 2)
        
        // Convert knobs to 0.0-1.0 range
        float k1_01 = static_cast<float>(k1_4095) / 4095.0f;
        float k2_01 = static_cast<float>(k2_4095) / 4095.0f;
        
        // Apply quadratic response: pitch = knob^2
        float pitch1 = k1_01 * k1_01;
        float pitch2 = k2_01 * k2_01;
        
        // Calculate frequencies
        float freq1 = 100.0f + (pitch1 * 8000.0f);
        float freq2 = 60.0f + (pitch2 * 3000.0f);
        
        // Update base frequencies
        osc1_.setFrequencyHz(freq1);
        osc2_.setFrequencyHz(freq2);
        
        // Cross-modulation: each oscillator's output modulates the other's frequency
        // Convert previous outputs to FM format (Q16.16 Hz)
        // Scale the previous outputs for FM (similar to CrossModRingSquare)
        int32_t fm1_q16_16 = static_cast<int32_t>(prev_osc2_out_) * 32; // osc2 modulates osc1
        int32_t fm2_q16_16 = static_cast<int32_t>(prev_osc1_out_) * 32; // osc1 modulates osc2
        
        // Generate oscillator outputs with cross-modulation
        int16_t osc1_out = osc1_.nextSample(fm1_q16_16);
        int16_t osc2_out = osc2_.nextSample(fm2_q16_16);
        
        // Store for next sample's cross-modulation
        prev_osc1_out_ = osc1_out;
        prev_osc2_out_ = osc2_out;
        
        // Ring modulation: multiply the two oscillator outputs
        // Both signals are in range -2048 to 2047, so product needs scaling
        int32_t ring_mod = (static_cast<int32_t>(osc1_out) * static_cast<int32_t>(osc2_out)) >> 11; // Divide by 2048
        
        // Clamp to 12-bit output range
        if(ring_mod > 2047) ring_mod = 2047;
        if(ring_mod < -2048) ring_mod = -2048;
        
        return ring_mod;
    }

private:
    // Signal Flow (matching Teensy patch cords):
    // sine_fm1 ←→ sine_fm2 (cross-modulation via FM inputs)
    //    ↓         ↓
    //    ring modulator → output
    
    WaveformOscillator osc1_;    // sine_fm1 equivalent  
    WaveformOscillator osc2_;    // sine_fm2 equivalent
    
    // Previous outputs for cross-modulation feedback
    int16_t prev_osc1_out_;
    int16_t prev_osc2_out_;
};
