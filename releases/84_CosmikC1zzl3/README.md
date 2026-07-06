# C1ZZL3

Stable production firmware for the Cosmik C1ZZL3 Music Thing Modular Workshop
Computer card.

C1ZZL3 is a dual phase-distortion synthesiser with custom Web MIDI envelopes,
USB MIDI device/host support, optional Turing MIDI output, and a Turing machine
mode with CV and pulse outputs.

For the user-facing card guide, see:

```text
CARD_README.md
```

## Stable Build

Current stable UF2:

```text
uf2/C1ZZL3.uf2
```

Checksum:

```text
12c356ad75d6d25fccab5060dbb057f1e3ed86169c2429a798c66f835825ed61
```

This is hardware-tested production release 1.1, promoted on 2026-07-06.

The previous production release 1.0 is archived at:

```text
uf2/archive/production-1.0-20260706/C1ZZL3_1.0_production.uf2
```

Release 1.1 works with Envelope Lab and CZ Import and includes Web MIDI PD,
detune, eight waveform families, and card-to-editor envelope readback.

Previous UF2s are archived in:

```text
uf2/archive/
```

Source snapshots and experiment notes are archived in:

```text
archive/
```

## Current Stable Feature Set

- Phase-distortion synth voice.
- Factory envelopes plus eight protected custom envelope slots.
- Web MIDI envelope editor.
- USB MIDI device mode for DAW/browser use.
- USB MIDI host mode for class-compliant controllers.
- MIDI notes with envelope triggering.
- MIDI CC control with knob pickup handoff.
- Turing machine audio, CV, pulse, and optional MIDI note output.
- Turing CV and pulse outputs continue running in synth mode.
- Settings readback from the card into the Web MIDI editor.
- Ring, noise, MIDI channel, Turing range, and Turing MIDI settings persist.

## Controls

Switch middle: synth mode.

- Main: pitch
- X: phase distortion
- Y: waveform
- `CV In 1`: phase-distortion modulation
- `CV In 2`: waveform modulation
- `Pulse In 2`: envelope trigger and oscillator sync

Switch down from middle: performance edit and save.

- Main: oscillator 2 detune
- X: ring modulation
- Y: noise/grit

Switch up: Turing mode.

- Main: mutation/lock
- X: sequence length, 2 to 16 steps
- Y: internal clock speed
- `Pulse In 1`: external clock
- `CV Out 1`: stepped Turing CV
- `CV Out 2`: smoothed Turing CV
- `Pulse Out 1/2`: Turing pulses

## MIDI

MIDI CC controls on the selected input channel:

- `CC1`: phase distortion
- `CC20`: oscillator 2 detune
- `CC21`: ring modulation
- `CC22`: noise amount
- `CC23`: waveform
- `CC24`: Turing CV octave range, from 1 to 8 octaves

The physical knobs and MIDI CC controls share the same control values. After a
CC change, the related knob must be swept through the current value before it
takes over again.

## Web MIDI Editor

Hosted editor:

```text
https://soveda.github.io/CozmikC1zzl3/web-midi/editor/
```

Local editor:

```sh
python3 -m http.server 5173 --directory web-midi/editor
```

Open:

```text
http://localhost:5173
```

Use Chrome or another browser with Web MIDI and SysEx support.

## C1ZZL3 Import Lab

Hosted import lab:

```text
https://soveda.github.io/CozmikC1zzl3/experiments/cz-import/
```

Local import lab:

```sh
python3 -m http.server 5174 --directory experiments/cz-import
```

Open:

```text
http://localhost:5174
```

Use this page to decode Casio CZ `.syx` patches into C1ZZL3 drafts, then open
the result in Envelope Lab for final editing and sending.

## How To Use The Editor

1. Pick a preset on the left, or add a custom one.
2. Choose `Amplitude` or `Phase Distortion` to focus on one lane at a time.
3. Drag points on the graph to change both level and timing.
4. Watch the point numbers. Matching numbers mean the stages are stacked at the same spot.
5. Use the tables below the graph for exact values when you want precise edits.
6. Use the action buttons on the right when you want to send, save, read, or export.

Button quick reference:

- `Load RAM`: send the envelope to the card until reset.
- `Load Envelope + Settings`: temporarily load the selected envelope and send
  all current settings in one action.
- `Save Envelope`: store the selected custom envelope in flash.
- `Delete Envelope Slot`: clear the selected custom slot from card flash.
- `Read Envelopes from Card`: load occupied card slots into the editor without
  overwriting changed local drafts.
- `Read Settings from Card`: pull the current performance settings into the editor.
- `Send Settings`: send the current performance settings to the card.
- `Export JSON`: download all editor presets.
- `Reset Preset`: restore the selected preset to its factory value.

`Load RAM`, `Load Envelope + Settings`, and `Send Settings` are temporary.
Use `Save Envelope` to retain an envelope in flash. To make the current
performance settings the startup baseline, move the hardware switch from
middle to down and hold it until the card confirms the save.

The card can save up to eight custom envelopes. The browser can retain additional
local drafts. Factory presets are not overwritten.
Custom presets are labelled `Local only`, `Saved - slot N`, or `Changed - slot N`.
Envelope readback confirms which custom slots are occupied and verifies saves and
deletions when supported by the experimental firmware.

## Build

```sh
cmake -S . -B build
cmake --build build
```

The built UF2 will be:

```text
build/C1ZZL3.uf2
```

The production source build currently reports:

```text
FLASH: 138160 B
RAM: 146500 B
```

## Stability Notes

This build is close to the practical processing limit of this RP2040 card
format, so future changes should be tested carefully at maximum settings.

The stable version includes the lookup-table oscillator optimisation, 192 MHz
clock, Turing MIDI output, settings readback, and full CC/knob pickup handoff.
Tap tempo remains removed; Y is the Turing internal clock control.

Possible future optimisation notes are kept in:

```text
FUTURE_NOTES.md
```

## Repository Layout

- `C1ZZL3.cpp`: main firmware
- `C1ZZL3_LUT.cpp` / `C1ZZL3_LUT.h`: phase-distortion lookup tables
- `FUTURE_NOTES.md`: deferred optimisation and cleanup notes
- `web-midi/editor/`: browser editor
- `experimental-firmware/pd-detune-wave/`: current stable experimental firmware
- `uf2/C1ZZL3.uf2`: current stable firmware
- `uf2/archive/`: older UF2s and rollbacks
- `archive/`: source snapshots and experiment notes
- `CARD_README.md`: user-facing card guide
- `info.yaml`: Workshop Computer site metadata
