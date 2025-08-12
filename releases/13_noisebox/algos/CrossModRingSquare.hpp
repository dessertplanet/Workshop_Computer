#pragma once

#include <cstdint>
#include "../dsp/WaveformOsc.hpp"

class CrossModRingSquare {
public:
    CrossModRingSquare() {
        // Initialize oscillators to match original:
        // waveformMod1.begin(0.8, 500, WAVEFORM_SQUARE);
        // waveformMod2.begin(0.8, 500, WAVEFORM_SQUARE);
        
        osc1_.setSampleRate(48000.0f);
        osc2_.setSampleRate(48000.0f);
        
        osc1_.setShape(WaveformOscillator::Shape::Square);
        osc2_.setShape(WaveformOscillator::Shape::Square);
        
        osc1_.setAmplitudeQ12(4095);
        osc2_.setAmplitudeQ12(4095);
        
        // Set initial frequencies to 500 Hz
        osc1_.setFrequencyHz(500.0f);
        osc2_.setFrequencyHz(500.0f);

        osc1_.setPulseWidthQ15(0.8f * 32768.0f);
        osc2_.setPulseWidthQ15(0.8f * 32768.0f);
        
        // Initialize previous outputs for cross-modulation
        prev_osc1_out_ = 0;
        prev_osc2_out_ = 0;
    }
    
    int32_t process(int32_t k1_4095, int32_t k2_4095) {
        // Map knobs to frequencies using original's approach:
        // waveformMod1.frequency(100+(pitch1*5000));  where pitch1 = pow(knob_1, 2)
        // waveformMod2.frequency(20+(pitch2*1000));   where pitch2 = pow(knob_2, 2)
        
        // Convert knobs to 0.0-1.0 range
        float k1_01 = static_cast<float>(k1_4095) / 4095.0f;
        float k2_01 = static_cast<float>(k2_4095) / 4095.0f;
        
        // Apply quadratic response: pitch = knob^2
        float pitch1 = k1_01 * k1_01;
        float pitch2 = k2_01 * k2_01;
        
        // Calculate frequencies
        float freq1 = 100.0f + (pitch1 * 5000.0f);
        float freq2 = 20.0f + (pitch2 * 1000.0f);
        
        // Update base frequencies
        osc1_.setFrequencyHz(freq1);
        osc2_.setFrequencyHz(freq2);
        
        // Cross-modulation: each oscillator's output modulates the other's frequency
        // Convert previous outputs to FM format (Q16.16 Hz)
        // The original used frequencyModulation(1) which is quite strong FM
        
        // Scale the previous outputs for FM
        // prev_osc outputs are in range -2048 to 2047
        // Scale to reasonable FM amount (similar to RadioOhNo approach)
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
    // osc1 ←→ osc2 (cross-modulation via FM inputs)
    //  ↓     ↓
    //  ring modulator → output
    
    WaveformOscillator osc1_;    // waveformMod1 equivalent
    WaveformOscillator osc2_;    // waveformMod2 equivalent
    
    // Previous outputs for cross-modulation feedback
    int16_t prev_osc1_out_;
    int16_t prev_osc2_out_;
};