# Web envelope 8-slot flash/select UI LED recovery - 2026-06-13

Hardware-passed recovery point for the Reverb+ 1.5 style MIDI build with:

- USB MIDI device/host note input
- Web MIDI input-channel setting
- Web MIDI ring and noise setting
- neutral ring/noise on reset
- switch-down ring/noise edit lockout until the switch has left down once
- eight Web MIDI custom envelope slots
- explicit custom-envelope Save/Flash persistence in a dedicated flash sector
- startup envelope selection including loaded custom slots after factory presets
- clearer Web UI labels: Custom 1-8, Load, Save
- custom-bank LED feedback during startup envelope selection
- factory presets kept intact

Confirmed on hardware as passing before the CV input rerouting work.

Turing MIDI output is not included and should stay out.

Use this folder as the rollback point before CV input routing changes.
