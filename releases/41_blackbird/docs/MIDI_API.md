# MIDI Support (`bb.midi`)

Blackbird can receive and send USB MIDI and exposes it to your script via the `bb.midi` table.

The API is split into:

- `bb.midi.rx` (receive): callbacks you define to react to incoming MIDI
- `bb.midi.tx` (transmit): functions you call to send MIDI

## USB device mode vs USB host mode

Blackbird supports USB-MIDI in both directions, depending on the USB role it boots into:

- **Device mode**: Blackbird enumerates as a USB device (CDC serial + USB-MIDI). This is the mode you use with a computer + web-druid/druid for live coding and seeing `print()` output.
- **Host mode**: Blackbird acts as a USB host and reads MIDI from a class-compliant USB-MIDI device plugged into it (controller, keyboard, etc.).

Role selection is automatic at boot, based on the Workshop Computer’s USB power orientation (whether it believes it is downstream-facing power / DFP). In host mode, Blackbird waits briefly at startup for a USB-MIDI device to mount. Note you always need to power the Workshop System completely off and back on to switch between device and host mode- resetting computer alone is not enough.

Tip: during the startup “waiting” period, the LEDs animate differently:

- Device mode: “around-the-world” loop
- Host mode: top-to-bottom sweep

### Recommended workflow: develop in device mode, then switch to host mode

This lets you iterate quickly with web-druid while keeping the exact same `bb.midi.rx.*` handlers for your final host-mode setup.

1. **Develop + debug in device mode**
  - Connect Blackbird to your computer and open web-druid.
  - Load a script that defines `bb.midi.rx.note` / `bb.midi.rx.cc` / `bb.midi.rx.bend` and uses `print()` so you can see events arriving.
  - Send MIDI to Blackbird from the computer (DAW, MIDI monitor, virtual keyboard). Your `bb.midi.rx.*` functions should fire and you should see prints in web-druid.

2. **Move to host mode (same script, same callbacks)**
  - Disconnect from the computer.
  - Connect a class-compliant USB-MIDI device to Blackbird (so Blackbird can be the USB host).
  - Reset/reboot Blackbird with the MIDI device already connected so it mounts during the startup window.
  - Your existing `bb.midi.rx.*` functions will still be called on incoming MIDI.

Note: in host mode, you generally won’t have the web-druid serial console available, so prefer debugging by driving outputs/LEDs instead of relying on `print()`.

## Receive: `bb.midi.rx` callbacks

Incoming MIDI is decoded and dispatched into Lua. MIDI channels are presented as **1–16** (not 0–15).

If you do nothing, Blackbird installs default callbacks that forward events to the host as crow-style MIDI messages (via `tell('midi', ...)`). When you override the callbacks below, you take control of what happens.

### `bb.midi.rx.note(type, note, vel, channel)`

Unified note handler.

- `type`: string, `'on'` or `'off'`
- `note`: integer 0–127
- `vel`: integer 0–127
- `channel`: integer 1–16

Notes:

- A MIDI **Note On** with velocity `0` is treated as a **Note Off** (this is common MIDI practice).

Example: MIDI note → 1V/oct pitch on `output[1]` (middle C = 60 → 0V)

```lua
bb.midi.rx.note = function(type, note, vel, ch)
  if type ~= 'on' then return end
  if ch ~= 1 then return end

  output[1].volts = (note - 60) / 12
end
```

### `bb.midi.rx.cc(num, val, channel)`

Control Change.

- `num`: integer 0–127 (CC number)
- `val`: integer 0–127
- `channel`: integer 1–16

Example: CC1 (mod wheel) → scale output level

```lua
bb.midi.rx.cc = function(num, val, ch)
  if ch ~= 1 then return end
  if num ~= 1 then return end

  local x = val / 127
  output[2].volts = x * 5.0
end
```

### `bb.midi.rx.bend(val, channel)`

Pitch bend.

- `val`: number in the range **-1.0 .. 1.0** (centered pitch bend)
- `channel`: integer 1–16

`val` is derived from the MIDI 14-bit bend value (0..16383, center 8192) and normalized.

Example: bend → detune around current pitch

```lua
local base = 0.0
bb.midi.rx.note = function(type, note, vel, ch)
  if type == 'on' then base = (note - 60) / 12 end
end

bb.midi.rx.bend = function(v, ch)
  output[1].volts = base + (v * (2/12)) -- +/- 2 semitones
end
```

## Transmit: `bb.midi.tx`

### `bb.midi.tx.note(note, vel, duration, channel)`

Send a Note On immediately, and optionally schedule a Note Off.

- `note` (required): integer 0–127
- `vel` (optional, default `100`): integer 0–127
- `duration` (optional, default `0.0`): seconds; if `> 0`, a Note Off is scheduled
- `channel` (optional, default `1`): integer 1–16

Notes:

- Scheduled Note Offs use velocity `0`.
- Blackbird has a small internal queue for scheduled Note Offs; if it’s full, the firmware sends the Note Off immediately to avoid stuck notes.
- If you need an immediate Note Off and your synth supports it, you can send a “Note On with velocity 0” using `bb.midi.tx.note(note, 0, 0, channel)`.

Example: play a short note every beat

```lua
clock.run(function()
  while true do
    clock.sync(1)
    bb.midi.tx.note(60, 100, 0.1, 1)
  end
end)
```

## MIDI Clock integration (optional)

Blackbird also listens for MIDI realtime messages:

- Clock tick (`0xF8`)
- Start (`0xFA`)
- Continue (`0xFB`)
- Stop (`0xFC`)

When the system clock source is set to MIDI, these messages drive Blackbird’s internal `clock` engine (tempo and transport). In Lua, the clock-source helpers are exposed as global functions:

- `clock_get_source()` returns 1-based: `1=internal`, `2=MIDI`, `3=LINK`, `4=CROW`
- `clock_set_source(source)` sets the source (pass `0` for “auto”)

Example: force MIDI clock

```lua
clock_set_source(2)
```

## Tips

- If you want to both **handle** MIDI locally *and* **forward** it to a host (druid/norns/Max), call `tell('midi', ...)` in your callback (the same way the defaults do).
- Channel numbers given to Lua are always 1–16.
