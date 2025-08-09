#pragma once

#include <cstdint>
#include "../dsp/WaveformOsc.hpp"

class ClusterSaw {
public:
    static constexpr int MAX_OSCILLATORS = 6;  // Adjust this to change number of oscillators
    
    ClusterSaw() {
        // Initialize all sawtooth oscillators
        for(int i = 0; i < MAX_OSCILLATORS; i++) {
            oscs_[i].setSampleRate(48000.0f);
            oscs_[i].setShape(WaveformOscillator::Shape::Saw);
            
            // masterVolume = 0.25, scaled to Q12: 0.25 * 4095 ≈ 1024
            oscs_[i].setAmplitudeQ12(1024);
            
            // Initialize to base frequency (will be updated in process)
            oscs_[i].setFrequencyHz(100.0f);
        }
        
        // Initialize control state
        ctrlCounter = 0;
        lastK1 = 0xFFFF; // Force initial update
        lastK2 = 0xFFFF;
    }
    
    int32_t process(int32_t k1_4095, int32_t k2_4095) {
        // Control-rate updates (every 128 samples) to reduce expensive calculations
        if ((ctrlCounter++ & 0x7F) == 0 || 
            (k1_4095 != lastK1) || (k2_4095 != lastK2)) {
            
            lastK1 = k1_4095;
            lastK2 = k2_4095;
            
            // Convert knobs to 0.0-1.0 range
            float k1_01 = static_cast<float>(k1_4095) * (1.0f / 4095.0f);
            float k2_01 = static_cast<float>(k2_4095) * (1.0f / 4095.0f);
            
            // K1: Base frequency with quadratic response (original: pitch1 = pow(knob_1, 2))
            float pitch1 = k1_01 * k1_01;
            float f1 = 20.0f + (pitch1 * 1000.0f);       // Range: 20 Hz to 1020 Hz
            
            // K2: Multiplication factor with quadratic response (original: pitch2 = pow(knob_2, 2))
            float pitch2 = k2_01 * k2_01;
            float multFactor = 1.01f + (pitch2 * 0.9f);  // Range: 1.01 to 1.91 (original behavior)
            
            // Calculate and set frequencies for ALL oscillators (original behavior)
            float freq = f1;
            for(int i = 0; i < MAX_OSCILLATORS; i++) {
                // Clamp frequency to reasonable range
                if(freq > 8000.0f) freq = 8000.0f;
                if(freq < 10.0f) freq = 10.0f;
                
                oscs_[i].setFrequencyHz(freq);
                freq *= multFactor;  // Exponential spacing controlled by Y knob
            }
        }
        
        // Generate all oscillator outputs and mix - optimized loop
        int32_t total_mix = 0;
        for(int i = 0; i < MAX_OSCILLATORS; i++) {
            total_mix += static_cast<int32_t>(oscs_[i].nextSample());
        }
        
        // Clamp to 12-bit output range
        if(total_mix > 2047) total_mix = 2047;  
        if(total_mix < -2048) total_mix = -2048;
        
        return total_mix;
    }

private:
    // Signal Flow: 
    // MAX_OSCILLATORS sawtooth oscillators → direct mix → output
    //
    // oscs[0] through oscs[MAX_OSCILLATORS-1] → total_mix → final_mix → output
    // Number of active oscillators controlled by Y knob
    
    WaveformOscillator oscs_[MAX_OSCILLATORS];  // Configurable number of sawtooth oscillators
    
    // Control-rate optimization state
    uint32_t ctrlCounter;
    int32_t lastK1, lastK2;
};
