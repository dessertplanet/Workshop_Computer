# 26_Dyski Web Serial Image Uploader

This is a minimal local website that:

1. Connects to your card over USB serial (Web Serial API)
2. Lets you upload an image
3. Shows the image preview
4. Sends image bytes to the card

## Run

From this folder:

```bash
cd /workspaces/computercard-dev-env/26_Dyski/web
python3 -m http.server 8080
```

Open in a Chromium-based browser (Chrome/Edge):

- `http://localhost:8080`

## Serial protocol sent to the card

The page sends one packet per image:

- ASCII header: `IMG\n`
- 4-byte little-endian uint32 image size
- raw image bytes
- ASCII footer: `\nEND\n`

So firmware can parse:

1. Wait for `IMG\n`
2. Read 4 bytes for payload size
3. Read exactly payload size bytes
4. Read `\nEND\n`

## Notes

- Browser must support `navigator.serial`.
- User gesture is required to connect (`Connect` button).
- If your firmware expects a different framing/protocol, update `app.js` in this folder.
