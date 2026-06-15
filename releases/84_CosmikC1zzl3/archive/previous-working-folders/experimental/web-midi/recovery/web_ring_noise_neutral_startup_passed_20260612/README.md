# Web ring/noise neutral-startup recovery - 2026-06-12

Hardware-passed recovery point for the Reverb+ 1.5 style MIDI build with:

- USB MIDI device/host note input
- Web MIDI input-channel setting
- Web MIDI ring and noise setting
- static factory envelope data
- mode-change knob pickup
- neutral ring/noise on reset
- switch-down ring/noise edit lockout until the switch has left down once

Ring and noise are intentionally not restored from flash on reset. Web MIDI `Set` applies ring/noise for the active session only.

Turing MIDI output is not included. Web MIDI custom-envelope transfer and custom-envelope flash are not included.

Use this folder as the rollback point before adding further Web MIDI features.
