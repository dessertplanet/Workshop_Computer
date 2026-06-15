# Web envelope 8-slot volatile recovery - 2026-06-13

Hardware-passed recovery point for the Reverb+ 1.5 style MIDI build with:

- USB MIDI device/host note input
- Web MIDI input-channel setting
- Web MIDI ring and noise setting
- neutral ring/noise on reset
- switch-down ring/noise edit lockout until the switch has left down once
- eight RAM-only Web MIDI custom envelope slots
- factory presets kept intact

The editor `Send` button loads the selected volatile slot and selects it for the next MIDI note or Pulse 2 trigger. Envelope flash/persistence remains disabled, and all custom slots are lost on reset.

Turing MIDI output is not included.

Use this folder as the rollback point before adding persistence or other Web MIDI envelope features.
