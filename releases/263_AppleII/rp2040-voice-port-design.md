# RP2040 SSI-263 Voice Port — Design Context

Design doc for a Music Thing Modular Workshop System **Computer** card
(`releases/263_AppleII`): porting the Casso SSI-263 formant speech synthesizer
to the RP2040, driven by polyphonic MIDI, for vocoder-style behavior (N voices
speaking the same words in lockstep at different pitches).

## Goal

- N simultaneous voices, all uttering the **same words**, at the **same time**
  and **same rate**.
- The only per-voice difference is the **fundamental frequency**, from
  polyphonic MIDI (note number + pitch bend).
- The **words themselves** are typed into a WebMIDI browser app and pushed to
  the card as **MIDI SysEx** (see "Text & control transport").
- Target: RP2040 (dual Cortex-M0+, **no FPU**). Audio out is the Computer's
  `ComputerCard::AudioOut` at a **fixed 48 kHz**, values **-2048..2047** (PWM/DAC,
  **not** I2S — the framework owns the output timer/ISR).

## Source engine

The synth is a clean-room SSI-263A model (formant speech synthesis), currently
two self-contained C++ files in the Casso project:

- `Ssi263.h` (~285 lines) — class + 5-register model + public API.
- `Ssi263.cpp` (~1,100 lines) — synthesis + a 64-entry `constexpr` phoneme
  table.

It is **fully self-contained**. The only non-STL dependency is a `Byte`
typedef (`unsigned char`). No Windows, no framework, no interrupt/EHM
machinery in the synthesis path. The card/bus/VIA/PSG classes that surround it
are **not** needed and should not be vendored.

### Vendoring checklist

1. Copy `Ssi263.h` + `Ssi263.cpp`.
2. Replace `Byte` with `uint8_t`.
3. Provide the real headers it uses: `<cmath>`, `<cstdint>`, `<numbers>`,
   `<algorithm>` (drop the Windows-heavy PCH).
4. Discard the audio-adapter and mixer glue; wire the sample output straight
   into `AudioOut` from `ProcessSample()` (the ComputerCard 48 kHz callback).
5. Drive it via MIDI (see below), not via emulated register writes — but keep
   the chip's real register/duration model as the internal timing truth (see
   "Fidelity over voder").

## Signal chain (per sample, per voice today)

```
glottal excitation (pulse train at inflection freq)
  -> tract resonator cascade: 3 x two-pole (F1, F2, F3)
  -> + parallel fricative branch (LFSR noise -> broad F2 resonator -> 3 one-pole LPs)
  -> radiation differentiator (+6 dB/oct lip radiation)
  -> output low-pass x2
  -> amplitude envelope
  -> clamp to [-1, 1]
```

Formant centers glide toward per-phoneme targets at an articulation rate; the
two source amplitudes (voiced / fricative) glide too, so boundaries ramp rather
than gate.

## The RP2040 problem: representation, not structure

The engine is `double`/`float` with transcendentals recomputed **every
sample**: roughly 6–7 `exp` and 3–4 `cos` per sample. On an FPU-less M0+ each
is a software routine; a naive port is ~5x over the entire core budget before
any multiply-adds.

But those transcendentals mostly compute **near-constants**:

- Resonator `r = exp(-pi * bandwidth / fs)` depends only on constants ->
  compile-time value per stage.
- Only `cos(2*pi*fc/fs)` varies, and `fc` glides slowly -> **cos LUT** indexed
  by quantized formant center.
- Envelope / glide `coef = 1 - exp(-1/(tau*fs))` use constant `tau` and `fs` ->
  a small **constant table** (attack/release + 8 articulation rates).

So the port is a **fixed-point (Q15/Q16) + LUT rewrite**, not a DSP redesign.
The topology ports intact. Drop to 22.05–24 kHz if headroom is tight.

### Fixed-point migration strategy

The float implementation is the waveform oracle. The comparison harness must
stay below 2% relative RMS error, with the current float-control port measuring
0.068% against `Ssi263` on the scripted vowel and fricative sequence. Do not
replace the entire numeric path in one change: that makes a phase, timing, or
Q-format error look like a vocoder error and removes the first useful failure
location.

