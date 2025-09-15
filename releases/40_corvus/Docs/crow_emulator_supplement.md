# Crow Workshop Emulator Supplement

Version: 0.3.2  
Target: Workshop Computer (RP2040) Crow-Compatible Environment  
Scope: Differences, supported subset, migration guidelines.

## 1. Overview

This document complements the original crow reference. It describes what the Workshop Computer crow emulator implements, what differs, and how to adapt existing crow scripts.  
Philosophy: keep core crow scripting idioms (outputs, actions, simple timing) while simplifying hardware-specific or bandwidth-heavy subsystems.

## 2. Hardware & Channel Mapping

| Logical crow | Emulator Channel | Physical Function (Workshop Computer) | Calibrated Pitch Support | Notes |
|--------------|------------------|---------------------------------------|---------------------------|-------|
| output 1     | 0                | AudioOut1 (DC-capable)                | No (continuous CV/audio)  | Always raw volts path (slew / ASL shaping applies). |
| output 2     | 1                | AudioOut2 (DC-capable)                | No (continuous CV/audio)  | Same as output 1. |
| output 3     | 2                | CVOut1                                | Yes (quantizable)         | Uses calibrated MIDI note -> DAC when scale enabled. |
| output 4     | 3                | CVOut2                                | Yes (quantizable)         | Same as output 3. |
| input 1      | 0                | AudioIn1                              | Raw volts read            | Modes mostly unimplemented (see below). |
| input 2      | 1                | AudioIn2                              | Raw volts read            | Same as input 1. |
| input 3      | 2                | (not wired)                           | Stub (0.0V)               | Reserved. |
| input 4      | 3                | (not wired)                           | Stub (0.0V)               | Reserved. |

Voltage Range Mapping: Internal normalized conversion treats ±(CROW_FULLSCALE_VOLTS) as full scale (currently ±6 V conceptually). Adjust constant if hardware safe range changes.

## 3. Processing & Timing Model

- Sample Rate: 48 kHz interrupt on Core 0.
- Block Size: 32 samples (vector-style block processing for slopes & detection).
- Core Split:
  - Core 0: Real-time sample loop, slopes (ASL execution primitives), event queue dispatch, detection scaffolding, hardware updates.
  - Core 1: Lua VM, metros, CASL, background USB, periodic task polling, script I/O bridging.
- Latency: One block worst-case (≈0.67 ms) for Lua-driven voltage changes to appear on an output (value broadcast then block fill).

Pipeline Order (per output):
1. Slopes / ASL generate per-sample trajectory into `output_block`.
2. Lua layer may atomically overwrite entire block with a static voltage (when a new `output[n].volts=...` arrives inside the block window).
3. Quantization (only outputs 3–4, if enabled) applied at final hardware write.
4. Hardware driver writes (continuous for 1–2; MIDI note or raw CV for 3–4).

## 4. Output Features

| Feature                           | Crow (Original)                             | Emulator Status | Notes |
|----------------------------------|----------------------------------------------|-----------------|-------|
| `output[n].volts`                | Immediate set                                | Supported       | All 4 outputs. |
| Slew (`output[n].slew`)          | Time-based glide                             | Supported       | Implemented via slopes system. |
| Shape (`output[n].shape`)        | Multiple curve types                         | Supported (core set) | Provided by slopes engine; advanced shapes mapped to existing internal curves. |
| Actions / ASL (`output[n].action`) | Full ASL mini-language                      | Partially Supported | Core constructs (`to`, `loop`, `held`, `times`, etc.) present; edge-case directives may differ. |
| `output[n]()` to start action    | Yes                                          | Supported       | |
| Quantization `output[n].scale()` | Any output, ET + JI                          | Limited         | Only outputs 3–4, ET-style numeric degrees only. No JI ratio interpretation yet. |
| Modify scale table directly      | Yes (`output[n].scale = {...}`)              | Supported       | Table assignment or function call; {degrees=,mod=,scaling=} accepted; 'none'/nil/{} disables. |
| Clock mode (`output[n]:clock()`) | Clock pulse generation                       | Supported       | All outputs; 0/5V gate; outs 3–4 quant temporarily bypassed; explicit volts cancels clock. |
| Output pulse / gate via action   | Via `pulse()`, `ar()`, etc.                  | Supported       | |

### Direct Scale Assignment & Clock Mode

Direct Scale Assignment:
You can now reassign a scale table directly:
```
output[3].scale = {0,2,4,7,9}
output[4].scale = { degrees = {0,3,7}, mod = 12, scaling = 1.0 }
output[3].scale = 'none'   -- disable
output[3].scale = {}       -- also disables
output[3].scale = nil      -- also disables
```
Function form still works:
```
output[3].scale({0,3,7}, 12, 1.0)
```

