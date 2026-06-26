# Turing Matrix

This is a draft Workshop Computer card built from the official
**03_Turing_Machine** firmware, reshaped into two switch-selected layers inspired by the
Music Thing Modular **Turing Machine + Vactrol Mix Expander** combination.

The original Vactrol Mix Expander is a four-input, two-output vactrol matrix mixer
for the hardware Turing Machine. On the Workshop Computer we have two audio inputs,
two CV inputs, two audio/CV outputs, two CV outputs, and two pulse outputs, so this
card treats the idea as a two-input, two-output random matrix/mixer.

## Basic idea

- **Z middle** is the Turing control layer and is intended to feel like the original Turing card.
- **Z up** is the Vactrol Mix layer and uses the card's Turing-style control signals to animate a
  two-input, two-output audio/CV mixer.
- **Z down** remains tap tempo.
- **Pulse Out 1** and **Pulse Out 2** keep the same clock/Turing pulse behavior in both layers.
- **CV Out 1** and **CV Out 2** stay quantized pitch outputs in `Z middle`, and become mirrored
  crossfaded CV outputs in `Z up`.
- On startup the active layer reads the physical knob positions directly; pickup only applies after
  switching between layers so the controls do not jump when you return to a layer.

## Controls

**Z middle**
- Main knob: Turing randomness / write amount.
- X knob: loop length.
- Y knob: channel 2 divide/multiply relationship.
- Audio/CV In 1 is ignored in normal use, and Audio/CV In 2 can still be patched as a CV offset
  source in the current implementation.

**Z up**
- Main knob: vactrol lag / slew time.
- X knob: crossfade depth for Audio Out 1.
- Y knob: crossfade depth for Audio Out 2.
- Audio/CV In 1 and 2 are the two mixer inputs. The audio inputs can also be used as slow CV
  sources, but the card is not trying to detect or re-scale them differently.

## LED feedback

- **Z middle** keeps the inherited Turing-style LED view:
  DAC lane 1, DAC lane 2, PWM lane 1, PWM lane 2, then pulse activity on LEDs 5 and 6.
- **Z up** switches the four brightness LEDs to mixer feedback:
  mix position 1, mix position 2, depth 1, depth 2, with pulse activity still shown on LEDs 5 and 6.
- **Z down** is tap tempo when no external clock is patched.

## Inputs

- **Pulse In 1**: external clock for the main Turing channel.
- **Pulse In 2**: independent clock for channel 2.
- **CV In 1**: divide/multiply modulation for channel 2 in `Z middle`, CV mix input 1 in `Z up`.
- **CV In 2**: quantized pitch offset in `Z middle`, CV mix input 2 in `Z up`.
- **Audio/CV In 1**: mixer input 1 in the Vactrol Mix layer.
- **Audio/CV In 2**: mixer input 2 in the Vactrol Mix layer.

The CV inputs are still available in the mixer layer, and the current implementation mirrors the
audio crossfade behavior there so they can be used as CV or audio sources depending on the patch.

## Outputs

- **Pulse Out 1**: matrix gate 1.
- **Pulse Out 2**: matrix gate 2.
- **CV Out 1**: channel 1 quantized pitch CV in `Z middle`, crossfaded CV output 1 in `Z up`.
- **CV Out 2**: channel 2 quantized pitch CV in `Z middle`, crossfaded CV output 2 in `Z up`.
- **Audio Out 1**: direct pass-through of Audio In 1 in `Z middle`, mixed audio output 1 in `Z up`.
- **Audio Out 2**: direct pass-through of Audio In 2 in `Z middle`, mixed audio output 2 in `Z up`.

## Vactrol Mix behavior

In the Vactrol Mix layer, the card uses the Turing-style control signal as a smoothed crossfade
driver. Audio Out 1 crossfades Audio In 1 against Audio In 2, and Audio Out 2 crossfades Audio In 2
against Audio In 1.

The same crossfade is mirrored on the CV pair: CV Out 1 crossfades CV In 1 against CV In 2, and
CV Out 2 crossfades CV In 2 against CV In 1.

That gives a practical Workshop Computer interpretation of the Vactrol Mix idea:
click-softened transitions, mirrored movement, and a layer that can behave like audio mixing or CV
crossfading depending on what is patched in.

## Status

Draft implementation. The firmware source is copied from card **03_Turing_Machine** with:

- the internal card number changed to 93
- the original Audio In 1 reset behavior removed
- the original Audio In 2 switch override behavior removed
- a new `Z up` Vactrol Mix layer added alongside the Turing control layer
- startup knob values applied directly to the active layer, with pickup only applied after layer changes

## Web editor

This card now needs its own editor model because the switch no longer selects two Turing presets.
The local `web/` editor handles the Turing settings plus the Vactrol-layer settings: timing,
scale/range, pulse behavior, crossfade law, lane relation, rise/fall timing, and per-lane
minimum/maximum windows. The panel still handles the live lag and crossfade depth gestures in
`Z up`.
