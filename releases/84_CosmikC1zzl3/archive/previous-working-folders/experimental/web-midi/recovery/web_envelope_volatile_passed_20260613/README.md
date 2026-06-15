# Web envelope volatile recovery - 2026-06-13

Hardware-passed recovery point for the Reverb+ 1.5 style MIDI build with:

- USB MIDI device/host note input
- Web MIDI input-channel setting
- Web MIDI ring and noise setting
- neutral ring/noise on reset
- switch-down ring/noise edit lockout until the switch has left down once
- one RAM-only Web MIDI custom envelope slot
- factory presets kept intact

The editor `Send` button loads the single volatile slot and selects it for the next MIDI note or Pulse 2 trigger. Envelope flash/persistence remains disabled.

Turing MIDI output is not included.

Use this folder as the rollback point before adding further Web MIDI envelope features.
