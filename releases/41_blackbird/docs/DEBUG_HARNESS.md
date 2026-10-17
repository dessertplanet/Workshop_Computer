# Debug harness

A debug build of blackbird that a host can drive over SWD while the card is
running: read performance counters, run Lua, type into the REPL as druid would,
and fake the hardware inputs. It exists for repeatable stress and coverage
tests (`tests/`), run with `util/bbdbg.py`.

Nothing here is in the release firmware: it is all behind the
`BB_DEBUG_HARNESS` CMake option.

## Quick start

You need a debug probe on the card's SWD pins, and OpenOCD running on the host
with its GDB server on port 3333 (for example `openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg`).
From the dev container, the host is `host.docker.internal`.

```sh
make flash-debug          # build build-debug/blackbird_debug.elf and load it over SWD
make test                 # run tests/ (stress tests take ~10s each)

util/bbdbg.py stats --watch 1
util/bbdbg.py eval "return input[1].volts"
util/bbdbg.py input cv1=2.5 pulse1=square:100hz switch=up
util/bbdbg.py input --clear
util/bbdbg.py repl "^^k"
util/bbdbg.py log --follow           # everything the card writes to USB
```

`make test TEST_ARGS="-k stress --repeat 5 --report results.json"` passes
arguments through. `BB_STRESS_SECONDS=60 make test` runs the stress tests for longer.
Tests that write flash only run with `--allow-flash`.

In VS Code, "Blackbird: Debug harness external OpenOCD" (cortex-debug) loads the
debug build. Its live watch can show `bb_dbg` while the card runs.

## How it works

`main.cpp` defines `bb_dbg`, a struct in RAM. OpenOCD reads and writes RAM
through the debug port without halting the cores (`read_memory` /
`write_memory`), so the card keeps running normally while the host talks to it.

- **Stats.** Core 0 refreshes them every main loop iteration: worst-case
  and overrun counts for `ProcessSample`, main loop worst case, Lua memory and
  its peak, and drop counts for every event queue.
- **Commands.** The host writes `script`, `script_len` and `cmd`, then
  increments `cmd_seq`. Core 0 runs the command in the main loop, on the same
  thread as Lua, and sets `ack_seq` when it is done.
  - `EVAL` runs `script` as a Lua chunk. `print()` output and returned values
    go to `out`. Errors go to `out`, with a non-zero `status`.
  - `REPL` feeds `script` into the USB receive queue in 64-byte packets, so the
    REPL handles it exactly as it would bytes from druid. That includes `^^`
    commands, multi-line blocks and script uploads. Send `^^s`/`^^e`/`^^w` in
    their own command, as druid sends them in their own packet.
  - `RESET_STATS` zeroes the worst-case and overrun counters.
- **Log.** Everything written to USB CDC is mirrored into the `log` ring (the
  link wraps `tud_cdc_n_write`). `print()` from callbacks only reaches the log
  when the card's USB is connected to a host, because blackbird skips printing
  otherwise. Errors always reach it.
- **Inputs.** `bb_dbg.inputs` is passed to `ComputerCard::SetInputOverrides`.
  At the end of input processing each sample, `BufferFull` replaces overridden
  audio, CV, pulse, knob and switch values. Edge detection, `Connected()`,
  input detection and `ProcessSample` all then see the fake values. Each input
  can be held constant, or driven by a square or triangle generator that runs
  on the card, so pulse trains and sweeps are sample-accurate. A jack with an
  override reports as connected unless `set_connected` says otherwise.

The struct layout is duplicated in `util/bbdbg.py`. `static_assert`s in
`main.cpp` pin the offsets, and the tool checks `version` and `struct_size` when
it connects. If you add fields, append stats into `reserved` and bump
`BB_DBG_VERSION`.

## Writing tests

Tests are `tests/test_*.py` functions that take a `bb` argument (see
`Blackbird` in `util/bbdbg.py`). No pytest is needed. Before each test the runner
clears input overrides, sends `^^k` and resets the counters. After each test it
fails the test if:

- `ProcessSample` overran (set `bb.allow_overruns` to allow some)
- the main loop heartbeat stopped
- the USB log contains Lua errors or a crash (set `bb.allow_log_errors`)

```python
def test_metro_survives_input_storm(bb):
    bb.eval("n = 0; metro[1].event = function() n = n + 1 end; metro[1].time = 0.01; metro[1]:start()")
    bb.square("cv1", 300, low=0, high=3)
    bb.wait(5)
    assert bb.query("n") >= 450
```

Mark tests that write flash with `@writes_flash` (from `bbdbg`). Mark known
issues with `@expected_failure("why")`; they report as XFAIL instead of failing
the run, and as XPASS once fixed. To skip a test, raise `bbdbg.Skip("why")`
from inside it.

## Script compatibility: bbbowery and bowery

`make test-bowery` runs every script in `bbbowery/` and
[monome/bowery](https://github.com/monome/bowery) on the card. That covers
bowery's top-level scripts and `snippets/`; set `BB_BOWERY_LEGACY=1` to add
`legacy/`. Bowery is fetched at a pinned commit (`BOWERY_REF` in
`tests/test_bowery.py`) into `tests/.cache/`. Set `BB_BOWERY_DIR` to use your
own checkout.

Each script is uploaded the way druid's `r` does (`^^s`, the script, `^^e`).
It then runs for `BB_BOWERY_SECONDS` (default 5) with generic fake inputs:

- clocks on CV 1 and both pulse inputs
- a sweep on CV 2
- knobs part-way

It passes if it loads and runs without Lua errors, overruns, a stalled main
loop or dropped events. Which outputs moved is reported, not asserted.

Known problems go in `EXPECTED` (XFAIL) or `SKIP` at the top of
`tests/test_bowery.py`, with the reason. If a script needs particular input to
do anything, add it to `STIMULUS_FOR`. `--report results.json` saves the
whole compatibility table.

## Caveats

- **Halting.** OpenOCD may halt the target when a GDB client attaches.
  `bbdbg.py` resumes it at once, but while it is halted the ADC keeps
  sampling. The FIFO can overflow, which leaves the input mux misaligned until
  reset. To avoid the halt, either:
  - use the Tcl server: `--transport tcl` (port 6666)
  - start OpenOCD with `-c "rp2040.core0 configure -event gdb-attach {}"`

  Don't set breakpoints during a timing test.
- **RAM.** The harness takes about 16 KB of RAM (script, output and log
  buffers), so the debug build has less Lua heap than release. Memory-limit
  tests are slightly pessimistic.
- **Timing.** The debug build uses the same optimisation as release, so timings
  are representative. Polling the mailbox and refreshing stats adds a few
  microseconds per main loop iteration on core 0. It adds nothing to core 1:
  when nothing is overridden, the input override hook is a single pointer
  check.
