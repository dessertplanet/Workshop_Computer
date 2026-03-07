#!/usr/bin/env python3
"""
Generate a test image for the additive synthesis system.

RGB mapping on the card:
  R (0-255) → frequency  20 Hz – 10 kHz  (exponential)
  G (0-255) → amplitude  0 = silent, 255 = full
  B (0-255) → phase offset

This script creates a 256×256 PNG with distinct colored regions
so that moving the 4×4 viewfinder around produces clearly audible changes.

Layout (4 quadrants, each 128×128):
  ┌────────────┬────────────┐
  │ TOP-LEFT   │ TOP-RIGHT  │
  │ Low tone   │ High tone  │
  │ R=80 ~220Hz│ R=200 ~3kHz│
  │ G=255 loud │ G=255 loud │
  │ B=0        │ B=0        │
  ├────────────┼────────────┤
  │ BOT-LEFT   │ BOT-RIGHT  │
  │ Mid tone   │ Sweep      │
  │ R=140 ~600Hz│ R=gradient │
  │ G=200      │ G=200      │
  │ B=128      │ B=gradient │
  └────────────┴────────────┘

Plus vertical stripes overlaid in the sweep quadrant for variety.
"""

import struct
import zlib
import os


def create_png(width, height, pixels):
    """Create a minimal PNG file from raw RGB pixel data."""

    def chunk(chunk_type, data):
        c = chunk_type + data
        crc = struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
        return struct.pack(">I", len(data)) + c + crc

    header = b"\x89PNG\r\n\x1a\n"
    ihdr = chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))

    # Build raw scanlines with filter byte 0 (None)
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter byte
        row_start = y * width * 3
        raw.extend(pixels[row_start : row_start + width * 3])

    compressed = zlib.compress(bytes(raw), 9)
    idat = chunk(b"IDAT", compressed)
    iend = chunk(b"IEND", b"")

    return header + ihdr + idat + iend


def generate_test_image():
    W, H = 256, 256
    pixels = bytearray(W * H * 3)

    for y in range(H):
        for x in range(W):
            offset = (y * W + x) * 3

            if x < 128 and y < 128:
                # TOP-LEFT: low bass tone (~220 Hz), full volume
                r, g, b = 80, 255, 0

            elif x >= 128 and y < 128:
                # TOP-RIGHT: high tone (~3 kHz), full volume
                r, g, b = 200, 255, 0

            elif x < 128 and y >= 128:
                # BOTTOM-LEFT: mid tone (~600 Hz), slightly quieter
                r, g, b = 140, 200, 128

            else:
                # BOTTOM-RIGHT: frequency sweep (R varies with x),
                # amplitude varies with y, phase varies with x
                r = 60 + ((x - 128) * 160) // 128  # 60..220
                g = 160 + ((y - 128) * 80) // 128   # 160..240
                b = ((x - 128) * 255) // 128         # 0..255

            # Add vertical stripes in bottom-right for tonal variety
            if x >= 128 and y >= 128:
                stripe = ((x - 128) // 16) % 4
                if stripe == 1:
                    r = min(r + 40, 255)  # push frequency up
                elif stripe == 3:
                    g = g // 2  # quieter stripe

            pixels[offset] = r & 0xFF
            pixels[offset + 1] = g & 0xFF
            pixels[offset + 2] = b & 0xFF

    return create_png(W, H, pixels)


if __name__ == "__main__":
    png_data = generate_test_image()
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_synth.png")
    with open(out_path, "wb") as f:
        f.write(png_data)
    print(f"Wrote {len(png_data)} bytes to {out_path}")
    print()
    print("Quadrant guide:")
    print("  Top-left:     Low bass ~220 Hz, full volume")
    print("  Top-right:    High tone ~3 kHz, full volume")
    print("  Bottom-left:  Mid tone ~600 Hz, with phase offset")
    print("  Bottom-right: Frequency/amplitude sweep with stripes")
    print()
    print("Load this image in the web UI and move the viewfinder around.")
    print("Each quadrant should sound distinctly different.")
