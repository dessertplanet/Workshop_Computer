# reverb

A simple reverb (on audio inputs/outputs) **plus** configurable MIDI interface / clock / Turing machine / bernoulli gate ( on CV / pulses inputs/outputs), for Music Thing Workshop System Computer.

See card documentation on the [Music Thing Modular website](https://www.musicthing.co.uk/Computer_Program_Cards/#20-reverb).

Reverb algorithm is that in the [Dattorro paper](https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf) (J. Audio Eng. Soc., **45**(9), 1997, 660&ndash;684), modified to damp either high or low frequencies in the reverb tail.

## Use:

A `uf2` file is included in the `build` directory.

#### Compiling from source

The source is in C, using the Raspberry Pi Pico SDK. To compile:

1. Set up the environment variable defining the path to the Pico SDK, e.g.:
    `export PICO_SDK_PATH=<path_to_pico_sdk>`
	or equivalent for your shell.
    
2. Change to `20_reverb/` directory

3. Make and build in the usual way for the Pico SDK:


    mkdir build
    cd build
    cmake ..
    make
    
   
----

Author: [Chris Johnson](https://github.com/chrisgjohnson)

Reverb DSP derived from code by [Pauli Pölkki](https://github.com/el-visio/dattorro-verb ) 
