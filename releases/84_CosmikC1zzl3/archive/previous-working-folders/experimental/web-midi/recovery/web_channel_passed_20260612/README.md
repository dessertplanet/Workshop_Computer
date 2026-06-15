# C1ZZL3 Web Channel Recovery

This folder is the hardware-passed recovery point for the Reverb+ 1.5 style
MIDI interface build with mode-change knob pickup, static factory envelope data,
and Web MIDI input-channel setting.

The previous envelope-static recovery point is intentionally still kept at:

```text
experimental/web-midi/recovery/envelope_static_passed_20260612/
```

Use this recovery point before adding further Web MIDI features if the active
experimental build becomes unstable.

Included:

- `C1ZZL3_reverb15_midi_web_channel_experimental.uf2`: hardware-passed test UF2.
- `C1ZZL3.cpp`: matching firmware source.
- `CMakeLists.txt`: matching build target.
- TinyUSB MIDI host/device support files used by this build.
- `editor_index.html`, `editor_app.js`, `editor_styles.css`: matching
  channel-only Web MIDI editor files.
- `SHA256SUMS.txt`: checksum for the recovery UF2.

Passed scope:

- normal synth behaviour
- USB MIDI device mode from DAW/computer
- USB MIDI host mode from class-compliant controller
- MIDI note triggering
- pitch bend
- mode-change knob pickup
- no-envelope max X/Y/ring/noise/detune
- envelope plus max X/Y/detune/ring/noise
- Web MIDI input channel change
- channel setting persists after reset in hardware testing

Not included in this recovery point:

- Web MIDI envelope transfer
- custom envelope flash writes
- ring/noise Web MIDI settings
- Turing MIDI output
