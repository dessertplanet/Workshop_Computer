# C1ZZL3 Envelope Static Recovery

This folder is the hardware-passed recovery point for the Reverb+ 1.5 style
MIDI interface build with mode-change knob pickup and static factory envelope
data.

Use this recovery point before adding Web MIDI features if the active
experimental build becomes unstable.

Included:

- `C1ZZL3_reverb15_midi_envelope_static_experimental.uf2`: hardware-passed test
  UF2.
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
- mode-change knob pickup
- no-envelope max X/Y/ring/noise/detune
- envelope plus max X/Y
- envelope plus max X/Y/detune/ring/noise

Not included in this recovery point:

- Web MIDI envelope transfer
- custom envelope flash writes
- settings SysEx
- Turing MIDI output
