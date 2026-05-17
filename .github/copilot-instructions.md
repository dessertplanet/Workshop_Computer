# Copilot instructions for Workshop_Computer

## Build and validation commands

- Most firmware is built per card from its release directory with the Raspberry Pi Pico SDK available via `PICO_SDK_PATH`.
- For the active MLRws card:
  - `cd releases/15_MLRws && make mono` builds `MLRws_mono.uf2` into `releases/15_MLRws/UF2/`.
  - `cd releases/15_MLRws && make stereo` builds `MLRws_stereo.uf2`.
  - `cd releases/15_MLRws && make all` builds both variants.
  - `cd releases/15_MLRws && make clean` removes `build/` and `build-stereo/`.
  - `cd releases/15_MLRws && make flash-mono` or `make flash-stereo` builds and flashes through GDB/OpenOCD (`OPENOCD_HOST`, `OPENOCD_GDB_PORT`, and `GDB` are overridable).
- Generic Pico SDK card builds use the local card directory, for example:
  - `mkdir -p build && cd build && cmake .. -DPICO_SDK_PATH=/opt/pico-sdk -DPICO_BOARD=pico && make`
  - Some cards use Ninja: `cmake .. -G Ninja ... && ninja -j4`.
- Build a single ComputerCard example from `Demonstrations+HelloWorlds/PicoSDK/ComputerCard` by configuring once, then building the target name: `cmake --build build --target passthrough` (replace `passthrough` with another `add_example(...)` target).
- The release tree does not define a repo-wide automated test or lint command. For firmware changes, validate by building the affected card target; for MLRws, prefer both `make mono` and `make stereo` when touching shared audio/flash/grid logic.
- The static site generator is separate: `cd tools/sitegen && npm install && npm run build`.

## High-level architecture

- This repository is a collection of Music Thing Workshop Computer program cards plus examples. `releases/` contains numbered firmware cards, often self-contained and using different stacks: Pico SDK C/C++, Arduino-Pico, CircuitPython, MicroPython, Rust/Embassy, Blackbird Lua, and PlatformIO.
- `Demonstrations+HelloWorlds/PicoSDK/ComputerCard/ComputerCard.h` is the main reusable Pico SDK hardware abstraction. Cards derive from `ComputerCard`, do setup in the derived constructor, override `ProcessSample()`, then call `Run()`. `ProcessSample()` runs at 48 kHz and must complete in roughly 20 microseconds.
- ComputerCard handles ADC/mux sampling, smoothed knobs/CV, switch and jack detection, DAC audio outputs, PWM CV outputs, LEDs, EEPROM calibration, and optional normalisation probing. Jack indexes are zero-based internally even though panel labels are one-based.
- Pico SDK cards usually link `pico_stdlib` plus hardware libraries and call `pico_add_extra_outputs(...)` to produce `.uf2` files. Many cards disable USB stdio when TinyUSB or USB audio/MIDI owns the USB port.
- MLRws (`releases/15_MLRws`) is a Pico SDK C/C++ card built in mono and stereo variants from the same sources. CMake sets `CARD_NAME`, `CARD_STEREO`, and flash layout macros; the post-build step copies UF2 files into `UF2/` and fails if the binary exceeds the reserved firmware flash area.
- MLRws splits real-time audio/control from slower I/O across RP2040 cores. Core 0 runs `ComputerCard::ProcessSample()`, handles grid gestures/control state, records ADPCM samples, plays/mixes RAM ring buffers, and outputs audio. Core 1 runs USB tasks plus `mlr_io_task()`, refilling playback rings from flash and erasing/writing recording pages.
- MLRws mode is selected at startup from USB power/protocol detection:
  - `HostMLR`: USB host with a directly connected monome grid.
  - `DeviceMLR`: USB device speaking mext through a host/grid proxy.
  - `DeviceGridless`: no grid protocol detected; panel-only interaction.
  - `DeviceSampleMgr`: CDC sample manager protocol owns flash/sample transfer.
- MLRws persistent data lives on the program card flash after the firmware reserve. Each of six tracks has a header sector plus ADPCM data; scene/pattern/recall state is stored after the track area. Keep CMake flash constants, `mlr.h` layout macros, and sample-manager protocol expectations in sync.

## Codebase-specific conventions

- Prefer fixed-point integer DSP in per-sample paths. Avoid introducing floating-point work inside `ProcessSample()` unless the surrounding card already accepts that cost.
- Use `__not_in_flash_func(...)` or copy-to-RAM builds for timing-critical code that may run while flash is busy. MLRws uses `pico_set_binary_type(... copy_to_ram)` because it writes flash during operation.
- Keep USB/TinyUSB polling and other potentially long work off the audio core. Existing USB examples and MLRws use `pico_multicore` so core 1 can call `tud_task()`, `tuh_task()`, `mext_task()`, `device_mode_task()`, or flash I/O without blocking audio.
- Preserve the `COMPUTERCARD_NOIMPL` pattern when including `ComputerCard.h` from multiple translation units: define it before including the header in all but one source file.
- For MLRws mono/stereo behavior, guard variant-specific code with `#ifdef MLR_STEREO` and keep the mono path compiling. Shared structs in `mlr.h` include static size checks because flash headers must fit in one 4 KB sector.
- Treat MLRws cross-core shared state deliberately: existing code uses `volatile` fields, single-producer/single-consumer rings, and memory barriers such as `__dmb()` around mode handoffs. Do not replace these with blocking locks in audio paths.
- MLRws mext grid transport pads USB writes to 64 bytes with `0xFF` for compatibility with both modern CDC/RP2040 and older FTDI grids; preserve this behavior when changing `monome_mext.c`.
- When changing card CMake files, keep `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64` where present; several cards rely on the longer oscillator startup delay.
