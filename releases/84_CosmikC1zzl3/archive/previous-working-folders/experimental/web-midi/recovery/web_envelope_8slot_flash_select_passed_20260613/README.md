# Web envelope 8-slot flash/select recovery - 2026-06-13

Hardware-passed recovery point for the Reverb+ 1.5 style MIDI build with:

- USB MIDI device/host note input
- Web MIDI input-channel setting
- Web MIDI ring and noise setting
- neutral ring/noise on reset
- switch-down ring/noise edit lockout until the switch has left down once
- eight Web MIDI custom envelope slots
- explicit custom-envelope Flash persistence in a dedicated flash sector
- startup envelope selection including loaded custom slots after factory presets
- factory presets kept intact

The editor `Send` button loads the selected custom slot into RAM. The editor `Flash` button writes the selected custom slot to the dedicated custom-envelope flash sector. Ring/noise are not written by this envelope Flash path.

Confirmed on hardware, including torture testing, and stable as of 2026-06-13.

Turing MIDI output is not included.

Use this folder as the rollback point before adding further Web MIDI envelope features or promoting this work.
