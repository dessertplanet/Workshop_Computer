# Druid Compatibility Plan (A–F)

## Context / Current State

Your emulator already:
- Enumerates as USB CDC with VID:PID 0483:5740 and product string "crow: telephone line" (druid auto-detect passes).
- Accepts crow-style system command prefixes `^^` (`b,s,e,w,r,p,v,i,k,c,f/F`) via `parse_command`.
- Supports volatile script upload path `^^s ... ^^e`.
- Implements flash storage for user script and First.lua (separate sectors).

Key incompatibilities with upstream crow as required by druid:
1. **Handshake / packets**: Emulator prints human text lines instead of protocol packets (druid parses only tokens beginning with `^^` and shaped like `^^event(args)`).
2. **Version / identity**: `^^v` and `^^i` produce plain lines, not `^^version('x.y.z')` and `^^identity('0xHEX')`.
3. **Ready signaling**: No `^^ready()` after boot or after script load/save (druid often expects readiness cues; at minimum consistent with crow’s style).
4. **Persistent upload**: `^^s ... ^^w` path does not work. Stream mode only ends on `^^e`; `^^w` is never recognized inside upload mode, so scripts are never auto-written to flash the way druid expects for “upload”.
5. **Script buffer lifecycle**: Buffer is reset in a way that prevents using the just-uploaded data for flash save. Flash write command path is disjoint (`handle_flash_upload_command`) and unreachable through druid’s normal `^^s ... ^^w` sequence.
6. **Telemetry / events**: No emission of `^^output(ch,volts)`, `^^stream(ch,volts)` or `^^change(ch,state)`. Druid REPL capture panes only update on `stream` or `change`.
7. **Script load readiness**: After volatile or persistent load there is no `^^ready()`.
8. **Reset semantics**: Users (and druid CLI) may send `crow.reset()` (Lua). Emulator has no crow table / reset binding; it is silently a no-op unless user defines it.
9. **Minor**: No size limit message aligned with docs (8 kB) although internal buffers exist; need consistent error `!script too long`.

## Goals (Scope A–F Only)

A–F covers minimum viable druid compatibility for interactive use and upload, excluding advanced compile-time flags, DFU bootloader and optional polish.

| ID | Goal |
|----|------|
| A  | Protocol packet handshake & formatted responses (`^^version`, `^^identity`, `^^ready`) replacing plain text. |
| B  | Unified script upload finalization recognizing `^^e` (volatile) and `^^w` (persist) inside stream. |
| C  | Robust sentinel detection (both end tokens) with size enforcement & error reporting; leave buffer intact until decision; integrate flash write into finalization. |
| D  | Emit telemetry events: `^^output(ch,volts)` on output voltage change; `^^stream(ch,volts)` periodically / on delta for inputs 1–2 (sufficient for druid UI). |
| E  | Emit `^^ready()` after: boot init (post version/identity), successful volatile script load, successful flash save, boot-time flash script load. |
| F  | Provide `crow.reset()` Lua binding to reinitialize Lua environment (and selected subsystems) then emit `^^ready()` (lightweight reset). |

(G and beyond—compile-time gating, change events, extended error packets—explicitly deferred.)

## Detailed Plan

### A. Handshake & Core Packets
- Modify `crow_send_hello()`:
  1. Send `hi` then blank line (upstream style).
  2. Immediately call version & identity emitters.
  3. Emit `^^ready()`.
- Modify `crow_print_version()` to: `send_usb_printf("^^version('%s')", FW_VERSION_STRING);`
- Modify `crow_print_identity()`:
  - Gather 48/64-bit unique ID (existing pico unique ID or `get_unique_card_id()`).
  - Format hex: `^^identity('0x%012llX')` (or longer if available; druid just parses parentheses).
- All writes retain `\n\r` line ending (current helper already appends).

### B & C. Script Upload State Machine
Current:
- `script_upload_mode` begins with `^^s`.
- Recognizes only `^^e`.
Issues:
- `^^w` ignored; no flash persistence path.

Changes:
1. In `process_usb_data()` when `script_upload_mode` is true, scan incoming chunk for both `^^e` and `^^w`.
2. Handle possibility that sentinel boundary spans reads: keep last 2 chars of previous chunk (simple rolling window).
3. On detecting sentinel:
   - Process preceding bytes into buffer.
   - Call `finalize_script_upload(persist)` with `persist = (cmd == 'w')`.
