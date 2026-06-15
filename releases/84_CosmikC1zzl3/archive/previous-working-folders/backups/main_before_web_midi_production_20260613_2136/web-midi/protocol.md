# C1ZZL3 Web MIDI SysEx Protocol

This protocol is implemented by the main C1ZZL3 firmware in the repo root.

## Transport

- USB MIDI device mode when connected to a computer/DAW/browser.
- USB MIDI host mode at boot on 2025 hardware when the port is
  downstream-facing and a class-compliant USB MIDI controller is attached.
- Browser/editor sends Web MIDI SysEx to the card.
- Current firmware supports preview and flash-save commands for one of eight
  custom slots, plus a performance-settings command.
- Factory presets `0..8` are never overwritten by SysEx.
- Custom slots are enumerated after the factory presets as card presets `9..16`.
- Custom envelopes with no non-zero amplitude stage are rejected, so preset `0`
  / Off cannot accidentally create a silent custom slot.

## Frame

All bytes inside the SysEx frame must be 7-bit clean.

```text
F0 7D 43 31 5A 33 cc payload... F7
```

| Bytes | Meaning |
| --- | --- |
| `F0` | SysEx start |
| `7D` | non-commercial/manufacturer development ID |
| `43 31 5A 33` | C1ZZL3 card ID, ASCII `C1Z3` |
| `cc` | command byte |
| `ss` | custom slot target, `0..7`, mapped to card presets `9..16` |
| `nn...` | 16-byte ASCII preset name, space-padded |
| `aa...` | eight amplitude stages |
| `dd...` | eight phase-distortion stages |
| `F7` | SysEx end |

Envelope commands:

| Command | Meaning |
| --- | --- |
| `01` | preview RAM-only custom envelope |
| `02` | flash-save custom envelope |

Envelope payload length after the command byte is 97 bytes:

```text
1 custom slot target
16 name bytes
40 amplitude stage bytes
40 phase-distortion stage bytes
```

The firmware expects 103 bytes between `F0` and `F7`:

```text
1 manufacturer byte
4 card ID bytes
1 command byte
97 payload bytes
```

## Stage Encoding

Each envelope lane has exactly eight stages. Each stage is encoded as:

```text
level_lsb7 level_msb7 time_lsb7 time_mid7 time_msb7
```

`level` is a 14-bit little-endian 7-bit packed integer:

```text
level = level_lsb7 | (level_msb7 << 7)
```

Valid firmware range:

```text
0..4095
```

`time` is a 21-bit little-endian 7-bit packed integer:

```text
time = time_lsb7 | (time_mid7 << 7) | (time_msb7 << 14)
```

Firmware treats `time == 0` as `1` sample.

## Performance Settings Encoding

Performance settings use command `03` for RAM-only apply and `04` for
flash-save apply.

```text
F0 7D 43 31 5A 33 03 rr rr nn nn ic oc ff F7
```

Payload length after the command byte is 7 bytes:

```text
2 ring-mod bytes
2 noise bytes
1 MIDI input channel
1 MIDI Turing output channel
1 flags byte
```

Ring mod and noise are 14-bit little-endian 7-bit packed integers, using the
same `0..4095` range as knob values. MIDI channels are encoded as `0..15` for
channels `1..16`. Flags bit `0` enables Turing MIDI note output.

## Firmware Response

There is no MIDI acknowledgement yet.

LED feedback is used during hardware testing:

- Valid C1ZZL3 preview frame: all six LEDs flash together briefly.
- Rejected/ignored frame: even and odd LEDs alternate briefly.

Accepted frames select and trigger the destination custom slot immediately:

```text
slot 0 -> preset 9
slot 1 -> preset 10
slot 2 -> preset 11
slot 3 -> preset 12
slot 4 -> preset 13
slot 5 -> preset 14
slot 6 -> preset 15
slot 7 -> preset 16
```

## Current Limitations

- No SysEx readback.
- Custom slot names are kept by the browser editor, not by the card firmware.
- Custom slots are blank after startup unless they were sent with command `02`.
- Silent custom envelopes are not accepted.
- USB role is selected only at boot/reset. It does not hot-switch between
  browser/DAW device mode and controller host mode while running.
- Web MIDI SysEx editing is only available in device mode.
- MIDI note input currently retriggers the selected envelope as a one-shot. It
  does not implement a separate release stage.