Clock Mode:
Generate periodic gates:
```
output[1]:clock(0.25)          -- 250 ms period, 10 ms default width
output[2]:clock(0.5, 0.02)     -- 20 ms pulse every 500 ms
output[1]:clock('stop')        -- stop
output[1].unclock()            -- equivalent
```
Details:
- Gate high = +5V, low = 0V.
- Works on all 4 outputs.
- Quantization (outputs 3–4) is temporarily bypassed during clock mode and restored when clock stops or a manual voltage is set.
- Any explicit `output[n].volts = ...` assignment cancels clock mode for that channel.

### Quantization (Outputs 3–4 Only)

Configuration path maps volts → MIDI note (0V = MIDI 60) → nearest allowed degree within modulus → calibrated pitch out:

```
-- Example: minor triad arpeggio on output 3
output[3].scale({0,3,7}, 12, 1.0)  -- degrees, divisions per octave, volts-per-octave
output[3].volts = 0.0              -- Middle C
```

Rules:
- Per-output (independent) scale state; no global shared mod/scaling.
- Degrees list clamped to `[0, mod-1]`.
- Internal rounding to nearest integer MIDI note (microtonal drift removed).
- Disabling:
  ```
  output[3].scale('none')
  ```

Unsupported:
- Passing `'ji'` or ratio tables.
- Calling on outputs 1–2 (silently ignored by underlying C++ function).

### Why scale is restricted to outputs 3–4
Only those channels use calibrated pitch helper functions (`CVOut?MIDINote`). Channels 1–2 are continuous wide-band outputs more suitable for arbitrary waveforms or unquantized CV.

## 5. Input Features

All primary crow input detection modes are now implemented for inputs 1–2 with an internal lock-free overwrite queue (size 64) bridging Core 0 (producer) to Core 1 / Lua (consumer). Inputs 3–4 remain stubs (0V) and do not generate events.

| Input Mode / Feature | Crow Reference Behavior                        | Emulator Status | Notes |
|----------------------|-------------------------------------------------|-----------------|-------|
| Raw `.volts`         | Instant read                                    | Supported       | Direct ADC-derived (calibration internal). |
| `.query`             | Report value to host                            | Not Implemented | Planned lightweight host message. |
| `mode='stream'`      | Periodic host events                            | Supported       | Interval in seconds → block countdown (arg: interval). Event: `input[n].stream(value)`. |
| `change`             | Threshold / hysteresis event                    | Supported       | Args: threshold, hysteresis, direction ('rising','falling','both'). Event: `input[n].change(state)` (state 0/1). |
| `window`             | Region-based events                             | Supported       | Args: windows{...}, hysteresis. Event: `input[n].window(index, direction)` (direction -1/1). |
| `scale` (quant detect)| Quantize + event                               | Supported       | Args: scale{degrees}, divs, scaling. Event: `input[n].scale(index, octave, note, volts)`. |
| `volume` (RMS)       | Audio RMS tracking                              | Supported       | Args: interval. Event: `input[n].volume(level)`. Level = smoothed RMS / envelope hybrid. |
| `peak`               | Transient detection                             | Supported       | Args: threshold, hysteresis. Event: `input[n].peak()`. Internal envelope with release. |
| `freq`               | Frequency analysis                              | Supported (basic) | Args: interval. Zero-cross period → Hz. Unipolar edge fallback TBD. Event: `input[n].freq(hz)`. |
| `clock`              | External clock detection                        | Supported (basic) | Args: threshold[, hysteresis, min_period]. Rising-edge period → BPM & period_s with EMA smoothing (alpha=0.2). Event: `input[n].clock(bpm, period_s)`. |
| Inputs 3–4           | Physical CV                                     | Stub (0V)          | No detection; hidden from typical scripts. |

Input Clock Mode (New):
```
-- Configure clock detection on input 1 (threshold=1.0V, hysteresis=0.1V, min period 20ms => ignores >3000 BPM noise)
set_input_clock(1, 1.0, 0.1, 0.02)
input[1].clock = function(bpm, period)
  print(string.format("clock bpm=%.2f period=%.4f", bpm, period))
end

-- Example: tighter debounce (min 40ms) with larger hysteresis for noisy gates
-- set_input_clock(1, 1.0, 0.2, 0.04)
```
Behavior:
- Rising edge detected when signal crosses (threshold + hysteresis) after being below (threshold - hysteresis).
- First edge arms detector; BPM reported after second edge onward.
- Debounce: edges closer than min_period are ignored.
- Reports smoothed BPM (EMA 0.2) and raw period seconds.
- Suitable for audio-rate pulses converted to logic-level CV; noisy signals may require larger hysteresis.

Event Dispatch Path:
1. Core 0 per-sample / per-block detection updates mode state and pushes compact `detect_event_t` into ring (overwrite on overflow).
2. Core 1 calls `crow_detect_drain_events()` each Lua loop; events invoke Lua handlers if present.
3. Missing handlers are ignored (no allocation).

