# C1ZZL3 Web MIDI SysEx Protocol

This protocol is implemented by the production C1ZZL3 firmware.

## Transport

- USB MIDI device mode when connected to a computer/DAW/browser.
- USB MIDI host mode at boot on 2025 hardware when the port is downstream-facing
  and a class-compliant USB MIDI controller is attached.
- Browser/editor sends Web MIDI SysEx to the card in USB MIDI device mode.
- Factory presets `0..8` are never overwritten by SysEx.
- Custom slots are enumerated after the factory presets as card presets `9..16`.
- Custom envelopes with no non-zero amplitude stage are rejected.

## Frame

All bytes inside the SysEx frame are 7-bit clean.

```text
F0 7D 43 31 5A 33 cc payload... F7
```

| Bytes | Meaning |
| --- | --- |
| `F0` | SysEx start |
| `7D` | non-commercial/manufacturer development ID |
| `43 31 5A 33` | C1ZZL3 card ID, ASCII `C1Z3` |
| `cc` | command byte |
| `payload...` | command payload |
| `F7` | SysEx end |

## Commands

| Command | Meaning |
| --- | --- |
| `01` | Load custom envelope into RAM |
| `02` | Save custom envelope to flash |
| `03` | Apply ring/noise/MIDI channel settings |
| `04` | Reserved settings-save command; currently applies settings like `03` |
| `05` | Delete saved custom envelope slot from flash |

## Envelope Payload

Envelope commands `01` and `02` use a 97-byte payload:

```text
1 custom slot target
16 name bytes
40 amplitude stage bytes
40 phase-distortion stage bytes
```

The name bytes are accepted for frame compatibility, but the firmware stores
shape data only. Names remain in browser local storage.

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

Firmware treats `time == 0` as `1` sample and caps very long stages internally.

## Delete Envelope Payload

Delete command `05` uses a 1-byte payload:

```text
ss
```

| Byte | Meaning |
| --- | --- |
| `ss` | custom slot target, `0..7` for Custom 1..8 |

The firmware clears the selected custom slot, removes it from the saved custom
slot mask, and writes the updated custom envelope state to flash. If the deleted
slot is currently selected on the card, the active envelope preset is changed to
`Off`.

## Performance Settings Payload

Settings commands `03` and `04` use a 5-byte payload:

```text
rr rr nn nn ic
```

| Bytes | Meaning |
| --- | --- |
| `rr rr` | ring modulation amount, 14-bit packed, `0..4095` |
| `nn nn` | noise amount, 14-bit packed, `0..4095` |
| `ic` | MIDI input channel encoded as `0..15` for channels `1..16` |

There is no MIDI acknowledgement frame.

## Current Limitations

- No SysEx readback.
- Custom slot names are kept by the browser editor, not by the card firmware.
- Silent custom envelopes are not accepted.
- USB role is selected only at boot/reset.
- Web MIDI SysEx editing is only available in USB MIDI device mode.
- Turing MIDI output is intentionally absent for stability.
- The Turing clock does not continue running in synth mode; CV and pulse outputs
  hold the last Turing values.