Migrate in these stages, keeping the comparison harness green after each one:

1. Keep the public sample API as `float` and keep phoneme timing as `double`.
  Convert only at the final hardware output boundary. This prevents output
  normalization and fractional phoneme-cycle changes from contaminating DSP
  comparisons.
2. Convert the constant-coefficient audio-rate one-poles to fixed point:
  excitation smoothing, noise smoothing, fricative LPs, output LPs, and the
  envelope. Compare each state against the float implementation before moving
  on.
3. Convert the resonator state and coefficients. Keep formant positions and
  cosine lookup inputs in float until the fixed resonator recurrence matches;
  only then quantize the formant control path and replace `cos` with the LUT.
4. Convert the per-voice phase accumulators last. Check impulse indices and
  wrap positions directly against the float phase accumulator; a one-sample
  phase slip destroys sample correlation even when the spectrum sounds right.
5. Convert the hardware output representation and measure the resulting
  `AudioOut` range separately from the host/reference waveform comparison.

Every fixed-point value must document its format at the declaration and at
each conversion boundary. In particular:

- If frequency is Q16 and the result of `731 / F1` is Q8.24, the numerator is
  `731 << 40` when dividing by the Q16 frequency. A `<< 33` numerator is 128
  times too small.
- A Q8.24 envelope must be compared with a Q8.24 silence threshold, not a
  floating-point literal such as `0.001f`.
- Do not change `double` phoneme-cycle state to an integer tick count during
  the DSP migration. Fractional truncation changes phoneme boundaries and
  oscillator phase.
- If `GenerateSample()` returns Q8.24 during an embedded-only experiment, the
  comparison harness must be changed at the same time. Otherwise it will
  interpret fixed-point integers as floats or normalize a float twice.

The diagnostic harness should expose stage traces for impulse count, smoothed
excitation, each resonator, frication, radiation, output LP, envelope, and
final sample. Require correlation above 0.999 at each intermediate stage and
above 0.995 for the final waveform before deleting the corresponding float
stage. If correlation is near zero, stop tuning gain: the error is in timing,
phase, state initialization, or coefficient conversion.

## The vocoder optimization (the important part)

Because every voice runs the **same phoneme program in lockstep** and differs
**only in pitch**, everything except the glottal oscillator is identical across
voices sample-for-sample: phoneme timeline, formant glide, resonator
coefficients, source-level glides, and amplitude envelope.

The tract is a **linear** filter, so with shared coefficients:

```
sum_i( Tract(excitation_i) ) == Tract( sum_i(excitation_i) )
```

**Sum the excitations first, then run ONE tract.** This turns "N voices" into
"one synthesizer + N oscillators."

- The entire chain except the final clamp is linear; with shared coefficients
  it is one linear time-varying system for all voices.
- The fricative/noise branch is **unpitched** -> compute one shared hiss for
  the whole chord.
- Radiation diff, output LP, envelope: linear/shared scalars -> apply once to
  the summed mix.
- The clamp is the only nonlinearity and correctly sits at the very end, on the
  mixed output.

**Per-voice cost collapses to a phase accumulator**: advance
`glottalPhase += pitch/fs`, detect wrap, emit an impulse. ~2–3 fixed-point ops.
Even the excitation smoothing one-poles and voiced gain are linear/shared and
hoist out of the voice loop (sum the raw impulse trains, smooth once). Voice
*count* stops being the constraint; the single shared synthesizer is the cost.

Hardware validation on 2026-09-11 proved all eight configured voices with the
Daisy demo playing a fixed spread chord. The summed voiced excitation is
normalized by active voice count before entering the shared tract. Changes to
that normalization are slewed over approximately 2 ms, preventing a note-on or
note-off from abruptly rescaling voices that are already sounding. At 192 MHz
and a 24 kHz sample rate, the measured worst-case `ProcessSample()` time was
28 us with zero overruns against the 41.67 us callback budget. A short envelope
compressor handles remaining peaks without voice-count-dependent level changes.

