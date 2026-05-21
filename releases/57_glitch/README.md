# Glitch — Workshop Computer Program Card

A clock-synced beat-repeater and audio degradation effect. Glitch continuously records incoming audio into a circular buffer, then — when triggered — replays a frozen slice of that buffer at a subdivided rate, with optional reversal and lo-fi degradation.

![Glitch key](GLITCH_key.jpg)

## Installation

1. Workshop computer connected via (data compatible) USB cable
2. Power cycle
3. Hold secret button under knob on Workshop Computer, press reset button on WC, release secret button — it mounts as a drive called RPI-RP2
4. Drag and drop `glitch.uf2` onto the drive
5. The Workshop Computer reboots automatically and Glitch is running

---

## Inputs

### Audio (top row)
| Jack | Function |
|------|----------|
| Audio In 1 | Main audio input — continuously recorded into the buffer |

### Pulse (bottom row)
| Jack | Function |
|------|----------|
| Pulse In 1 | **Clock input** — rising edge sets the beat length. Each new pulse measures the time since the last pulse (in samples) to define MasterLoopLength. Maximum buffer is 0.5 seconds; longer beats are capped. |
| Pulse In 2 | **External gate** — used only when Switch is in MID position. While HIGH, forces glitching on the current slice. |

### CV (middle row)
| Jack | Function |
|------|----------|
| CV In 1 | **Freeze** — treated as a comparator. When the voltage is above ~0V (ADC value > 2047), the buffer stops recording. The frozen content loops continuously. Below ~0V, recording resumes. |

---

## Outputs (top row)

| Jack | Function |
|------|----------|
| Audio Out 1 | Glitched output — or live pass-through when not glitching |
| Audio Out 2 | Always dry — live pass-through of Audio In 1 regardless of glitch state |
| Pulse Out 1 | Sub-slice clock — fires a one-sample pulse at every ratchet slice boundary |
| CV Out 1 | Glitch gate — high (~+5V) while glitching, low (0V) while passing through |
| CV Out 2 | Descending ramp — falls from ~+5V to 0V across each slice, resets at boundary. Patch to a VCA to fade out each repeated slice. |
| Pulse Out 2 | Clock mirror — passes Pulse In 1 straight through |

---

## Controls

### Main Knob — Ratchet Zone + Probability

The Main Knob does two things simultaneously, hidden within a single sweep.

The knob range (0–4095) is divided into **5 equal zones**. The zone you are in selects the **ratchet division** — how many times the captured beat slice is subdivided and repeated:

| Zone | Ratchet | Effect |
|------|---------|--------|
| 1 (fully CCW) | ÷1 | Whole beat repeated once |
| 2 | ÷2 | Beat cut in half, each half repeated |
| 3 | ÷3 | Beat cut into thirds |
| 4 | ÷4 | Beat cut into quarters |
| 5 (fully CW) | ÷6 | Beat cut into sixths |

**Hidden within each zone:** the position of the knob *within* that zone sets the **Reverse Probability Threshold**. At the bottom of a zone there is no reversal; approaching the top of the zone, random reversal becomes increasingly likely. This probability also controls whether glitching fires at all in Probabilistic mode (Switch UP).

LED 5 (bottom right) shows the current probability threshold as brightness — use it to navigate within zones.

---

### Knob X — Degradation Amount

Controls the depth of two simultaneous lo-fi effects applied to glitched slices.

**Bitcrushing** — reduces the bit depth of the audio. At minimum (fully CCW) the audio is clean. As the knob increases, low bits are progressively masked off: at mid-range you get 8-bit grit; near maximum, extremely coarse quantisation.

**Decimation (sample-rate reduction)** — quantises the read position so the same sample plays multiple times before advancing. Creates a stepped, aliased texture. At minimum, smooth playback. At maximum, the signal advances in steps of 16 samples, giving a heavily lo-fi character.

Both effects scale together as Knob X increases. At zero, degradation is completely bypassed regardless of Knob Y.

---

### Knob Y — Degradation Probability

Sets **how often** the degradation (bitcrush + decimation) actually applies to a given slice. This is evaluated fresh at every slice boundary.

- Fully CCW (0): degradation never applies, even if Knob X is turned up — the audio is always clean
- Mid-range: degradation fires roughly half the time, alternating between clean and degraded slices
- Fully CW (4095): degradation always applies

Combine Knob X (amount) and Knob Y (probability) to dial in anything from subtle occasional grit to relentless destruction.

---

### Switch — Glitch Trigger Mode

The switch selects how glitching (ratcheting + reversal) is activated. Glitch state is locked in for the full duration of each slice, then re-evaluated at the next slice boundary.

| Position | Mode | Behaviour |
|----------|------|-----------|
| **UP** (latching) | Probabilistic | At each slice boundary, a random number is compared to the Main Knob's hidden remainder threshold. If the random value falls below the threshold, glitching fires. Low threshold = rare glitches. High threshold = frequent glitches. |
| **MID** | External Gate | Glitching is active whenever Pulse In 2 is HIGH. Use an external gate, envelope, or sequencer to control exactly when glitching occurs. |
| **DOWN** (momentary) | Force | Glitching is always on, regardless of probability or gate. Useful for manual performance — hold down to freeze into the glitch, release to return to pass-through. |

When **not glitching**, the module passes live audio from Audio In 1 directly to the outputs. No buffer playback, no degradation.

---

## LEDs

