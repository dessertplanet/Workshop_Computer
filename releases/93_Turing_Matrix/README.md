# Turing Matrix

This is a draft Workshop Computer card built from the official
**03_Turing_Machine** firmware, reshaped into two switch-selected layers inspired by the
Music Thing Modular **Turing Machine + Vactrol Mix Expander** combination.

The original Vactrol Mix Expander is a four-input, two-output vactrol matrix mixer
for the hardware Turing Machine. On the Workshop Computer we have two audio inputs,
two CV inputs, two audio/CV outputs, two CV outputs, and two pulse outputs, so this
card treats the idea as a two-input, two-output random matrix/mixer.

## Basic idea

- One shared background Turing engine keeps running in both layers.
- **Z middle** is the Turing control layer and is intended to feel like the original Turing card.
- **Z up** is the Vactrol Mix layer and uses the background Turing engine to animate a two-input,
  two-output audio mixer.
- **Z down** remains tap tempo.
- **Pulse Out 1** and **Pulse Out 2** keep the same clock/Turing pulse behavior in both layers.
- **CV Out 1** and **CV Out 2** stay quantized pitch outputs in `Z middle`, and become mirrored
  crossfaded CV outputs in `Z up`.

## Controls

- **Z middle**
- Main knob: Turing randomness / write amount.
- X knob: loop length.
- Y knob: channel 2 divide/multiply relationship.
- Audio/CV In 1 and 2 are unused.

- **Z up**
- Main knob: vactrol lag / slew time.
- X knob: crossfade depth for Audio Out 1.
- Y knob: crossfade depth for Audio Out 2.
- Audio/CV In 1 and 2 become the two audio inputs to the mixer.

## LED feedback

- **Z middle** keeps the inherited Turing-style LED view:
  DAC lane 1, DAC lane 2, PWM lane 1, PWM lane 2, then pulse activity on LEDs 5 and 6.
- **Z up** switches the four brightness LEDs to mixer feedback:
  mix position 1, mix position 2, depth 1, depth 2, with pulse activity still shown on LEDs 5 and 6.

- **Z down**
- Tap tempo when no external clock is patched.

## Inputs

- **Pulse In 1**: external clock for the main Turing channel.
- **Pulse In 2**: independent clock for channel 2.
- **CV In 1**: divide/multiply modulation for channel 2 in `Z middle`, CV mix input 1 in `Z up`.
- **CV In 2**: quantized pitch offset in `Z middle`, CV mix input 2 in `Z up`.
- **Audio/CV In 1**: mixer input 1 in the Vactrol Mix layer.
- **Audio/CV In 2**: mixer input 2 in the Vactrol Mix layer.

## Outputs

- **Pulse Out 1**: matrix gate 1.
- **Pulse Out 2**: matrix gate 2.
- **CV Out 1**: channel 1 quantized pitch CV in `Z middle`, crossfaded CV output 1 in `Z up`.
- **CV Out 2**: channel 2 quantized pitch CV in `Z middle`, crossfaded CV output 2 in `Z up`.
- **Audio Out 1**: DAC CV output in the Turing layer, mixed audio output 1 in the Vactrol Mix layer.
- **Audio Out 2**: DAC CV output in the Turing layer, mixed audio output 2 in the Vactrol Mix layer.

## Vactrol Mix behavior

In the Vactrol Mix layer, the current implementation uses the two Turing DAC lanes as smoothed
crossfade controls. Audio Out 1 crossfades Audio In 1 against Audio In 2, and Audio Out 2
crossfades Audio In 2 against Audio In 1.

The same crossfade is mirrored on the CV pair: CV Out 1 crossfades CV In 1 against CV In 2, and
CV Out 2 crossfades CV In 2 against CV In 1.

That gives a practical Workshop Computer interpretation of the Vactrol Mix idea:
shared random movement, click-softened transitions, and stereo drift driven by the background
Turing machine. The audio inputs can therefore be used for audio or slow CV, while the CV inputs
provide a dedicated CV-only version of the same motion.

## Status

Draft implementation. The firmware source is copied from card **03_Turing_Machine** with:

- the internal card number changed to 93
- the original Audio In 1 reset behavior removed
- the original Audio In 2 switch override behavior removed
- a new `Z up` Vactrol Mix audio layer added on top of the shared Turing engine

## Web editor

This card now needs its own editor model because the switch no longer selects two Turing presets.
The local `web/` editor is for the shared Turing engine settings only: timing, scale/range, pulse
behavior, and related persistent options. It also stores a first batch of Vactrol-layer settings:
crossfade law, lane relation, rise/fall timing, and per-lane minimum/maximum windows. The panel
still handles the live lag and crossfade depth gestures in `Z up`.
