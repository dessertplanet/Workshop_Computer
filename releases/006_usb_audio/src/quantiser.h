// Basic Quantizer Helper
// Based on ComputerCard examples

#ifndef QUANTISER_H
#define QUANTISER_H

// Basic definition if ComputerCard headers aren't present yet
#ifndef __not_in_flash_func
#define __not_in_flash_func(x) x
#endif

// Quantize input (-2048 to 2047) to MIDI NoteRelative (0-12 approx)
// Input: -2048 (0V approx) to 2047 (5V approx?)
// Map input to Note Number relative to C3 (60).
// 12-bit ADC (scaled to signed 12-bit)
// This simple version quantizes to Semitones.
static inline int16_t quantSample(int16_t input)
{
    // Input range: -2048 to 2047.
    // Hardware Range: -6V to +6V (Total 12V span).
    // Units per Volt: 4096 / 12 = 341.33.
    // 1V/Octave Standard -> 12 Semitones per Volt.
    // Semitones = (Volts * 12).
    // Note Delta = (InputUnits / UnitsPerVolt) * 12
    //            = (Input * 12) / (4096 / 12)
    //            = (Input * 144) / 4096
    //            = (Input * 9) / 256.
    
    // Base Note: 0V (Input 0) = C3 (MIDI 60).
    int16_t note = 60 + (input * 9) / 256;
    
    // Clamp to valid MIDI range (0-127)
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    
    return note;
}

#endif
