// Example usage of the Waveshaper class
// Demonstrates how to create common waveshaping functions

#pragma once

#include "Waveshaper.hpp"
#include <cmath>

class WaveshaperExample {
public:
    // Create a soft saturation curve (tanh-like)
    static bool createSoftSaturation(Waveshaper& ws, int tableSize = 257) {
        if ((tableSize - 1) & (tableSize - 2)) {
            return false; // tableSize-1 must be power of 2
        }
        
        float* curve = new float[tableSize];
        
        for (int i = 0; i < tableSize; i++) {
            // Map i from [0, tableSize-1] to [-3, 3] for tanh input
            float x = (i * 6.0f) / (tableSize - 1) - 3.0f;
            curve[i] = tanhf(x);
        }
        
        bool success = ws.shape(curve, tableSize);
        delete[] curve;
        return success;
    }
    
    // Create a hard clipping curve
    static bool createHardClip(Waveshaper& ws, float threshold = 0.7f, int tableSize = 257) {
        if ((tableSize - 1) & (tableSize - 2)) {
            return false; // tableSize-1 must be power of 2
        }
        
        float* curve = new float[tableSize];
        
        for (int i = 0; i < tableSize; i++) {
            // Map i from [0, tableSize-1] to [-1, 1]
            float x = (i * 2.0f) / (tableSize - 1) - 1.0f;
            
            if (x > threshold) {
                curve[i] = threshold;
            } else if (x < -threshold) {
                curve[i] = -threshold;
            } else {
                curve[i] = x;
            }
        }
        
        bool success = ws.shape(curve, tableSize);
        delete[] curve;
        return success;
    }
    
    // Create a tube-like asymmetric distortion
    static bool createTubeDistortion(Waveshaper& ws, int tableSize = 257) {
        if ((tableSize - 1) & (tableSize - 2)) {
            return false; // tableSize-1 must be power of 2
        }
        
        float* curve = new float[tableSize];
        
        for (int i = 0; i < tableSize; i++) {
            // Map i from [0, tableSize-1] to [-1, 1]
            float x = (i * 2.0f) / (tableSize - 1) - 1.0f;
            
            // Asymmetric tube-like distortion
            if (x >= 0) {
                curve[i] = x / (1.0f + 0.7f * x);
            } else {
                curve[i] = x / (1.0f - 0.3f * x);
            }
        }
        
        bool success = ws.shape(curve, tableSize);
        delete[] curve;
        return success;
    }
    
    // Create a bit-crusher-like staircase function
    static bool createBitCrush(Waveshaper& ws, int steps = 16, int tableSize = 257) {
        if ((tableSize - 1) & (tableSize - 2)) {
            return false; // tableSize-1 must be power of 2
        }
        
        float* curve = new float[tableSize];
        
        for (int i = 0; i < tableSize; i++) {
            // Map i from [0, tableSize-1] to [-1, 1]
            float x = (i * 2.0f) / (tableSize - 1) - 1.0f;
            
            // Quantize to steps
            float stepSize = 2.0f / steps;
            float quantized = floorf(x / stepSize) * stepSize;
            
            // Clamp to [-1, 1]
            if (quantized > 1.0f) quantized = 1.0f;
            if (quantized < -1.0f) quantized = -1.0f;
            
            curve[i] = quantized;
        }
        
        bool success = ws.shape(curve, tableSize);
        delete[] curve;
        return success;
    }
};
