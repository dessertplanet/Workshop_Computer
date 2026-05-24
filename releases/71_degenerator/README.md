# Degenerator

A layered audio looper for the [Music Thing Modular Workshop Computer](https://www.musicthing.co.uk/Workshop-Computer/). Record a loop, then shape it by adding and removing material like clay on a spinning wheel. Nothing is ever the same twice. Inspired by [William Basinski's *The Disintegration Loops*](https://en.wikipedia.org/wiki/The_Disintegration_Loops).

## Concept

Think of a potter's wheel. The loop is the spinning wheel, the buffer is the clay. You can add clay or carve it away, but you can never undo what's been done. Every pass leaves its mark.

- MIX mode presses new clay onto the form. Audio input is added to the buffer, layer over layer.
- DEGRADE mode carves the clay. Effects eat into the buffer, reshaping it irreversibly with each pass.
- Big Knob at zero freezes the current state. The wheel keeps spinning but nothing is added or removed.

There is no undo. There is no preview. Every change is permanent. The only way to preserve a moment is to stop adding and stop carving.

The Basinski connection: he recorded tape loops that were literally falling apart, oxide shedding, hiss accumulating, signal disintegrating. Degenerator does the same thing but adds the other half: building up. A piece can grow and erode in turns, accumulating layers of new sound and layers of decay, until it becomes something neither the original recording nor any single pass could predict.

## Controls

| Control | Function |
|---------|----------|
| Big Knob | Mix level (MIX) / Rate of change (DEGRADE). Full left freezes state |
| Knob X | Harmonic effects: Saturation → Filter Drift → Tape Hiss |
| Knob Y | Destructive effects: Oxide Shedding → Bit Crush → Bit Rot |
| Z Switch | Down = RECORD or slot modes (context-dependent), Middle = MIX, Up = DEGRADE |

Big Knob at full left means no change, the current state is frozen. Turning it clockwise increases the rate of change, from barely perceptible to instant.

## Z switch modes

| Position | Mode | Big Knob | Knob X | Knob Y |
|----------|------|----------|--------|--------|
| Down | RECORD or slot mode | Record: not used, Slot mode: select slot | — | — |
| Middle | MIX | Mix level (quadratic `>>16`). Below ~1.2% (<50) = bypass | Shapes incoming audio through harmonic effects | Shapes incoming audio through destructive effects |
| Up | DEGRADE | Commit rate (quadratic `>>15`). Below ~1.2% (<50) = bypass | Selects harmonic degradation applied to buffer | Selects destructive degradation applied to buffer |

## Slot modes (flash save/load)

- `SELECT_SLOT`: hold Z down at boot, or send `Pulse In 2` while Z is down. Big Knob selects slot `0-3`. Release Z to load the selected slot and return to MIX/DEGRADE. If the slot is empty, the module enters RECORD.
- `STORE_SLOT`: from MIX or DEGRADE, move Z down with Big Knob near zero (`< 50`). Big Knob selects slot `0-3`. Press Z down to write the current buffer. Move Z up to cancel back to DEGRADE. Audio mutes briefly during flash write.

### RECORD (Z down)

Audio input writes directly into the buffer, overwriting whatever was there. Recording runs until the buffer is full (~5.0 seconds) or you flip the Z switch away from Record. When the buffer fills, the card auto-switches to MIX.

The Big Knob has no effect during recording. Its position in MIX or DEGRADE mode depends on where you left it. A good habit: start with the Big Knob at zero. That way, when recording finishes and you switch to MIX, nothing layers in until you turn the knob up.

You can record silence by leaving Audio In 1 unpatched. This gives you a 5.0-second empty buffer to build up from nothing using MIX mode.

### MIX (Z middle)

The loop plays back while audio input is mixed in on top. The Big Knob controls how much of the input is added:

- Full left: nothing added, pure playback of current buffer
- Center: input mixed in at ~25% level, subtle layering
- Full right: input at full level, each pass adds the full signal

The X and Y knobs both shape the incoming audio before it's mixed in. X adds harmonic color (saturation, filter drift, tape hiss) and Y adds destructive texture (oxide shedding, bit crush, bit rot). You can use them independently or together. Saturation warming up the input before Bit Crush crunches it gives a very different result from either alone. You can record clean audio and mix in degraded versions, or vice versa.

MIX is additive. You're building up. Each pass adds more clay to the wheel.

### DEGRADE (Z up)

The loop plays back while effects eat into the buffer. The signal path is sequential:

```
readSample → X effects (harmonic) → Y effects (destructive) → blend with original → write back
```

X runs first, then Y processes the result. So Filter Drift warming up the signal before Bit Crush is different from Bit Crush before Filter Drift. The Big Knob controls the commit rate:

- Full left: no change (frozen state)
- ~25%: slow erosion, many passes before you notice
- ~50%: moderate carving, audible within a few loops
- ~75%: aggressive reshaping
- Full right: near-instant transformation

DEGRADE is subtractive. You're carving the clay away. It never grows back.

## Knob X: harmonic effects

Three zones with smooth crossfading between neighbors. Full left = no effect.

| X Knob | Zone |
|--------|------|
| 0% | No effect |
| 0 to ~33% | Saturation |
| ~33 to ~67% | Filter Drift |
| ~67 to 100% | Tape Hiss |

Between zones, both effects are active and crossfaded. At the boundary between Saturation and Filter Drift, you hear both warming and darkening at the same time.

### Saturation (X zone 0)

Even-harmonic warmth and soft compression. The signal gets thicker and more present without hard clipping. Higher X means more drive and more high-frequency rolloff, from subtle warmth to thick overdrive.

### Filter Drift (X zone 1)

The tone wanders. A lowpass filter drifts downward with each loop pass. The sound gets darker and more muffled over time, like aging capacitors in old equipment. At high settings, a highpass stage adds a thin, ghostly quality. This effect accumulates: each pass makes the filter a little darker, and it never recovers.

### Tape Hiss (X zone 2)

Analog noise floor added to the signal. White and pink noise are mixed, shaped by an envelope follower that ducks the noise when the signal is loud (like tape companding). Higher X means more noise. Use this to age a clean recording into something that sounds like it's been on tape for decades.

## Knob Y: destructive effects

Three zones with smooth crossfading. Full left = no effect. These are the carving tools. They damage the signal.

| Y Knob | Zone |
|--------|------|
| 0% | No effect |
| 0 to ~33% | Oxide Shedding |
| ~33 to ~67% | Bit Crush |
| ~67 to 100% | Bit Rot |

### Oxide shedding (Y zone 0)

Tape oxide physically flakes off, causing dropouts at specific positions. The dropouts persist across passes: once a patch of oxide is gone, it stays gone. Higher Y means more frequent and longer dropouts. Patches gradually grow wider and drift in position. The sound is gaps and fading, moments of silence that return in the same spot every pass.

### Bit crush (Y zone 1)

Bit depth reduction and sample-rate decimation. The signal becomes stair-stepped and lo-fi. Higher Y means fewer bits (down to 6-bit) and lower sample rate, from clean CD to crusty old sampler. The effect is always audible. Even small amounts add a gritty digital texture.

### Bit rot (Y zone 2)

Random single-bit flips in the data. Unlike other effects that shape the sound, bit rot corrupts it at the binary level. The result is unpredictable digital glitches: brief bursts of noise, clicks, and tonal shifts that come and go randomly. Higher Y means more frequent flips. The damage accumulates with each pass.

## Effect combinations

Since X and Y are independent, you can combine any harmonic effect with any destructive effect:

| X + Y | Character |
|-------|-----------|
| Saturation + Oxide Shedding | Warm gaps, like worn tape with dropouts |
| Saturation + Bit Crush | Fuzzy lo-fi, overdriven into a sampler |
| Filter Drift + Oxide Shedding | Darkening decay, gets muffled as holes appear |
| Filter Drift + Bit Rot | Unstable medium, drifting tone with digital corruption |
| Tape Hiss + Bit Crush | Old and crusty, noisy and lo-fi at the same time |
| Tape Hiss + Bit Rot | Degraded medium, hissing tape with random digital errors |

Both X and Y crossfade at zone boundaries. With X at ~33% and Y at ~33%, you hear a blend of Saturation fading into Filter Drift, combined with Oxide Shedding fading into Bit Crush.

## Inputs and outputs

| Input | Function |
|-------|----------|
| Audio In 1 | Primary audio input, loop source (RECORD) or overdub source (MIX) |
| Audio In 2 | Secondary audio input, mixes with Audio In 1 when connected |
| CV In 1 | Adds voltage to the Big Knob |
| CV In 2 | Adds voltage to the Y knob |
| Pulse In 1 | Record trigger, rising edge starts recording |
| Pulse In 2 | Loop reset, or enter SELECT_SLOT when Z is down |

| Output | Function |
|--------|----------|
| Audio Out 1 | Processed loop output |
| Audio Out 2 | Audio In 1 monitor, always carries whatever's connected to Audio In 1 |
| CV Out 1 | Loop position, from 0V at start to ~+2V at end |
| CV Out 2 | Output envelope, CV proportional to output audio level |
| Pulse Out 1 | Loop boundary clock, pulse each time the loop wraps |
| Pulse Out 2 | Record complete, pulse when recording finishes |

### CV In 1

Adds voltage to the Big Knob. The effective value is `knob + cv`, where cv is the bipolar signal scaled to 0-4095. This lets you voltage-control mix level (MIX), degrade rate (DEGRADE), and slot selection in STORE/SELECT slot modes.

### CV In 2

Adds voltage to the Y knob, giving you voltage control over destructive effects. Sweep between Oxide Shedding (left), Bit Crush (middle), and Bit Rot (right). Modulate effect intensity with an LFO or envelope generator. Create evolving textures without touching the knob.

### Pulse In 1

Starts recording on a rising edge. Connect another module's clock or trigger to start recording in sync with a sequence.

### Pulse In 2

Resets loop position to the start when Z is not down. A rising edge sets `phasePos = 0` and resets all degradation state. When Z is down, the same trigger enters `SELECT_SLOT` mode. Useful for external clock sync, resyncing after exploring degradation, or jumping into slot selection without power-cycling.

### Pulse Out 1

Fires a pulse at the loop boundary. Each time the playback position wraps around to the start, a pulse is generated. Use it to drive other loopers, step sequencers, or clock inputs on other modules.

### Pulse Out 2

Fires when recording finishes. After the buffer fills, the card auto-switches to MIX and sends a pulse on this output. Use it to trigger a second recorder, light an LED, or start another module's recording.

### CV Out 1

Outputs a CV proportional to the current playback position, from ~0V at the loop start to ~+2V at the end. The slope depends on loop length. Sweep a filter cutoff in sync with the loop, control an LFO rate, or modulate a delay time.

### CV Out 2

Outputs a CV proportional to the output audio level. An envelope follower tracks the level with fast attack and slow decay. Use it to duck other sounds via a VCA (sidechaining), modulate a filter for tremolo, or trigger other modules when the output gets loud.

## LEDs

| LED | Meaning |
|-----|---------|
| LEDs 0-1 | Output level (brightness = amplitude) |
| LED 2 | Big Knob position |
| LED 3 | Y Knob position (destructive effect) |
| LED 4 | Loop boundary, flashes each time the loop repeats |
| LED 5 | Mode: dim = MIX, bright = DEGRADE (intensity scales with knob) |

## First-time walkthrough

### Starting from scratch

The simplest way to begin: start with the Big Knob at zero, record silence, then build from MIX.

1. Turn the Big Knob all the way left (zero). Don't patch anything into Audio In 1 yet.
2. Flip Z down. Since nothing is patched in, you're recording silence: a clean, empty buffer. Recording runs until the buffer is full (~5.0 seconds) or you flip Z away.
3. The card auto-switches to MIX. Since the Big Knob is at zero, nothing layers in. You hear the empty loop.
4. Patch audio in. Connect something to Audio In 1: an oscillator, a microphone, another module.
5. Turn the Big Knob up to ~25% in MIX. Audio starts mixing into the buffer with each pass. Let it run for a few loops and hear the sound accumulate.
6. Shape the input. Turn X to ~20% (Saturation zone) to warm up what's coming in. Turn Y to ~40% (Oxide Shedding zone) to add dropouts to the incoming audio before it mixes in. The effects only touch the input in MIX mode, the buffer stays clean until the audio is written.
7. Switch to DEGRADE. Flip Z up. Start with the Big Knob at ~25%. Turn X to ~50% (Filter Drift zone) and Y to ~30% (Oxide Shedding). The loop starts evolving on its own, darkening and shedding.
8. Let it run. Don't touch anything. After 10, 20, 30 passes, the sound becomes unrecognizable.
9. Freeze. Turn the Big Knob all the way left. The current state is locked. Listen.
10. Build again. Flip Z back to Middle, turn the Big Knob up, layer new material over the degraded loop. The erosion stays, but new sound accumulates on top.
11. Degrade again. Flip Z up. A new layer, a new target. Each cycle builds and erodes.

### Recording audio directly

If you want to start with existing audio rather than building from silence:

1. Start with the Big Knob at zero. This way, when recording finishes and you switch to MIX, nothing layers in until you turn the knob up.
2. Patch audio in. Connect your source to Audio In 1.
3. Flip Z down. Audio writes directly into the buffer. Recording runs until the buffer is full (~5.0 seconds) or you flip Z away.

### Making fragments

For short, percussive, corrupted fragments:

1. Flip Z down and record a short hit: a drum, a chord stab, a word. Flip Z to Middle when you've captured what you want.
2. Switch to DEGRADE, turn the Big Knob to ~75%
3. Turn X to ~67% (Tape Hiss zone) and Y to ~80% (Bit Rot zone)
4. The loop gets destroyed within a few passes, stuttering, glitching, collapsing into noise
5. Freeze at any moment to capture the fragment before it disintegrates further

### Building drones

For long, slowly evolving drones:

1. Flip Z down and record a drone, a chord, or silence. Flip Z to Middle when you've captured enough.
2. In MIX, set the Big Knob to ~10-15% for very subtle layering
3. Patch in a slow-moving source and let it accumulate over many minutes
4. Switch to DEGRADE with the Big Knob at ~10% and X at ~30% (Filter Drift start)
5. The drone darkens imperceptibly each pass. A piece that evolves over hours.

## Performance workflow

### Shaping a piece

1. Record. Start with the Big Knob at zero. Flip Z down, capture audio, flip Z to Middle when you've got what you want. Since the knob is at zero, nothing layers in yet.
2. Mix. Turn up the Big Knob to start overdubbing. The more you turn it, the more gets layered in. Use X to warm up the input (saturation), Y to crunch it (bit crush) before it mixes in. Each pass adds more.
3. Degrade. Switch to DEGRADE, dial in X for harmonic coloring and Y for destructive carving. Start low. Let it evolve over many passes. The sound keeps changing and never goes back.
4. Freeze. Turn the Big Knob to zero to hold the current state. Listen. Then decide: add more, carve more, or walk away.

### The no-undo discipline

There is no preview. There is no undo. Every decision is permanent. This is the point. Like throwing pottery, you can add clay or carve it away, but every mark stays. The piece evolves through accumulation and erosion, and the only way to keep a particular shape is to stop working on it.

A loop that has been degraded for 30 minutes and then layered with new material in MIX mode becomes something entirely different from what was recorded. That's not a bug. It's the whole idea.

## Technical notes

### Signal flow

```
MIX mode:
  buffer → readSample
  audioIn → X effects → Y effects → scaled by mixLevel → added to readSample → buffer + output

DEGRADE mode:
  buffer → readSample → X effects → Y effects → blended with readSample at rate → buffer + output
```

### Mix level and commit rate

Both MIX level and DEGRADE rate use quadratic scaling from the Big Knob:

```
MIX level:  (knob * knob) >> 16
DEGRADE rate: (knob * knob) >> 15
```

MIX uses `>>16` (quadratic range 0-255) so that the knob must be turned further before overdub becomes audible. DEGRADE uses `>>15` (range 0-511, clamped to 255) for a faster rate of change at lower knob positions.

| Big Knob | Level/Rate |
|----------|-----------|
| ~7% (256) | ~1 |
| 25% (1024) | ~32 |
| 50% (2048) | ~128 |
| 75% (3072) | ~236 |
| 100% (4095) | 255 |

In DEGRADE, a rate of 128 means 50% of the affected sample replaces the original per pass. A rate of 1 means the change is nearly imperceptible per pass but accumulates over many loops.

### Knob reference tracking

When entering MIX or DEGRADE, the module records the Big Knob's current position. The effect engages only after you turn the knob past this reference point. This prevents accidental changes if the knob happens to be in a sensitive position when switching modes.

For example: if the Big Knob is at 75% when you flip to MIX, the mix level stays at zero until you turn past 75%. Turn back to 25% and the mix level follows downward as well. The reference updates whenever you cross it in either direction.

### Buffer

- 240,000 samples at 48 kHz with μ-law companding = ~5.0 seconds
- Fixed-point playback at 1x speed
- All arithmetic is integer with bit-shifts (no floating point)

### μ-law companding

Audio is stored in the buffer as 8-bit μ-law codes rather than raw 16-bit samples. This doubles the loop length from ~2.6s to ~5.0s at the cost of a small quantization error:

- Encode: 4,096-entry lookup table (4 KB in flash), indexed by `sample + 2048`
- Decode: 256-entry lookup table (512 bytes in flash), single table read
- Maximum quantization error: 44 LSBs (2.1%), near-perfect at low signal levels
- Error does not accumulate across degrade passes — each encode/decode cycle independently quantizes

### Key parameters

- Bypass threshold: Big Knob < 50 = no change
- X/Y bypass: each knob at full left (near 0) = effect off
- DEGRADE commit rate: quadratic, 1-255
- MIX overdub level: quadratic, 1-255
- Buffer size: 240,000 samples at 48 kHz (~5.0s), always full length in RECORD
- All effects use their respective knob position as intensity (not the Big Knob)

## Building

```sh
# Configure (first time)
cmake -S . -B build

# Compile
cmake --build build -j$(sysctl -n hw.logicalcpu)

# Output: build/degenerator.uf2
```

Flash by holding BOOTSEL and copying `degenerator.uf2` to the mounted drive.

## Web interface

The Degenerator Manager is a browser-based tool for uploading and downloading loops to the module's flash memory. It uses WebUSB to communicate with the Pico in BOOTSEL mode.

**Run locally:** start a web server in the `web/` directory:

```sh
# Option A: use the convenience script
./web/serve.sh

# Option B: Python one-liner
cd web && python3 -m http.server 8080
```

Then open `http://localhost:8080` in Chrome or Edge (WebUSB is required; Firefox and Safari are not supported).

## Credits

Inspired by William Basinski's *The Disintegration Loops* (2002-2003): the discovery that archived tape loops were physically disintegrating during digitization, and the decision to record the process rather than fight it.

The pottery metaphor came from playing with the card and noticing that adding and removing felt like working with clay on a wheel. Build up, carve away, and what remains is shaped by every pass.

Built using opencode and various open models and even some coding by myself. Thanks to Tom Whitwell for the Workshop Computer platform and the Music Thing Modular community.

## License

This project is released under the [MIT License](LICENSE). Use it, modify it, fork it, break it, improve it. No warranties, no liabilities. If you build something from this, you don't owe me anything — but I'd love to hear about it.