Handler Registration:
Assign Lua functions directly to `input[n].<eventname>`:

```
function init()
  input[1].stream = function(v) print("stream", v) end
  input[1].change = function(state) print("change", state) end
  input[1].scale  = function(idx, oct, note, volts)
    print("scale note", idx, oct, note, volts)
  end
  input[1].freq   = function(hz) print("freq", hz) end
end
```

Mode Configuration:
Low-level C bindings (e.g. `set_input_change`) are exposed; a higher-level `input[n].mode(...)` helper is planned but not yet provided. Call explicit setters via Lua glue if already surfaced, otherwise configuration occurs through forthcoming convenience API (see Roadmap).

Limitations / Tuning Notes:
- Only two physical inputs (`CROW_DETECT_CHANNELS=2`).
- Queue size (64) sufficient for CV; audio-rate misuse may drop events silently.
- `freq` assumes bipolar waveform; unipolar gate fallback not yet implemented (will underestimate frequency for narrow pulses).
- `volume` & `peak` semantics on very slow CV produce sparse / near-zero results (intended).
- Scale detection uses floating window + hysteresis to mitigate chatter; thresholds may need retuning if hardware noise characteristics change.

## 6. Timing Systems

| System         | Crow Original                            | Emulator Status         | Guidance |
|----------------|-------------------------------------------|-------------------------|----------|
| `metro` (8 slots) | Independent periodic callbacks        | Supported (core)        | Use for repeating events. |
| `clock.run / sleep / sync` | Coroutine-based global clock | Not Implemented / Stub  | Use `metro` or manual `output[n].action` loops. |
| `delay(fn, t [, repeats])` | Simple scheduling            | Not Implemented         | Emulate with a metro + counter. |
| `timeline`      | Loop / score / real modes                | Not Implemented         | Consider composing metros + sequins. |
| Launch quantization | Global / per timeline               | N/A                     | Not available. |

## 7. Lua Environment Differences

| Component         | Status | Notes |
|-------------------|--------|-------|
| ASL core (`to`, `loop`, etc.) | Present | Basis for actions. |
| Dynamic variables (`dyn{}`)   | Likely Partial | If unsupported operations appear, fallback is static capture. |
| Sequins (`sequins{}`)         | TBD (If integrated) | If absent, approximate via plain tables or simple arrays. |
| hotswap                 | Not Implemented | |
| `ii` bus                | Not Implemented | No external device control. |
| `public` variables      | Not Implemented | No remote host synchronization. |
| Calibration (`cal.*`)   | Not Exposed     | Hardware calibration managed internally. |
| Random (`math.random`)  | Pseudo-only     | No analog entropy injection yet (treat as standard Lua). |
| Utilities (`justvolts`, `hztovolts`, etc.) | Not Implemented | Provide manual helpers if needed inside scripts. |
| `unique_id()`           | Provided via wrapper | Returns hardware unique ID. |

(If you later expose modules, update this matrix rather than rewriting descriptive sections.)

## 8. Unsupported Features Summary

- `.query` input query event (report-to-host) not implemented.
- Output clock division mode.
- JI scale interpretation & ratio conversion utilities.
- Multi-output shared scale semantics (each output independent here).
- Just Intonation helpers (`justvolts`, `just12`).
- Clock coroutine system, timeline, delay scheduler.
- ii bus (all device interaction).
- public variable system & view telemetry.
- Calibration scripting API.
- Hot swap table.
- Advanced dynamic variable mutations (if not fully mirrored).

## 9. Migration Cheat Sheet

| Original crow Pattern | Emulator Adaptation |
|-----------------------|---------------------|
| `output[1].scale({0,4,7})` | Move melodic quantization to `output[3]` or `output[4]`. |
| `input[1].mode('change', ...)` | Poll `input[1].volts` inside a metro and implement threshold logic in Lua. |
| `output[2]:clock(1/4)` | Create a metro: `metro[1].event=function() output[2].volts = 5; output[2].volts = 0 end` with appropriate short pulse timing. |
| JI scale: `output[3].scale({1/1, 3/2}, 'ji')` | Pre-convert ratios to semitone offsets externally then apply numeric ET scale. |
| `clock.run(func)` | Use `metro` plus state machine, or an ASL loop for regular cyc patterns. |
| `public{param=...}` | Replace with normal global variable; optionally print/log for host inspection. |
| `ii.jf.play_note(...)` | Not available; stub or remove. |

### DIY Threshold Example (replace `change` mode)
```
last_state = nil
threshold = 1.5
hyst = 0.1

metro[1].time = 0.01
metro[1].event = function()
  v = input[1].volts
  local state = (v > threshold) and 1 or 0
  if state ~= last_state then
    if math.abs(v - threshold) > hyst then
      print("edge", state, v)
      last_state = state
    end
  end
end
metro[1]:start()
```

