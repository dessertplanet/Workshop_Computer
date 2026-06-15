# C1ZZL3 Web MIDI Editor

This folder contains the browser editor for C1ZZL3 custom envelopes and MIDI
settings.

Run locally:

```sh
python3 -m http.server 5173 --directory web-midi/editor
```

Then open:

```text
http://localhost:5173
```

Use the `MIDI` button in the page, select the C1ZZL3 MIDI output, then use
`Send`, `Flash`, `Set`, or `Save Set`.

Factory presets are protected. Custom envelope names are stored by the browser;
the card stores custom envelope shapes and MIDI/settings data.
