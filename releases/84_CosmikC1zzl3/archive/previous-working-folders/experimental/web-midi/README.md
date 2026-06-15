# C1ZZL3 Web MIDI Experimental Firmware

This folder contains the isolated Web MIDI firmware and editor work for C1ZZL3.
It exists so the production `C1ZZL3.cpp` can remain untouched until a tested
experimental build is deliberately promoted.

## Production Candidate

Current production-candidate recovery point:

```text
experimental/web-midi/recovery/web_envelope_8slot_flash_select_ui_led_cvin_turing_clamp_production_candidate_20260613/
```

Current production-candidate UF2:

```text
experimental/web-midi/C1ZZL3_reverb15_midi_web_envelope_8slot_flash_select_ui_led_cvin_turing_clamp_reverted_experimental.uf2
```

Checksum:

```text
36789f89c9979157b09a1055b8e930c32812cb5d11c856e6a038e607b8254dd6
```

This build passed the production-candidate hardware torture tests:

- maximum physical controls
- maximum Web MIDI ring/noise settings
- factory and custom envelope stress
- repeated Pulse 2 and MIDI note triggering
- switch/mode changes
- USB MIDI device mode from DAW/browser
- USB MIDI host mode from class-compliant controllers
- MIDI channel and custom envelope persistence

Use the production-candidate recovery folder as the source for final polish and
promotion. It contains the matching UF2, firmware source, Web editor files, USB
MIDI support files, README, and checksum.

## Important Stability Note

This build appears to be at the practical limit of what is stable on this card
format with the RP2040 while keeping the current oscillator, envelopes, Web MIDI
custom slots, USB MIDI device/host support, ring/noise, detune, Turing mode, CV
outputs, and LED feedback.

The failed experiments are important evidence:

- Keeping the Turing clock running in synth mode pushed the card over the edge
  at maximum settings.
- Reducing noise depth did not make that clock-persistence branch reliable.
- Limiting Web MIDI ring/noise settings did not make that branch reliable.
- The stable path is to leave Turing CV/pulse outputs held while in synth mode,
  rather than continuing the Turing clock in the background.

For this hardware target, avoid adding background work to the audio callback
unless something else is removed or simplified. In particular, do not add Turing
MIDI output or synth-mode Turing clock persistence to this production candidate.

## Current Behaviour

### USB And MIDI

- USB role is chosen at boot/reset.
- Connected to a computer, the card appears as a USB MIDI device for DAW and
  browser Web MIDI use.
- Connected downstream to a class-compliant USB MIDI controller, the card can
  run in USB MIDI host mode on 2025 hardware.
- MIDI note-on sets pitch and retriggers the selected envelope.
- MIDI note-off releases the note state.
- MIDI input channel is set from the Web editor and persists after reset.
- Turing MIDI output is deliberately not implemented.

### Web Editor

The Web editor can:

- edit amplitude and phase-distortion envelope stages
- drag envelope nodes vertically for level and sideways for stage time
- add and remove up to eight custom presets
- send a custom envelope to RAM with `Load`
- persist a custom envelope with `Save`
- set ring, noise, and MIDI input channel
- audition envelopes in the browser
- copy the SysEx frame or C++ envelope data for inspection

Factory presets are never overwritten. The card stores custom envelope shapes
only; custom names stay in browser local storage.

### Envelope Slots

- Factory presets remain available at startup.
- Eight custom envelope slots can be loaded from the Web editor.
- Startup envelope selection includes loaded custom slots after the factory
  presets.
- LED 5 marks the custom bank during startup selection.
- LEDs 0-2 show the custom slot number.
- Preset 0 / Off and fully silent amplitude envelopes are not sent or accepted.
- Very short envelopes are blocked by the editor.

### Controls

Synth mode:

- Main controls pitch.
- X controls phase distortion.
- Y controls wave shape.
- `CVIn1` adds phase distortion amount.
- `CVIn2` adds wave control.
- `AudioIn2` is unused.
- Pulse 2 triggers the selected envelope.

Switch down:

- X edits ring modulation.
- Y edits noise.
- Ring/noise reset neutral on startup.
- If the card starts with the switch down, ring/noise editing is locked until
  the switch has first left down.

