# C1ZZL3 Card Guide

C1ZZL3 is a phase-distortion synth card for the Music Thing Modular Workshop
Computer. It combines a playable CZ-style oscillator, a second detuned oscillator
for ring/noise textures, a Turing machine mode, USB MIDI, and a Web MIDI editor
for custom envelopes.

## What It Does

- Plays as a phase-distortion synth voice.
- Responds to USB MIDI notes from a DAW or class-compliant controller.
- Lets you design and save custom envelopes from a browser.
- Runs a Turing machine mode with stepped CV, smoothed CV, and two pulse outputs.
- Keeps factory envelope presets safe; Web MIDI custom envelopes use separate
  custom slots.

## Main Synth Mode

Put the switch in the middle.

- Main sets pitch.
- X sets phase distortion.
- Y selects or morphs the waveform.
- `Audio/CV In 1` adds pitch at 1V/oct.
- `CV In 1` adds phase distortion.
- `CV In 2` adds wave control.
- `Pulse In 2` triggers the selected envelope.

## Performance Edit

Hold the switch down.

- Main sets oscillator 2 detune.
- X sets ring modulation.
- Y sets noise/grit.

Ring and noise start neutral after reset. If the card powers up with the switch
already down, ring/noise editing waits until the switch has first left down.

## Turing Mode

Put the switch up.

- Main controls mutation.
- X controls sequence length.
- Y controls clock speed.
- `Pulse In 1` can clock the Turing machine externally.
- `CV Out 1` outputs stepped Turing CV.
- `CV Out 2` outputs smoothed Turing CV.
- `Pulse Out 1` and `Pulse Out 2` output Turing pulses.

When you leave Turing mode, the CV and pulse outputs hold their last values.
The Turing clock does not keep running in synth mode; this is intentional for
stability on the RP2040 card format.

## Web MIDI Editor

The editor is in:

```text
web-midi/editor/
```

Hosted editor:

```text
https://soveda.github.io/CozmikC1zzl3/web-midi/editor/
```

Run it locally instead:

```sh
python3 -m http.server 5173 --directory web-midi/editor
```

Open:

```text
http://localhost:5173
```

Use Chrome or another browser with Web MIDI and SysEx support. Press `MIDI`,
choose the C1ZZL3 output, then use `Load`, `Save`, or `Set`.

## Envelope Presets

Factory presets:

0. Off
1. Pluck
2. Double pluck
3. Bounce
4. Bell
5. Brass
6. Strings
7. Reverse swell
8. Evolving digital

Saved custom envelopes appear after the factory presets during startup envelope
selection. Factory presets are not overwritten.

## LED Feedback

In synth mode:

- LED 1 shows phase-distortion amount.
- LED 2 shows waveform position.
- LED 3 shows oscillator 2 level.
- LED 4 shows ring modulation amount.
- LED 5 shows noise amount.
- LED 6 lights while the switch is held down.

In Turing mode:

- LEDs 1-3 show low bits of the Turing pattern.
- LED 4 follows Pulse In 1.
- LED 5 shows sequence length.
- LED 6 flashes on each Turing clock step.

In startup envelope selection:

- Factory presets use the binary LED display.
- Custom slots light LED 6 and use LEDs 1-3 for the custom slot number.

## Stability Note

This firmware is a hardware-tested production candidate, but it is also close to
the practical processing limit of this RP2040 card format. Turing MIDI output
and background Turing clocking in synth mode are intentionally left out because
testing showed they could cause lockups at maximum settings.
