# C1ZZL3

Dual phase-distortion synthesiser and probabilistic Turing machine program card for the Music Thing Modular Workshop Computer.

Status: Stable reference is the non-MIDI v0.4 build only; MIDI/Web MIDI builds are experimental and currently unstable  
Language: C++ / Pico SDK  
Framework: ComputerCard  
Creator: Adrian Vos

## Stability Notice

The only firmware currently confirmed as stable is the pre-MIDI v0.4 build:

- `uf2/C1ZZL3v04working.uf2`

That UF2 matches the project state from commit `941986e` on 9 June 2026. The current MIDI/Web MIDI work is retained for development and testing, but should not be treated as stable production firmware yet.
Unstable MIDI UF2s have been moved under archive folders to reduce the chance
of accidental flashing.

The sections below describe the intended C1ZZL3 behaviour and the MIDI-era feature work. If you need the reliable build today, use the non-MIDI v0.4 UF2 above.

## Overview

C1ZZL3 is a CZ-inspired phase-distortion oscillator card with a second mirrored oscillator for detune, ring modulation, and noise modulation. The switch also opens a Turing machine mode, turning the card into a clocked CV and pulse source with a self-playing stepped oscillator voice.

The stable v0.4 build has passed hardware tests for the core oscillator, held-switch performance controls, Turing machine behaviour, oscillator sync, restrained CZ-style grit, envelope presets, manual flash persistence, and 1V/oct pitch input scaling.

Experimental MIDI builds have added a browser Web MIDI editor for custom
envelopes, USB MIDI device mode for computer/DAW control, and USB MIDI host mode
for class-compliant controllers on 2025 Workshop Computer hardware, but these
builds have shown overload and lockup problems during high-modulation tests.

Holding the switch down during startup enters envelope preset select. Release the startup hold, then Main chooses one of nine presets, LEDs show the preset number in binary, a short down press exits for the current session, and a long down hold saves the selected preset to flash.

## Current Modes

### Switch Middle: PD Synth

Main controls oscillator pitch over an octave-based range. Audio/CV in 1 adds pitch at a hardware-tested 1V/oct scale.

X controls phase-distortion amount. Audio/CV in 2 modulates the same parameter. Fully counter-clockwise is closest to a clean sine. Turning X up introduces the selected CZ-style target waveform.

Y morphs across eight CZ-inspired waveform families. CV in 1 modulates the same parameter:

1. Saw
2. Square
3. Needle pulse
4. Double sine
5. Saw-pulse curve
6. Resonance I, saw window
7. Resonance II, triangle window
8. Resonance III, trapezoid window

Audio out 1 carries oscillator 1.

Audio out 2 carries oscillator 2. In normal middle position, oscillator 2 is silent until it has been introduced with the held-switch controls.

CV out 1 and CV out 2 hold the last generated Turing values while the synth voice is playing.

Pulse out 1 carries the main Turing pulse. Pulse out 2 carries an alternate Turing bit pulse. These also keep running outside Turing mode from the last Turing settings.

Main, X, and Y use pickup when returning from Turing mode. The stored synth values are held until each physical knob is swept back through its stored value.

### Hold Switch Down: Performance Edit

Holding the switch down edits the latched second oscillator and modulation amounts. The normal Main, X, and Y synth controls are held at their last middle-position values while editing, and the held controls must move after the switch is pressed before soft pickup can edit a latched value.

Main controls oscillator 2 detune from the centre point. Around 12 o'clock, oscillator 2 is unison and quiet. Turning left detunes down; turning right detunes up. Moving away from centre also raises oscillator 2 level.

X controls ring modulation amount. The setting remains active after the switch is released.

Y controls noise modulation amount. The setting remains active after the switch is released.

Oscillator 2 mirrors oscillator 1's phase-distortion shape and selected waveform family.

Holding the switch down from middle synth mode for 8 seconds saves the current latched detune, ring, and noise settings to flash. Saved settings load automatically on startup. There is no autosave.

Pulse in 2 triggers the selected envelope preset and oscillator sync. Preset 0 is Off and ignores Pulse in 2, so the synth remains free-running. Active envelope presets gate the oscillator output, so the synth is silent between triggers.

### Switch Up: Turing Machine

Main controls mutation/lock behaviour. The centre is most random. The extremes become more locked.

X controls sequence length, from 2 to 16 steps.

Y controls internal clock speed.

Main, X, and Y use pickup when returning from synth mode. The stored Turing mutation, length, and clock speed values are held until each physical knob is swept back through its stored value.

Pulse in 1 acts as an external clock. When an external clock is present, it takes priority over the internal clock.

Flicking the switch down from Turing mode taps the internal clock tempo.

CV out 1 carries the scaled stepped Turing CV.

CV out 2 carries a smoothed version of the scaled Turing CV.

Pulse out 1 outputs the main Turing pulse. Pulse out 2 outputs an alternate Turing bit pulse.

Audio out 1 and audio out 2 carry a self-playing stepped oscillator voice. The last PD synth pitch, phase-distortion, waveform, detune, ring, and noise settings are used as the voice sound, with the Turing pattern added to the pitch.

When an envelope preset is active, the main Turing pulse self-triggers the envelope for the stepped oscillator voice. Preset 0 is Off and leaves the Turing voice free-running. Turing envelope triggers do not reset oscillator phase.

## Inputs And Outputs

### Inputs

