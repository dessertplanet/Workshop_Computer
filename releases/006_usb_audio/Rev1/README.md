# USB Audio

USB audio output for Music Thing Modular Workshop Computer

48kHz 16-bit stereo input through USB

12-bit output through both audio out jacks.

Main knob controls volume:
- Knob at 12 o'clock is the default value (0dB amplification).
- This volume and quieter will not introduce clipping 
- Louder volumes may clip but could be useful for boosting quiet sources


Heavily based on the [Pico-USB-Audio project](https://github.com/tierneytim/Pico-USB-audio/) project by Tim Tierney, with modifications only to: 
- use MTM Computer DAC and knob
- to convert to 48kHz stereo.

#### Compiling from source

The source is in C, using the Raspberry Pi Pico SDK. To compile:

1. Copy the custom board definition `mtm_computer_16mb.h` to `<path_to_pico_sdk>/src/boards/include/boards/`. 

2. Set up the environment variable defining the path to the Pico SDK, e.g.:
    `export PICO_SDK_PATH=<path_to_pico_sdk>`
	or equivalent for your shell.
    
3. Change to `06_usb_audio/` directory

4. Make and build in the usual way for the Pico SDK:


    mkdir build
    cd build
    cmake ..
    make
    

(The custom board definition  defines `#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64`,  often needed for the RP2040 to startup).
   
   
   
----

This code is a modification of the [Pico-USB-Audio project](https://github.com/tierneytim/Pico-USB-audio/) by Tim Tierney

Adapted for Music Thing Modular Workshop Computer by [Chris Johnson](https://github.com/chrisgjohnson)
