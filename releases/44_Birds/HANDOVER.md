# Birds Handover

Workshop Computer card 44. Version 0.5.0.

## What It Is

Birds is a small procedural birdsong instrument for the Workshop Computer. It makes two related voices rather than a single decorative sound effect. The useful musical bit is the behaviour between the voices: call, answer, copy, avoid, overlap, settle, change.

This version uses synthetic syllables built from a cheap wavetable sine approximation, envelopes, pitch contours, simple AM/FM for coos, and gated pitch movement for trills and runs. There are no samples.

## Design Process

The first decision was to avoid a zoological model. Real birds are complicated and very specific. A convincing blackbird model would mostly make people notice the missing blackbird. A better Workshop Computer card is a little electronic ecology: gestures which are bird-like enough to patch, but abstract enough to become music.

The second decision was to make the Turing Machine idea audible in time, not just in pitch. A 16-bit register advances on each syllable. At high lock it repeats a 16-step phrase. At low lock it uses a longer inverted pass. In the middle it mutates. The register is decoded into primitive calls rather than direct voltages.

The third decision was to make it a duet. Two identical independent birds sounded like two test oscillators. Two birds sharing one register sounded more alive. The leader alternates, the answerer can transform what it heard, and locked mode becomes a repeatable phrase instead of a frozen sound.

## Code Shape

`BirdCard` owns the whole instrument.

`BirdVoice` is the per-voice audio state: primitive, phase, pitch contour, envelope, trill gate, modulation and current level.

`PendingStart` is the answer scheduler. It lets the second bird wait, overlap or respond after a gap without introducing a separate event system.

The important flow is:

1. Read controls and switch state.
2. Detect an external clock edge, otherwise run the internal clock.
3. On each tick, step the shift register and choose the next phrase.
4. Process both voices at 48 kHz.
5. Update audio, CV, pulses and LEDs.

## Controls And Intent

The main knob deliberately keeps the Turing Machine convention: centre changes, clockwise locks, anticlockwise locks but inverts. That means people who know the module can find the card quickly.

X is register because pitch is the fastest way to move from pigeon-ish to bat-ish.

Y is time because birdsong is mostly contour and spacing. Slowing the card down reveals phrases. Speeding it up turns it into chatter, rhythm and texture.

The switch has one performance mode and one utility mode: up is wilder articulation, down reseeds.

## Things Worth Revisiting

The internal clock is simple. It works, but a future version could make phrase-level pauses more intentional.

The primitive tables are hand-tuned. That is fine for version 0.5.0, but another pass with the card in a real patch would probably find better ratios of rest, chirp and longer call.

Pulse output from trills is useful. Melody runs also pulse internally. There may be a more musical mapping where pulse density follows syllable confidence or register rather than just articulation gates.

Audio inputs are unused. A good next step would be to let Audio In 1 disturb the shift register or FM depth, and Audio In 2 act like a habitat control: wind, dusk, panic, etc.

## Build Notes

The project builds with Pico SDK and `ComputerCard.h`.

```sh
cmake -S . -B build
cmake --build build
```

The binary metadata is set in `CMakeLists.txt` as `Birds` version `0.5.0`. The same name, card number and version are also defined in `main.cpp` for readability.