Turing mode:

- Main controls mutation.
- X controls sequence length.
- Y controls clock speed.
- `CVIn1` adds clamped phase distortion amount.
- `CVIn2` adds clamped wave control.
- CV and pulse outputs are generated while in Turing mode.
- Turning X briefly displays sequence length as binary on the LEDs.

When leaving Turing mode, the last Turing CV/pulse values are held. The Turing
clock does not continue running in synth mode because that failed hardware
testing at maximum settings.

## Running The Editor Locally

From the repository root:

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
then choose the C1ZZL3 output explicitly before pressing `Load`, `Save`, or
`Set`.

## Hardware Test Checklist

Use this checklist before promoting the build.

1. Flash the production-candidate UF2.
2. Confirm normal synth audio with factory presets.
3. Set all physical controls to maximum and trigger from Pulse 2.
4. Use Web MIDI `Set` with ring/noise at maximum.
5. Play repeated MIDI notes from a DAW on the selected channel.
6. Confirm wrong-channel MIDI notes are ignored.
7. Test USB host mode with a class-compliant controller.
8. Load a custom envelope into RAM and trigger it.
9. Save a custom envelope, power cycle, and select it at startup.
10. Change MIDI channel, power cycle, and confirm it persists.
11. Switch repeatedly between synth, switch-down edit, and Turing modes.
12. In Turing mode, test CV/pulse outputs, X length display, and CV input clamps.
13. Return to synth mode and confirm Turing CV/pulse outputs hold steady.
14. Repeat maximum-control tests with factory and custom envelopes.

If any overload occurs, reset the card before judging the next test.

## Failed Branches To Avoid

These builds are retained only as historical artifacts and should not be used
as baselines:

```text
experimental/web-midi/C1ZZL3_reverb15_midi_web_envelope_8slot_flash_select_ui_led_cvin_turing_clamp_clock_persist_experimental.uf2
experimental/web-midi/C1ZZL3_reverb15_midi_web_envelope_8slot_flash_select_ui_led_cvin_turing_clamp_clock_persist_noise_half_experimental.uf2
experimental/web-midi/C1ZZL3_reverb15_midi_web_envelope_8slot_flash_select_ui_led_cvin_turing_clamp_clock_persist_noise_half_midi_effect_clamp_experimental.uf2
experimental/web-midi/C1ZZL3_reverb15_midi_web_envelope_8slot_flash_select_ui_led_cvin_turing_clamp_clock_persist_noise_half_midi_effect_half_clamp_experimental.uf2
```

The common problem was the added background load from Turing clock persistence
in synth mode. The card could work in normal use but lock up at the combined
maximum settings needed for a production candidate.

## Recovery Folders

Important recovery points:

```text
experimental/web-midi/recovery/web_envelope_8slot_flash_select_ui_led_cvin_turing_clamp_production_candidate_20260613/
experimental/web-midi/recovery/web_envelope_8slot_flash_select_ui_led_cvin_turing_clamp_passed_20260613/
experimental/web-midi/recovery/web_envelope_8slot_flash_select_ui_led_cvin_passed_20260613/
experimental/web-midi/recovery/web_envelope_8slot_flash_select_ui_led_passed_20260613/
experimental/web-midi/recovery/web_envelope_8slot_flash_select_passed_20260613/
```

The production-candidate folder is the one to use for promotion. Earlier
recovery folders remain useful if a regression needs to be narrowed down.

## Building

Build the experimental firmware from the repository root:

```sh
cmake -S experimental/web-midi/firmware -B experimental/web-midi/firmware/build -DPICO_NO_PICOTOOL=1
cmake --build experimental/web-midi/firmware/build
```

The production-candidate build reported:

```text
FLASH: 62 KB
RAM: 71788 B
```

## Promotion Notes

Before folding this into production:

- copy the production-candidate firmware source into the main firmware location
- move the Web editor into the main documented Web MIDI location
- keep the failed clock-persistence builds archived
- keep the non-MIDI v0.4 UF2 as a known historical fallback
- update the main README and metadata to state that Web MIDI is supported but
  Turing MIDI output and synth-mode Turing clock persistence are intentionally
  absent for stability