4. Enforce max size (e.g. 8192 bytes or existing `USER_SCRIPT_SIZE` if smaller). On overflow:
   - Abort mode, emit `!script too long`.
5. Remove separate `handle_flash_upload_command()` dependency—flash write happens within finalize when `persist==true`.
6. Keep script buffer intact until after optional flash write.

### D. Telemetry Packets
- Output changes: in Core 1 loop after retrieving new Lua output volts (where `volts_new` true), call `send_usb_printf("^^output(%d,%g)", ch+1, volts);`.
- Input streaming: in Core 1 loop track previous values for channels 1–2; every 100 ms OR delta >= 0.01V emit `send_usb_printf("^^stream(%d,%g)", ch+1, val);`.
- Provide simple rate limiting constants:
  - `#define DRUID_STREAM_INTERVAL_MS 100`
  - `#define DRUID_INPUT_DELTA_MIN 0.01f`
- (Optional `change` events deferred.)

### E. Ready Signaling
Emit `^^ready()`:
- After initial handshake (post version & identity).
- After `finalize_script_upload()` success (both volatile & persist).
- After boot-time flash load succeeded.
- After `crow.reset()` completes.

### F. crow.reset() Binding
1. Add C function (e.g. `static int crow_lua_reset(lua_State* L)`):
   - Deinit & re-init Lua VM (`g_crow_lua->deinit(); g_crow_lua->init();`)
   - Optionally clear slopes/events/quantization state if needed (minimal viable: Lua reset only).
   - Emit `^^ready()`.
2. Register C function before loading crow globals: `lua_register(L, "crow_reset", crow_lua_reset);`
3. In `crow_globals_lua` add:
   ```lua
   crow = {}
   function crow.reset()
       crow_reset()
   end
   ```
4. Users (and druid CLI) can invoke `crow.reset()`.

## Data Structures & Constants

Add (crow_emulator.cpp – top or near existing globals):
```c
#define MAX_DRUID_SCRIPT_SIZE 8192  // enforce crow 8kB limit
#define DRUID_STREAM_INTERVAL_MS 100
#define DRUID_INPUT_DELTA_MIN 0.01f
```

## Functions to Touch (File:Line approximate)

- crow_emulator.cpp:
  - crow_send_hello()
  - crow_print_version()
  - crow_print_identity()
  - process_usb_data() (upload mode branch)
  - end_script_upload() → refactor into finalize_script_upload(bool persist)
  - start_script_upload() (reset size counters)
  - load_flash_script_at_boot() (add ready)
  - core1_main() (add telemetry)
- crow_lua.cpp:
  - Registration section (add crow_reset)
  - crow_globals_lua string (inject crow table)
- (Optional) Add helper: `void CrowEmulator::emit_ready() { send_usb_string("^^ready()"); }`

## Error / Edge Handling

- If script too large: abort with `!script too long` and remain in idle (no partial execution).
- If flash write fails (existing API returns non-zero): emit `!flash write error`.
- On compile failure emit existing `!script compilation error` (no ready).
- Telemetry suppressed if USB disconnected (tud_cdc_connected check already inside helpers).

## Testing Sequence (Manual)

1. Start druid REPL: expect:
   ```
   hi
   (blank)
   ^^version('0.1.0')
   ^^identity('0x....')
   ^^ready()
   ```
2. Run `r test.lua`: expect running lines, then script `^^ready()`, plus future output events if script manipulates outputs.
3. Upload `u test.lua`: expect flash save confirmation text and `^^ready()`. Power cycle (or simulate) & confirm boot-time load prints version, identity, ready.
4. Observe input capture panes updating via `^^stream` messages (turn knobs / feed CV).
5. Invoke `crow.reset()`; expect new `^^ready()` quickly.

## Out-of-Scope (Deferred)
Gating macro, verbose error packets, `^^change` granularity, DFU / `^^b` bootloader, full event parity with original C implementation.

---

## Implementation Order

1. Add file (this document).
2. Implement A (handshake / version / identity / ready helper).
3. Implement B & C (upload finalization & sentinel detection).
4. Implement D (telemetry).
5. Implement E (ready emissions at load points).
6. Implement F (crow.reset binding).
7. Quick sanity compile (if build system available) & adjust.
