# C1ZZL3

Dual phase-distortion synthesiser, Web MIDI custom-envelope editor, USB MIDI
instrument, and probabilistic Turing machine program card for the Music Thing
Modular Workshop Computer.

Status: production build promoted from the hardware-tested Web MIDI build  
Language: C++ / Pico SDK  
Framework: ComputerCard  
Creator: Adrian Vos

For a simpler user-facing description of the card, see:

```text
CARD_README.md
```

## Production Build

Current promoted UF2:

```text
uf2/C1ZZL3.uf2
```

Checksum:

```text
22aaf7673e8169bb6f34d0fc2450f0ecccf82857278e0c428083162e854415dd
```

The exact tested production UF2 with custom-slot deletion is also retained:

```text
uf2/C1ZZL3_web_midi_delete_slots_production_20260615.uf2
```

Rollback build with Turing tap tempo still present:

```text
uf2/archive/previous-versions-20260615/C1ZZL3_with_tap_tempo_rollback_20260615.uf2
```

Matching source for that rollback is archived in:

```text
archive/previous-working-folders/backups/rollback_with_tap_tempo_20260615/
```

The previous main build was archived before promotion:

```text
archive/previous-working-folders/backups/main_before_web_midi_production_20260613_2136/
```

The non-MIDI v0.4 fallback remains available:

```text
uf2/archive/previous-versions-20260615/C1ZZL3v04working.uf2
```

## Stability Boundary

This production build appears to be at the practical limit of what is stable
on this RP2040 card format while keeping the current oscillator, envelopes, Web
MIDI custom slots, USB MIDI device/host support, ring/noise, detune, Turing
mode, CV outputs, and LED feedback.

The failed experiments are part of the design decision:

- Keeping the Turing clock running in synth mode caused lockups at maximum
  settings.
- Reducing noise depth did not make that clock-persistence branch reliable.
- Limiting Web MIDI ring/noise settings did not make that branch reliable.
- Turing MIDI output has not been added because it would increase complexity
  and processing load for little benefit on this card.

The stable production behaviour is therefore:

- Turing CV and pulse outputs are generated while in Turing mode.
- When switching back to synth mode, the last Turing CV and pulse values are
  held.
- The Turing clock does not continue running in synth mode.
- Turing tap tempo has been removed; Y is the only internal clock-speed
  control.
- Turing MIDI output is intentionally absent.

Avoid adding background work to the audio callback unless another feature is
removed or simplified.

## Modes

### Switch Middle: PD Synth

- Main controls pitch.
- Audio/CV in 1 adds pitch at the hardware-tested 1V/oct scale.
- X controls phase-distortion amount.
- Y morphs across the waveform families.
- `CVIn1` adds phase-distortion amount.
- `CVIn2` adds wave control.
- `AudioIn2` is unused.
- Pulse in 2 triggers the selected envelope and oscillator sync.
- CV out 1/2 and Pulse out 1/2 hold the last Turing values.

### Hold Switch Down: Performance Edit

- Main controls oscillator 2 detune.
- X edits ring modulation.
- Y edits noise/grit modulation.
- Ring and noise start neutral after reset.
- If the card starts with the switch down, ring/noise editing is locked until
  the switch has first left down.
- Holding switch down from middle for the save gesture keeps the original manual
  save behaviour for supported performance settings.

### Switch Up: Turing Machine

- Main controls mutation/lock behaviour.
- X controls sequence length from 2 to 16 steps.
- Y controls internal clock speed.
- Pulse in 1 acts as an external clock.
- CV out 1 carries scaled stepped Turing CV.
- CV out 2 carries smoothed Turing CV.
- Pulse out 1 carries the main Turing pulse.
- Pulse out 2 carries an alternate Turing bit pulse.
- Audio out 1/2 carry the self-playing stepped oscillator voice.
- `CVIn1` adds clamped phase-distortion amount.
- `CVIn2` adds clamped wave control.

Main, X, and Y use pickup when changing between synth and Turing modes, so
stored values do not jump until each physical knob is swept through its held
value.

## Web MIDI And USB MIDI

The card uses boot-time USB role selection on 2025 Workshop Computer hardware:

- connected to a computer, it appears as a USB MIDI device for DAW/browser use
- connected downstream to a class-compliant USB MIDI controller, it can run in
  USB MIDI host mode

The Web MIDI editor can:

- edit amplitude and phase-distortion envelopes
- drag envelope nodes vertically for level and sideways for stage time
- add and remove up to eight custom presets
- load a custom envelope into RAM
- save a custom envelope to card flash
- delete a saved custom envelope slot from card flash
- set ring, noise, and MIDI input channel
- audition envelopes in the browser
- copy SysEx or C++ envelope data for inspection

Factory presets are never overwritten. The card stores custom envelope shapes;
custom names are kept in browser local storage.

## Running The Web Editor

Hosted editor:

```text
https://soveda.github.io/CozmikC1zzl3/web-midi/editor/
```

Use the hosted editor for normal Web MIDI editing. To run the same editor
locally from the repository root:

```sh
python3 -m http.server 5173 --directory web-midi/editor
```

Open:

```text
http://localhost:5173
```

If port `5173` is already in use, choose another port:

```sh
python3 -m http.server 5174 --directory web-midi/editor
```

Use Chrome or another browser with Web MIDI and SysEx support. Press `MIDI`,
then choose the C1ZZL3 output explicitly before pressing `Load`, `Save`,
`Delete`, or `Set`.

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

Eight custom slots can be loaded from the Web editor and selected after the
factory presets during startup envelope selection. LED 6 marks the custom bank,
and LEDs 1-3 show the custom slot number.

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
- LED 4 follows Pulse in 1.
- LED 5 shows sequence length.
- LED 6 flashes on each Turing clock step.
- Turning X briefly displays sequence length as binary on the LEDs.

In startup envelope selection:

- Factory presets use the existing binary LED display.
- Custom slots light LED 6 and use LEDs 1-3 for the custom slot number.

## Hardware Test Coverage

This production build passed:

- maximum physical controls
- maximum Web MIDI ring/noise settings
- factory and custom envelope stress
- repeated Pulse 2 and MIDI note triggering
- switch/mode changes
- USB MIDI device mode from DAW/browser
- USB MIDI host mode from class-compliant controllers
- custom envelope save and startup selection

If an overload occurs while testing future changes, reset the card before
judging the next test.

## Building

```sh
cmake -S . -B build
cmake --build build
```

The promoted production build reported:

```text
FLASH: 62 KB
RAM: 71788 B
```

## Development Notes

The experimental history remains in:

```text
experimental/web-midi/
```

The failed synth-mode Turing clock persistence builds are archived there and
should not be used as future baselines.
