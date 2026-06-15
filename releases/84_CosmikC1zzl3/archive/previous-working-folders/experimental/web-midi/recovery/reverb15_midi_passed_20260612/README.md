# C1ZZL3 Reverb15 MIDI Recovery

This folder is the hardware-passed recovery point for the narrow Reverb+ 1.5
style MIDI interface build.

Use this recovery point before adding new MIDI/Web UI features if the active
experimental build becomes unstable.

Included:

- `C1ZZL3_reverb15_midi_experimental.uf2`: hardware-passed test UF2.
- `C1ZZL3.cpp`: matching firmware source.
- `CMakeLists.txt`: matching build target.
- TinyUSB MIDI host/device support files used by this build.
- `SHA256SUMS.txt`: checksum for the recovery UF2.

Passed scope:

- normal synth behaviour
- USB MIDI device mode from DAW/computer
- USB MIDI host mode from class-compliant controller
- MIDI note triggering
- pitch bend

Not included in this recovery point:

- Web MIDI envelope transfer
- custom envelope flash writes
- settings SysEx
- Turing MIDI output