Audio/CV in 1: 1V/oct pitch modulation in PD synth mode.

Audio/CV in 2: phase-distortion amount modulation in PD synth mode.

CV in 1: waveform morph modulation in PD synth mode.

CV in 2: positive voltages add ring modulation while the switch is held down; negative voltages add noise/grit while the switch is held down.

Pulse in 1: external Turing clock.

Pulse in 2: triggers the selected envelope preset and oscillator sync when preset 1-8 is active. Preset 0 ignores Pulse in 2.

### Outputs

Audio out 1: oscillator 1 in PD synth mode; self-playing stepped oscillator 1 in Turing mode.

Audio out 2: oscillator 2 in PD synth mode; self-playing stepped oscillator 2 in Turing mode.

CV out 1: scaled stepped Turing CV.

CV out 2: smoothed scaled Turing CV.

Pulse out 1: Turing pulse.

Pulse out 2: alternate Turing bit pulse.

## LED Feedback

In PD synth mode:

- LED 1 shows phase-distortion amount.
- LED 2 shows waveform morph position.
- LED 3 shows oscillator 2 level.
- LED 4 shows ring modulation amount.
- LED 5 shows noise modulation amount.
- LED 6 lights while the switch is held down.

In Turing mode:

- LEDs 1, 2, and 3 show low bits of the Turing pattern.
- LED 4 follows pulse input 1.
- LED 5 shows sequence length (brighter = longer).
- LED 6 flashes on every Turing clock step.

In envelope preset select:

- LEDs 1-6 show the selected preset number as a binary value.

Preset numbers:

0. Off
1. Pluck
2. Double pluck
3. Bounce
4. Bell
5. Brass
6. Strings
7. Reverse swell
8. Evolving digital

Names are conceptual and do not reflect accurate synthesis of named instruments!

## Current Hardware Notes

Confirmed on hardware:

- Oscillator output is audible and clean.
- Main pitch control uses an octave-based range.
- Audio/CV in 1 tracks at 1V/oct using the Workshop Computer 12V-over-4096-count input convention used by Chord Blimey.
- X phase-distortion control and Y waveform morphing are responsive across the usable range.
- All eight waveform positions have distinct character, including the CZ-style resonance-window shapes at positions 6-8.
- Held-switch soft pickup, latched detune/ring/noise controls, and 8-second manual save work as intended.
- Mode-switch pickup keeps synth and Turing controls from jumping when changing switch positions.
- Audio/CV in 2 modulates phase-distortion amount and CV in 1 modulates waveform morph in synth mode.
- Envelope preset select, binary LED display, triggering, and persistence work as intended.
- Turing mode clocks correctly from internal clock, tap tempo, and Pulse in 1.
- Turing CV/pulse outputs, self-playing stepped voice, and envelope self-triggering work as intended.
- High Y settings with ringmod and/or noise pass hardware testing after audio-path CPU relief.

Recently tuned:

- V/Oct is accurate (enough)
- X has a gentler response curve for more usable low-to-mid settings.
- Y waveform morphing now uses a direct waveform target, with CZ-style saw, square, pulse, double sine, saw-pulse curve, and three distinct resonance-window positions.
- Held-switch Y now adds restrained sample-held CZ-style grit by gently jittering phase distortion and oscillator phase instead of crossfading in plain audio noise.
- Turing internal clock range uses a faster curved response for a more useful minimum and middle setting.
- Turing CV outputs are scaled to three-quarter range with a slight upward bias for pitch-friendly modulation depth.
- Turing mode audio outs now carry a self-playing stepped oscillator voice using the last PD synth sound.
- Active envelope presets can be self-triggered by the main Turing pulse without oscillator sync.
- Ring modulation now uses a stronger internal carrier while keeping some dry signal at the maximum setting to avoid full signal loss.
- Held-switch performance controls require movement before soft pickup.
- High-Y waveform rendering skips unused oscillator 2/ring calculations, skips unused waveform families, and slightly softens the exact maximum Y endpoint to avoid lockups with ring/noise.
- Manual flash persistence saves latched performance settings after an 8-second held-switch gesture; autosave is intentionally avoided.
- Brass envelope decay has been lengthened, oscillator sync now uses a very short fade, and preset 0 ignores Pulse in 2 for clean free-running oscillator use.
- Envelope presets are ordered by musical family: short percussive shapes, tonal/instrument-like shapes, then unusual/special shapes.
- Reordering envelope presets changes the meaning of saved preset numbers; re-save the preferred preset after flashing this build.

## Future Work

- Further waveform tuning if hardware feedback suggests it.
- Further envelope preset tuning and possible envelope editing mode.
- Optional flash persistence expansion for future complex envelope data.

## Web MIDI Editor

The browser editor lives in `web-midi/editor/`.

Run it locally with:

```sh
python3 -m http.server 5173 --directory web-midi/editor
```

Then open:

```text
http://localhost:5173
```

The editor can send custom envelope slots over Web MIDI SysEx, flash custom
slots to the card, and set or save ring/noise/MIDI channel settings. Factory
presets remain protected.

## Design Notes

C1ZZL3 is intended as a playable, characterful program card rather than an exact Casio CZ clone. The code uses fixed-point integer arithmetic and lookup tables to stay inside the Workshop Computer's 48 kHz audio interrupt budget.

The current implementation favours hardware-tested musical behaviour over completeness. 
Full envelope shaping would need web interface.
