# Glitch

A clock-synced beat-repeater and audio degradation effect for the Music Thing Modular Workshop Computer.

Glitch records incoming audio into a 0.5s circular buffer and, on each clock pulse, replays a ratcheted sub-slice of the last beat — with optional reversal and lo-fi bitcrushing/decimation.

Inspired by the [Phazerville/O&C Glitch applet](https://github.com/Chysn/O_C-HemisphereSuite) by Andy Jenkinson.

## Installation

1. Download `glitch.uf2` from the [latest release](https://github.com/uglifruit/Workshop_Computer/releases/tag/glitch-v1.0.0)
2. Hold BOOTSEL on your Pico while plugging in USB — it mounts as RPI-RP2
3. Drag and drop `glitch.uf2` onto the drive — it reboots automatically

## Source

https://github.com/uglifruit/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard/examples/glitch

## Inputs

| Jack | Function |
|------|----------|
| Audio In 1 | Main audio input |
| Pulse In 1 | Clock — rising edge sets beat length (max 0.5s) |
| Pulse In 2 | External gate (Switch MID mode only) |
| CV In 1 | Freeze — above ~0V stops recording, loops frozen content |

## Outputs

| Jack | Function |
|------|----------|
| Audio Out 1 | Main output |
| Audio Out 2 | Identical to Out 1 |

## Controls

**Main Knob — Ratchet Zone + Probability**
Five zones select ratchet division ÷1 / ÷2 / ÷3 / ÷4 / ÷6. Position within each zone sets a hidden reverse probability threshold. LED 5 brightness shows the current threshold.

**Knob X — Degradation Amount**
Bitcrushing and decimation applied together. Fully CCW = clean, fully CW = maximum lo-fi.

**Knob Y — Degradation Probability**
How often degradation fires per slice. Fully CCW = never, fully CW = always.

**Switch — Glitch Trigger Mode**

| Position | Mode |
|----------|------|
| UP (latching) | Probabilistic — glitch fires randomly based on Main Knob threshold |
| MID | External gate — glitch while Pulse In 2 is HIGH |
| DOWN (momentary) | Force — always glitch |

When not glitching, live audio passes through unaffected.

## LEDs

| LED | Function |
|-----|----------|
| 0–4 | One lit = current ratchet zone (÷1 through ÷6) |
| 5 | Brightness = reverse probability threshold |

## Quick start

1. Patch a clock into Pulse In 1 and audio into Audio In 1
2. Set Switch DOWN — you should hear the beat repeating immediately
3. Turn Main Knob clockwise through the zones to hear higher subdivisions
4. Move Switch UP and use the probability threshold within each zone
5. Bring up Knob X for degradation, Knob Y to control how often it applies
6. Patch a gate into CV In 1 to freeze the buffer at a moment you like
