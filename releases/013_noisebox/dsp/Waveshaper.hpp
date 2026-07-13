// Waveshaper with lookup table and linear interpolation
// Based on Teensy AudioEffectWaveshaper by Damien Clarke
// Adapted for integer-only processing
// Input/output: 16-bit signed (-32768..32767)

#pragma once

#include <cstdint>

class Waveshaper {
private:
    int16_t* waveshape = nullptr;
    int length = 0;
    int lerpshift = 16;

public:
    Waveshaper() = default;
    
    ~Waveshaper() {
        if (waveshape) {
            delete[] waveshape;
        }
    }

    // Disable copy constructor and assignment operator
    Waveshaper(const Waveshaper&) = delete;
    Waveshaper& operator=(const Waveshaper&) = delete;

    // Set the waveshape table from float array
    // length must be a power of two + 1 (e.g., 33, 65, 129, 257, 513, 1025, etc.)
    // waveshape values should be in range [-1.0, 1.0]
    bool shape(const float* waveshape_in, int length_in) {
        // Validate input parameters
        if (!waveshape_in || length_in < 2 || length_in > 32769) {
            return false;
        }
        
        // Check if length-1 is a power of two
        int test = length_in - 1;
        if ((test & (test - 1)) != 0) {
            return false;
        }

        // Clean up existing table
        if (waveshape) {
            delete[] waveshape;
        }

        // Allocate and populate new table
        length = length_in;
        waveshape = new int16_t[length];
        
        for (int i = 0; i < length; i++) {
            // Clamp input to [-1.0, 1.0] range and convert to int16_t
            float val = waveshape_in[i];
            if (val > 1.0f) val = 1.0f;
            if (val < -1.0f) val = -1.0f;
            waveshape[i] = static_cast<int16_t>(32767.0f * val);
        }

        // Calculate lerpshift for interpolation
        // This determines how many bits to shift to map uint16_t input range
        // to waveshape table indices
        int index = length - 1;
        lerpshift = 16;
        while (index >>= 1) {
            --lerpshift;
        }

        return true;
    }

    // Set the waveshape table from int16_t array (direct copy)
    // length must be a power of two + 1
    // waveshape values should be in range [-32767, 32767]
    bool shape(const int16_t* waveshape_in, int length_in) {
        // Validate input parameters
        if (!waveshape_in || length_in < 2 || length_in > 32769) {
            return false;
        }
        
        // Check if length-1 is a power of two
        int test = length_in - 1;
        if ((test & (test - 1)) != 0) {
            return false;
        }

        // Clean up existing table
        if (waveshape) {
            delete[] waveshape;
        }

        // Allocate and copy new table
        length = length_in;
        waveshape = new int16_t[length];
        
        for (int i = 0; i < length; i++) {
            waveshape[i] = waveshape_in[i];
        }

        // Calculate lerpshift for interpolation
        int index = length - 1;
        lerpshift = 16;
        while (index >>= 1) {
            --lerpshift;
        }

        return true;
    }

    // Process a single sample with waveshaping
    // Input: 16-bit signed sample (-32768..32767)
    // Output: 16-bit signed shaped sample
    inline int16_t process(int16_t input) {
        if (!waveshape) {
            return input; // passthrough if no waveshape loaded
        }

        // Convert int16_t input to uint16_t range (0..65535)
        uint16_t x = static_cast<uint16_t>(input + 32768);
        
        // Linear interpolation (lerp) of waveshape table
        // Based on http://coranac.com/tonc/text/fixed.htm
        uint16_t xa = x >> lerpshift;           // table index (integer part)
        int16_t ya = waveshape[xa];             // value at current index
        int16_t yb = waveshape[xa + 1];         // value at next index
        
        // Linear interpolation between ya and yb
        // frac = x - (xa << lerpshift) = fractional part of index
        uint16_t frac = x - (xa << lerpshift);
        int32_t result = ya + (((int32_t)(yb - ya) * frac) >> lerpshift);
        
        return static_cast<int16_t>(result);
    }

    // Check if waveshape is loaded
    bool isReady() const {
        return waveshape != nullptr;
    }

    // Get current table length
    int getLength() const {
        return length;
    }
};
