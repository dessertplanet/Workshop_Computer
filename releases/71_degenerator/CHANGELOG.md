# Changelog

## v1.1 — Web Manager + Flash Slots + Degradation Fixes

### New features

- **Web-based loop manager** (`web/degenerator_manager.html`): single-page HTML app using WebUSB/Picoboot to upload and download loops from the browser. Upload WAV/MP3 files (resampled to 48 kHz, u-law encoded matching firmware tables), set trim points, rename slots, and flash to up to 4 slots. Download loops as 16-bit WAV.
- **Flash slot save/load**: persist up to 4 loops in RP2040 flash via STORE_SLOT and SELECT_SLOT modes. Slots survive power cycles.
  - `STORE_SLOT`: Z Down with Big Knob near zero → select slot 0-3 → Z Down writes
  - `SELECT_SLOT`: Z Down at boot, or Pulse In 2 while Z Down → select slot 0-3 → release Z loads
  - Multicore-safe flash writes using atomic handoff + `multicore_lockout` to safely pause core 1
- **Auto-reset to BOOTSEL**: web manager can send 1200-baud reset signal via WebSerial to enter flash mode without holding the BOOTSEL button

### Bug fixes

- **Oxide shedding**: `damageLevel` was not accumulating, causing dropouts to never appear. Fixed accumulation logic so oxide patches grow progressively over time.
- **Knob reference tracking**: entering MIX or DEGRADE now records the Big Knob's starting position. The knob must be turned past this reference before the effect engages, preventing accidental loud overdubs or rapid degradation on mode entry.

### Performance & safety

- `__not_in_flash_func` on u-law encode/decode, RNG, and flash write path for deterministic timing
- Input validation helpers (`validate_knob_value`, `validate_cv_value`, `clamp_audio12`) for runtime bounds checking
- Atomic cross-core handoff flags for flash save/load prevent concurrent access
- Core 1 mutes during flash writes; core 0 disables interrupts during erase/program

### Documentation

- Added `TESTING.md` — 424-line hardware test plan covering every effect, integration scenarios, bypass behavior, and common failure patterns
- Updated `README.md` with flash slot modes, STORE_SLOT/SELECT_SLOT instructions
- Updated `AGENTS.md` with flash layout, multicore lockout details, u-law round-trip

### Infrastructure

- `info.yaml`: version 1.1, added editor URL pointing to the web manager
- `web/lib/picoflash/`: full Picoboot protocol library (command, connection, UF2, picoboot layers)
