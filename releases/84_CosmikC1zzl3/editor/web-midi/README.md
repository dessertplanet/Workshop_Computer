# C1ZZL3 Web MIDI Editor

This folder contains the browser editor for the production C1ZZL3 Web MIDI
firmware.

## Hosted Editor

Use the hosted GitHub Pages editor:

```text
https://soveda.github.io/CozmikC1zzl3/web-midi/editor/
```

## Run Locally

From the repository root:

```sh
python3 -m http.server 5173 --directory web-midi/editor
```

Open:

```text
http://localhost:5173
```

If that port is busy, use another one:

```sh
python3 -m http.server 5174 --directory web-midi/editor
```

Use Chrome or another browser with Web MIDI and SysEx support. Press `MIDI`,
select the C1ZZL3 MIDI output, then use:

- `Load` to send the selected envelope to a custom RAM slot
- `Save` to write the selected custom slot to card flash
- `Delete` to clear the selected custom slot from card flash
- `Read` to load the current card settings into the editor
- `Set` to save ring, noise, MIDI input channel, Turing CV range, and Turing
  MIDI output settings to the card
- `SysEx` to copy the current envelope frame for inspection

Factory presets are protected. Custom envelope names are stored by the browser;
the card stores the envelope shapes.

Turing CV range defaults to 2 octaves. It can also be changed live with MIDI
CC24 on the selected MIDI input channel. Turing MIDI output on/off and output
channel are set from this editor.

CC20 controls detune, CC21 controls ring modulation, and CC22 controls noise
amount. These are live controls; save from the editor or the card if you want
them to survive reset.

CC1 controls phase distortion and CC23 controls waveform. These update the same
control values as the physical knobs, so the knobs use the normal pickup
behaviour afterward.

## Limits

The editor supports eight custom slots, matching the firmware. Custom envelope
names are kept in browser storage; the card stores the envelope shapes and
performance settings.
