# 30 - eighties_bass

Bass-oriented complete monosynth voice consisting of five detuned saw wave
oscillators with mixable white noise and adjustable resonant filter. 
CV controllable pitch, frequency cutoff, detune, noise mix

Video demo1: https://youtu.be/nrKnOlx8B3g

##  Controls and Audio

  - Main knob -- adjust filter cutoff frequency
  - X knob -- set pitch offset (CV1 controls voct pitch) 
  - Y knob -- set filter resonance
  - switch -- tap down to change filter
  
  - CV 1 in -- V/oct pitch
  - CV 2 in -- +/- additive to main knob filter cutoff frequency
  
  - audio L in -- CV controls detune amount
  - audio R in -- CV controls noise mix
  
  - LEDs -- LEDs 2,4,6 represent which filter mode (LPF, BPF, HPF)

## Pre-built UF2
- See `build` directory for a UF2 to copy to RPI-RP2 and you're off!


## Building 

- Uses arduino-pico Arduino core: https://github.com/earlephilhower/arduino-pico
- Uses Mozzi 2 library:  https://github.com/sensorium/Mozzi
