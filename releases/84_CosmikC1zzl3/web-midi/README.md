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
- `Set` to apply ring, noise, and MIDI input channel settings
- `SysEx` to copy the current envelope frame for inspection

Factory presets are protected. Custom envelope names are stored by the browser;
the card stores the envelope shapes.

## Limits

The editor supports eight custom slots, matching the firmware. Turing MIDI
output and synth-mode Turing clock persistence are intentionally absent from the
production firmware because hardware testing showed those additions could push
the RP2040 card beyond its stable processing limit at maximum settings.