## 10. Quantization Internals (Informational)

Algorithm (Outputs 3–4):
1. Convert volts → MIDI float: `m = 60 + (volts * 12 / scaling)` (0V maps to 60).
2. Split into `octave = floor(m / mod)` and intra-octave degree.
3. Find nearest degree from configured degree list.
4. Recombine + clamp to `[0,127]`, round to uint8.
5. If note changed vs last sent, call calibrated `CVOut?MIDINote(note)`; else skip hardware write.

Implications:
- Degrees need not be sorted; ordering does not influence nearest-distance selection (distance is purely numeric).
- Non-monotonic degree sets used for pattern-like arpeggios will still pick nearest numerical degree, not preserve insertion order—document if you rely on order-based traversal (future enhancement could add index-based cycling mode).
- No microtonal retention after rounding.

Potential Future Enhancements:
- JI path: interpret ratio list by converting to fractional semitone or direct voltage before nearest-match.
- Ordered sequencer quant mode: cycle degrees by list index instead of nearest match.

## 11. Performance & Stability Notes

- Caching of last quantized MIDI note prevents redundant calibrated writes (saves SPI / DAC bandwidth / CPU).
- All quantized outputs write at block boundaries; rapid micro-changes within a 32-sample block will collapse to a single quantized step.
- For audio-rate modulation on outputs 1–2, be mindful of potential aliasing with sharp shapes (`over`, `rebound`); consider limiting shapes or adding a future smoothing flag.

## 12. Roadmap (Proposed)

| Priority | Feature | Outline |
|----------|---------|---------|
| High     | `input[n].mode()` convenience API | Unified argument parsing & defaults for all implemented modes. |
| High     | Freq unipolar enhancement | Add rising-edge period fallback & noise debounce for gates. |
| Medium   | Enable scale on outputs 1–2 (optional) | Uncalibrated numeric quantization toggle; disabled by default. |
| Medium   | JI support for `scale()` | Ratio parsing, convert to semitone offsets or direct voltage map. |
| Medium   | `.query` implementation | Lightweight host polling/event message (USB). |
| Medium   | Basic clock coroutine subset | Minimal scheduler (sleep, run) without full timeline. |
| Low      | public variables | Host sync protocol design. |
| Low      | ii bus stubs | Read-only simulation or pass-through. |
| Low      | Timeline subset | Loop mode first; integrate with clock when present. |
| Low      | Advanced dynamic var parity | Audit edge cases & match original crow semantics. |

Progress Notes:
- All non-clock input modes complete.
- Priority shifts to clock/time abstractions & convenience surface.


## 13. Known Behavioral Deviations

| Area | Deviation |
|------|-----------|
| Scale editing | Direct reassignment supported (function or table forms). |
| Mixed quant + slew | Quantization occurs after block-level slew evaluation (still matches original conceptual ordering). |
| Dynamic variables | Advanced mutation chaining may partially differ (unconfirmed edge semantics). |
| Unused inputs | Inputs 3–4 read as 0V (avoid scripts that expect 4 analog channels). |

## 14. Example Starter Script

```
function init()
  -- Simple quantized triad cycling on calibrated output 3
  degrees = {0,3,7}
  output[3].scale(degrees, 12, 1.0)

  step = 0
  metro[1].time = 0.25
  metro[1].event = function(c)
    step = (step % #degrees) + 1
    -- drive by raw volts so nearest degree snaps
    output[3].volts = (degrees[step] / 12)
  end
  metro[1]:start()

  -- Continuous LFO on output 1 without quantization
  output[1].action = loop{
      to(  4, 1.0, 'sine'),
      to( -4, 1.0, 'sine')
  }
  output[1]() -- start
end
```

## 15. Troubleshooting

| Symptom | Likely Cause | Resolution |
|---------|--------------|-----------|
| `output[1].scale(...)` does nothing | Scale restricted to outputs 3–4 | Move scale usage to 3 or 4. |
| Quantized pitch not updating quickly | Changes inside a single block | Accept ≤0.67 ms latency or enlarge voltage step. |
| Script references `ii` and errors | ii not implemented | Remove or stub those calls. |
| JI scales ignored | JI not supported yet | Convert ratios externally to semitone offsets. |

## 16. Change Control

Update this file whenever:
- A previously unsupported input mode is added.
- JI becomes available.
- New timing system (clock / timeline subset) is introduced.
- public / ii subsystems gain partial support.

## 17. Disclaimer

Do not treat this supplement as an authoritative crow specification. It is an implementation delta guide for the Workshop Computer environment. For canonical semantics consult the upstream crow documentation.

---

Revision: 0.3.2 - Clarified clock detection (external), removed duplicate table rows, synchronized version header, expanded clock usage example.
