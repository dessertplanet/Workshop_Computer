# Blackbird 1.1 (draft) — Release Notes

This document summarizes notable changes in the `blackbird-1.1-dev` line compared to `main`.

## Highlights

- More robust scheduling under load via expanded lock-free event queues and event coalescing.
- Timing work: clock math moved to a unified monotonic ms timebase and fixed-point beat tracking.
- Output pipeline work: slope generation and related DSP paths reworked toward fixed-point and buffered rendering.
- Reset hardening: `crow.reset()` cleans up more C- and Lua-side state to avoid lingering coroutines/events.
- Build/output: UF2 post-processing step adds erase blocks to support user script storage behavior.

## Notable Changes

### Timing / Clock

- `clock` reference tracking moved from floating-point seconds/beats to:
  - monotonic milliseconds since boot, and
  - Q16.16 fixed-point beat representation.
- Scheduling comparisons in the clock update loop changed from `<` to `<=` for wakeups.
- `clock.sync()` scheduling logic uses the current beat position modulo the requested period, rather than the older reference-based floor/step approach.
- Added clock scheduling statistics (success/failure/max active/pool capacity) for diagnostics.

### Eventing / Concurrency

- Legacy `lib/events.*` removed from the build.
- `lib/events_lockfree.*` expanded:
  - queues now include metro, input, clock-resume, and ASL-done events.
  - queue size increased (default `LOCKFREE_QUEUE_SIZE` is 128).
  - overflow behavior includes best-effort **coalescing** (overwriting a pending event for the same metro/coroutine) instead of only dropping.
  - added queue clear/reset and per-queue stats accessors.

### Slopes / Output

- Slopes moved toward Q16.16 fixed-point state and new buffer/render helpers.
- Added infrastructure to reduce cross-core races (capturing callback pointers, queueing work from Core 1 → Core 0).

### Detection

- Detection timing is now derived from `PROCESS_SAMPLE_RATE_HZ`.
- Stream detection uses a sample-accurate countdown in the ISR path; some block-based constants were adjusted.

### Build / Tooling

- Added `-fsingle-precision-constant` for better RP2040 performance and to reduce accidental double math.
- Post-build UF2 step runs `util/add_uf2_erase_block.py` via `python3`.

## Breaking Changes

- `bb.priority()` was removed.
  - On `main`, `bb.priority()` existed and controlled timer block sizing; on this line `TIMER_BLOCK_SIZE` is fixed (8-sample blocks).
  - Any scripts that call `bb.priority(...)` will error and must be updated.

## Compatibility Notes

- `public.lua`: parameter actions are not auto-executed on add/assignment (aligns with crow expectations).
- `clock.lua`: cleanup behavior avoids mutating the coroutine table during iteration and performs a hard reset of internal bookkeeping.

## Known Risks / Potential Regressions to Watch

These are areas where behavior can change subtly even if nothing “crashes”:

- **Clock sync phase/rounding:** ms rounding + fixed-point conversion may shift phase for some `clock.sync()` / `clock.sync(N)` patterns, especially with fractional values.
- **Overload behavior:** event coalescing means intermediate metro stages / clock resume events may be merged under load.
- **Detection cadence:** stream and block constants changed; scripts that depended on older callback cadence may observe different timing.
- **HardFault behavior:** faults may break into a debugger-friendly BKPT loop rather than attempting to continue.

## Suggested Verification (practical)

- Stress clocks/metros:
  - run a high-tempo Lua clock test (e.g. rapid `clock.sync(1)` loops) and verify no missed/hung coroutines.
- Stress events:
  - enable multiple metros + input detection while driving fast pulse inputs; watch for stuck callbacks.
- Verify reset:
  - run a script that starts multiple clocks/metros, call `crow.reset()`, confirm no “resume cancelled clock” / phantom callbacks.
- UF2 flashing:
  - confirm produced `UF2/blackbird.uf2` flashes and user-script storage behaves as expected.

## References (diff touchpoints)

- `lib/sample_rate.h`
- `lib/clock.c`, `lib/clock.h`, `lib/lib-lua/clock.lua`
- `lib/events_lockfree.c`, `lib/events_lockfree.h`
- `lib/slopes.c`, `lib/slopes.h`
- `lib/detect.c`
- `lib/l_crowlib.c`, `lib/lib-lua/public.lua`
- `CMakeLists.txt`, `util/add_uf2_erase_block.py`
