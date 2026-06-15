# Web envelope 8-slot flash/select UI LED CVIn recovery - 2026-06-13

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
- synth-mode CVIn1 routed to phase distortion amount
- synth-mode CVIn2 routed to wave control
- AudioIn2 unused
- ring/noise controlled by switch-down controls and Web MIDI, not CV inputs

Confirmed on hardware, including torture testing, and stable as of 2026-06-13.

Turing MIDI output is not included and should stay out.

Use this folder as the rollback point before any further CV or Turing-mode changes.
