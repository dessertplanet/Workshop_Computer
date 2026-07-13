# 30 - cirpy_wavetable 

Wavetable oscillator that using wavetables from Plaits, Braids, and Microwave, 
or any other [wavetable WAVs from waveeditonline](http://waveeditonline.com/index-17.html)

Video demo1: https://youtu.be/Y7sOgAC92XU

Video demo2: https://youtu.be/Nxx_5Xhv1X8

##  Controls and Audio

  - Main knob   -- controls wavetable position 
  - X knob      -- controls amount of triangle wave LFO to add to wavetable position 
  - Y knob      -- controls frequency of wavemod LFO
  - Z switch down -- selects next wavetable
  - Z switch up   -- toggles quantized notes or not on CV 1 Input
  
  - CV 1 In     -- pitch (0-5V) should kinda track 1V/oct, with nothing plugged in note = 36 MIDI
  - CV 2 In     -- adds to main knob to control wavetable position

  - CV 1 Out    -- reflects current wavetable position
  - CV 2 Out    -- reflects wavemod LFO
  
  - Pulse 1 & 2 Out -- PWM audio out

## Pre-built UF2
- See `build` directory for a UF2 to copy to RPI-RP2 and you're off!
- Also availble at https://github.com/todbot/Hello_Computer/releases/

The prebuilt UF2 contains the following wavetables:

- PLAITS02.WAV -- Wavetable from [Mutable Instruments Plaits](https://mutable-instruments.net/modules/plaits/)
- PLAITS01.WAV -- Wavetable from [Mutable Instruments Plaits](https://mutable-instruments.net/modules/plaits/)
- BRAIDS01.WAV -- Wavetable from [Mutable Instruments Braids](https://mutable-instruments.net/modules/braids/)
- BRAIDS02.WAV -- Wavetable from [Mutable Instruments Braids](https://mutable-instruments.net/modules/braids/)
- BRAIDS03.WAV -- Wavetable from [Mutable Instruments Braids](https://mutable-instruments.net/modules/braids/)
- MICROW02.WAV -- "A curated set of wavetables from the [Waldorf Microwave 2](https://www.vintagesynth.com/waldorf/microwave-ii)"

## Setup if not using the pre-built UF2 file 

1. Install MTM CircuitPython UF2 from https://github.com/todbot/circuitpython/releases

2. Copy the [`mtm_computer.py` file](https://github.com/TomWhitwell/Hello_Computer/tree/main/Demonstrations%2BHelloWorlds/CircuitPython) to the CIRCUITPY drive.

3. Copy the `code.py` and `wav` directory to CIRCUITPY drive.

4. Install the extra CircuitPython libraries with `circup install -r requirements.txt`

5. Can use any [other WAV wavetables from waveeditonline.com](http://waveeditonline.com/index-17.html)
   including other Plaits, Braids, and PPG wavetables, just drop them in `wav`. 
   The order they come up in the oscillator is based on their names.