```
| LED 0   LED 1 |
| LED 2   LED 3 |
| LED 4   LED 5 |
```

| LED | Function |
|-----|----------|
| LED 0 | Lit when Main Knob is in Zone 1 (ratchet ÷1) |
| LED 1 | Lit when Main Knob is in Zone 2 (ratchet ÷2) |
| LED 2 | Lit when Main Knob is in Zone 3 (ratchet ÷3) |
| LED 3 | Lit when Main Knob is in Zone 4 (ratchet ÷4) |
| LED 4 | Lit when Main Knob is in Zone 5 (ratchet ÷6) |
| LED 5 | **Brightness** = reverse probability threshold (the hidden remainder within the current zone). Dim = low probability, bright = high probability. |

Only one of LEDs 0–4 is lit at a time, showing your current ratchet selection at a glance.

---

## Signal Flow

```
Audio In 1 ──► Circular Buffer (0.5s) ──► [if glitching] Slice Playback ──► Audio Out 1
     │               ▲                           │
     └───────────────────────────────────────────────────────────────────► Audio Out 2 (dry)
                CV In 1 (Freeze)          Ratchet / Reverse
                                          Decimate / Bitcrush

               Pulse In 1 (Clock) ──► MasterLoopLength
               Pulse In 2 (Gate) ──► [Switch MID only] force glitch
               CV In 2 (bipolar) ──► offsets Knob X + Knob Y

               Slice boundary ──────────────────────────────────────────► Pulse Out 1
               do_glitch flag ──────────────────────────────────────────► CV Out 1 (gate)
               Slice position (inverted) ───────────────────────────────► CV Out 2 (ramp)
               Pulse In 1 ──────────────────────────────────────────────► Pulse Out 2 (mirror)
```

When not glitching, Audio In 1 passes straight to the outputs, bypassing the buffer entirely.

---

## Getting Started

1. Patch a clock or LFO square wave into **Pulse In 1**
2. Patch audio into **Audio In 1**
3. Listen on **Audio Out 1**
4. Set Switch to **DOWN** — you should immediately hear the beat repeating
5. Turn the **Main Knob** clockwise to hear higher ratchet subdivisions
6. Move Switch to **UP** and explore the probability zone within each ratchet setting
7. Bring up **Knob X** for degradation; use **Knob Y** to control how often it fires
8. Patch a gate into **CV In 1** (above 0V) to freeze the buffer at a particular moment

---

## Patch Ideas

### Self-patching

**Self-freezing loop** — Patch CV Out 1 (glitch gate) into CV In 1 (freeze). When glitching starts, the buffer automatically freezes, locking the loop tightly. The glitch feeds itself.

**Subdivided clock from a single source** — Patch one clock into Pulse In 1. Use Pulse Out 1 (sub-slice clock) to drive a sequencer or envelope at the ratchet subdivision rate, and Pulse Out 2 (mirror) to drive something else at the original beat rate. One clock in, two related clocks out.

**Ramp-controlled degradation** — Patch CV Out 2 (descending ramp) into CV In 2. As each slice plays, degradation increases — the repeat starts clean and gets progressively dirtier before resetting at the next boundary.

---

### With other modules

**VCA ducking on repeats** — Patch CV Out 2 into a VCA controlling the glitched output. The repeated slice fades out naturally across its duration, giving each ratchet hit a decaying envelope rather than a hard cut.

**Glitch-triggered envelope** — Patch CV Out 1 (glitch gate) into an envelope generator's gate input. Every time Glitch fires, an envelope opens — useful for adding a filter sweep, reverb send, or amplitude swell that only happens during glitch events.

**Sequencer sync** — Patch Pulse Out 1 into a sequencer clock input. The sequencer advances once per ratchet subdivision, so at ÷4 your sequence runs four times per beat. Change the Main Knob zone to change the sequence speed in musically useful ratios.

**Rhythmic filtering** — Patch CV Out 1 into a filter's CV input. The filter opens fully while glitching and closes during pass-through, creating a rhythmic timbral gate in sync with the beat-repeat events.

**Wet/dry blend** — Take Audio Out 1 (glitched) and Audio Out 2 (dry) into a mixer or crossfader. Blend between them manually, or automate the crossfade with a CV source for gradual glitch introduction and removal.

**External glitch trigger** — Set Switch to MID. Patch a sequencer gate, envelope, or LFO square wave into Pulse In 2. The sequencer now decides exactly which beats glitch, while Glitch handles the ratcheting and degradation.

**Freeze on demand** — Patch an envelope or manual gate into CV In 1. Press/hold to freeze the buffer at the moment you want to capture. Release to resume recording. Combine with Switch DOWN to simultaneously force-glitch and freeze — the buffer locks immediately into a repeating loop.

**Degradation from an envelope** — Patch a slow envelope or LFO into CV In 2 (bipolar mod). As the envelope rises, both degradation amount and probability increase together, sweeping Glitch from clean repeats into full lo-fi destruction and back.

---

## Notes

- Before the first clock pulse arrives, the module passes audio through without glitching
- Beat lengths longer than 0.5 seconds are automatically capped at the buffer size
- Very short beats (faster than ~2ms per subdivision) use the minimum slice of 1 sample with no fade
- The buffer records even while glitching — on the next beat, fresh audio is available
- Degradation is evaluated independently of glitch mode — you can have degraded pass-through if Knob Y is high and Switch is DOWN with no gate signal