## Target structure (restructure `GenerateSample` into 3 phases)

1. **Shared per-sample update** (once): advance phoneme timeline, glides,
   resonator coefficients, the fricative signal, and the envelope.
2. **Per-voice excitation** (N times, cheap): phase accumulators; sum their
   impulses into one excitation signal.
3. **Shared back-end** (once): one tract cascade over the summed excitation,
   add the shared fricative, radiation diff, output LP x2, envelope multiply,
   clamp.

Do this split **at the same time** as the fixed-point rewrite; retrofitting it
later is the painful path.

## Text & control transport (SysEx)

The spoken text is **not** compiled in. A WebMIDI browser app is the editor:
you type a statement, it is converted to an SSI-263 phoneme + duration program
and sent to the card as a **MIDI SysEx** message. The card holds a **bank of up
to six phrase slots** (see "Phrase bank"); SysEx addresses a slot by index.

SysEx payload (sketch, to be finalised in a `protocol.h` + `sysex_spec.json`
pair like other cards):

- **Set phrase**: manufacturer/prefix bytes + opcode + **slot index (0..5)** +
  the phoneme program. The program is grouped into **steps** (syllables): each
  step is optional onset consonant(s), one **held nucleus** (the sustaining
  vowel), and optional coda consonant(s), so STEP-mode pacing knows where a key
  may hold. Per-phoneme duration/rate/inflection fields ride along, packed
  7-bit. The web app owns syllabification and emits the step markers.
- **Playback mode**: `ONCE` (utter the phrase a single time) vs `LOOP` (wrap to
  the beginning and keep going) — see "Utterance timing".
- **Bank ops**: clear a slot, query slot occupancy, and (optionally) select the
  active slot from the browser.
- **Transport / immediate**: speak-now, stop, and (optionally) parameter pokes.

The browser is the source of truth for *what* is said; the panel/MIDI notes
govern *when* and *at what pitch*.

## Phrase bank — six slots, knob-selected

The card stores **up to six phrases** at once, one per slot, all authored and
loaded from the web app. Slots persist in flash so the bank survives a power
cycle; the web app is the only editor.

At runtime the **Main knob selects the active slot**: its 0..4095 range is
quantized into 6 bands (slot 0..5), with a little hysteresis at the band edges
so a knob parked on a boundary does not flicker between phrases. The active
slot is the phrase the notes speak.

The **six panel LEDs map one-to-one to the six slots**; the LED of the active
slot is lit. Empty slots can be shown dim/off so the performer sees which
slots the web app has actually filled. Switching slot takes effect at the next
phrase boundary (or immediately, mode-dependent) rather than mid-phoneme, so
the change is clean.

## Utterance timing — ONCE vs LOOP

Two browser-selectable behaviors for what happens at the **end of the phrase**
(orthogonal to STEP/FLOW pacing, which sets how the cursor is advanced):

- **ONCE** — one pass through the step program, then silence (envelope releases
  at the end of the last step).
- **LOOP** — on reaching the last step, **wrap to step 0** and continue, so the
  phrase repeats: in FLOW it keeps rolling while gated; in STEP the next onset
  after the last step starts the phrase again.

In FLOW the program is a single shared timeline advanced once per sample in the
"shared per-sample update" phase; ONCE/LOOP only changes the end-of-program
boundary. In STEP the cursor is advanced by onsets, and ONCE/LOOP only changes
what the onset after the last step does.

## MIDI note mapping — pitch and pacing

Notes carry **pitch** and **pacing**; SysEx carries the words.

- **Pitch**: note number + pitch bend -> a voice's fundamental, replacing the
  register-derived inflection pitch that feeds that voice's glottal oscillator.
- **Voice allocation**: note-on allocates a phase-accumulator voice; note-off
  releases it. N held notes = N-voice chord, all speaking the shared phrase.
- **Pacing**: **STEP** is the pacing model for v1 (below). **FLOW** is a
  planned later addition, kept in this doc so the phrase format and state
  machine leave room for it, but not built first.

## Pacing modes — STEP (v1) and FLOW (later)

### STEP — one syllable per key, held while down (v1)

Note events drive the phrase directly; there is no free-running clock.

- **Advance on onset from silence**: only a **0 -> >0 voices** transition
  advances the cursor **one step** — to the next syllable. Its onset
  consonant(s) articulate fast (at the chip's own DUR/rate for those phonemes),
  then the step settles on its **held nucleus**. Notes played *while already
  sounding* do **not** advance: they join or re-voice the current syllable, so
  you can bring in and change additional voices on the held syllable before
  moving on. To step, drop back to silence (release all keys) and play again.
- **Hold while gated**: the nucleus sustains for as long as the key(s) are
  held — the envelope parks at the nucleus level and the glottal excitation
  keeps running at the played pitch, so the vowel sings the note. The cursor
  does **not** advance while simply holding.
- **Release**: note-off fires the step's coda consonant(s), if any, then
  releases the envelope. All notes up ends the utterance for that step.
- **Lockstep polyphony**: a chord speaks one syllable together. Because only an
  onset from silence advances, you can stack, swap, and re-pitch voices freely
  on the current syllable; the phrase moves on only after every key is released
  and a new one is struck.
- **Wrap**: at the last step, the next onset from silence wraps to step 0
  (LOOP) or stops (ONCE) — see "Utterance timing".

This makes the keyboard play the *rhythm of the speech*: each keypress is a
syllable, sustained as long as the finger holds, exactly as asked.

### FLOW — free-run at inter-onset rate (later, not in v1)

A planned second mode; documented here so v1's structures accommodate it, but
deferred. The phrase auto-advances on a shared timeline; notes carry mostly
pitch.

- **Gate**: the timeline advances only while at least one note is held; all
  notes up pauses/releases it (ONCE stops; LOOP freezes).
- **Legato / re-articulation**: a legato transition (voice count staying > 0)
  changes pitch without restarting; a fresh onset from silence (0 -> 1)
  restarts at step 0 (or resumes, mode-dependent).
- **Rate**: articulation rate is driven by **inter-onset note spacing** — the
  time between successive note-ons sets how fast the phrase articulates, so
  playing faster speaks faster. The chip's own DUR + rate register fields are
  the base timing model (`GetPhonemeDurationSec()`); the measured inter-onset
  interval scales that base (via `Ssi263::SetTickClock`, which exists exactly
  to retime phonemes without touching XCK). A running average / one-pole over
  recent onsets keeps the rate from jumping on a single irregular gap.

## USB — boot-selected device OR host (single native port)

The Computer has **one** native USB-C port; its role is chosen **at power-on**,
not simultaneously. This is the established dessertplanet idiom (see
`releases/15_MLRws`): `tusb_config.h` sets
`CFG_TUSB_RHPORT0_MODE (OPT_MODE_HOST | OPT_MODE_DEVICE)`, then the constructor
branches on `ComputerCard::USBPowerState()`:

- `!= UFP` (the Computer is the downstream/host, i.e. a **Keystep** is plugged
  into the front) -> **USB MIDI host** (`tuh_*` + the vendored
  `usb_midi_host.*`): read note-on/off from the keyboard for pitch + pacing.
- `== UFP` (the Computer is a device plugged into a **host computer**) ->
  **USB MIDI device** (`tud_*`, `CFG_TUD_MIDI 1`): receive **both** the SysEx
  phrase-bank/mode messages **and note events**. The card still makes sound in
  device mode — note-on/off arriving from any app or device on the host (a DAW,
  sequencer, or another controller) drives pitch + pacing exactly as the
  Keystep does in host mode. The **website** specifically only sends SysEx (it
  is the phrase editor and does not need to send notes), but the device MIDI-in
  path handles notes from whatever else is connected. The panel (Main-knob slot
  select, switch/pulse gating) is always available too.

So the note/pacing engine is identical in both modes; only the MIDI *transport*
differs (`tuh_midi_stream_read` vs `tud_midi_stream_read`). SysEx is received in
device mode (and, if a controller sends it, host mode too). Note that the USB
power circuitry may only latch the correct port state on a cold power-up, not a
bare RP2040 reset — so the role is fixed for the session at power-on.

Current device-mode bring-up accepts note-on and note-off messages on all MIDI
channels through a 16-entry single-producer/single-consumer queue. The first
note switches from the built-in eight-voice demo chord to DAW-controlled pitch;
the Daisy phoneme timeline continues to loop. Up to eight held notes are
allocated, with oldest-voice stealing when full, and all-notes-up closes the
synthesis output gate with its normal release. Pitch bend, STEP pacing, SysEx,
and host-mode note parsing are not implemented yet.

So: **Keystep session = host mode**, **website session = device mode**, decided
by what is connected when the card powers up (a distinct boot LED animation for
each, as MLRws does). A given power cycle is one or the other.

## Core split

Audio ISR on **core 0** (`ComputerCard::Run()`), USB stack pumped on **core 1**
(`tuh_task()`/`tud_task()` in a tight loop). This matches every working USB
card on this hardware; the audio ISR has a hard ~20.8 us deadline and must not
share a core with the USB polling loop.

## Constraints / non-goals

- The real SSI-263 is **monophonic**; polyphony here is N model instances (or,
  better, N oscillators into one shared synthesizer). Legitimate, but past
  hardware behavior.
- The superposition trick is valid **only** while voices share phoneme,
  timing, and amplitude. Per-voice timbre, detuned articulation, or independent
  words break it and pull the tract back into the voice loop.
- This is the floating-point engine as a starting point; shipping requires the
  fixed-point/LUT rewrite. Vendoring is trivial; the rewrite is the work.

## Fidelity over voder

Where a choice arises, **prefer faithfulness to the modelled SSI-263 over
copy-pasting `releases/105_voder`**. The voder is a useful *scaffolding*
reference only — its ComputerCard wiring, dual-core USB pump, Q15 DSP idioms,
`CMakeLists.txt`/`info.yaml` conventions, and verification-tool pattern
(`tools/*.py`). It is **not** a reference for the speech model itself: its
excitation, 8 fixed bandpass bands, vowel cube, and ad-hoc chatter are Homer
Dudley's Voder, a *different* machine. The SSI-263's own phoneme set, the
5-register model, the DUR/rate/inflection/articulation timing, and the
formant-glide behavior come from the Casso engine and must be preserved. In
particular, phrase pacing should be built on the chip's real duration/rate
model, not on the voder's syllable chatter.

## Casso source — vendored, self-contained

Both files are in the release dir and are **fully self-contained**:

1. **`Ssi263.h`** — class, 5-register model, public API, and the complete
   datasheet timing model in comments (Frame/Phoneme duration, inflection,
   filter formulas).
2. **`Ssi263.cpp`** — synthesis + the 64-entry phoneme `constexpr` table
   (extracted from visual6502 die shots of the SSI 263P; replaceable wholesale
   via `SetFormantTable`).

The **only** external dependency is `#include "Pch.h"` in both files, which
supplies the `Byte` typedef and the STL headers. **Nothing more is needed from
Casso** — replace `Pch.h` with a tiny shim:

```cpp
// Pch.h (port shim)
#pragma once
#include <cstdint>
#include <cmath>
#include <numbers>
#include <algorithm>
using Byte = uint8_t;
```

The Mockingboard/VIA/bus register-driver is **not** required: the chip's public
API already specifies how to drive it — `WriteRegister()` to set DUR/phoneme/
rate/inflection/CTL, `Tick(cycles)` to advance emulated (TICK-clock) time,
`IsRequesting()` for the A/R "feed the next phoneme" signal, and
`GetPhonemeDurationSec()` for pacing. The SysEx-text -> phoneme-program ->
pacing path is built on these, not on emulated bus writes.

> Note the two clock domains the header warns about: **XCK** (chip time base,
> all the datasheet formulas divide by it) vs the **TICK** clock (the rate
> `Tick()` counts in). `SetTickClock` is the intended hook for retiming phrases
> from inter-onset note spacing without disturbing XCK-derived pitch/filter.
