# Norns Crow Compatibility Plan (RP2040 Crow Emulator)

Goal: Allow unmodified norns scripts (using `norns.crow` / `crow` Lua APIs) to interact with this RP2040-based crow emulator exactly like hardware crow, over the existing USB text protocol.

---

## 1. Constraints (RP2040 Specific)

- MCU: RP2040 (2 cores, 264 KB SRAM). Avoid large dynamic allocations; prefer static buffers.
- Existing MAX_SCRIPT_SIZE = 8192 bytes (leave configurable but enforce hard cap).
- USB CDC line-based transport already present (uses simple newline framing).
- Timing: Avoid heavy per-step Lua overhead (event emission batching optional but not required initially).
- No filesystem writes except persistent flash script storage already implemented (flash upload path).

---

## 2. Communication Protocol (Reference From Norns Core)

Host (norns) sends:
- `^^s` start script upload
- (script lines)
- `^^e` execute (ephemeral) OR `^^w` write (persist then execute)
- `^^v` version
- `^^i` identity
- `^^k` kill lua (reset VM)
- `^^c` flash clear
- Arbitrary raw Lua lines (e.g. `crow.reset()`)

Device (crow emulator) must emit lines of form:
```
^^ready()
^^identity(<string>)
^^version(<string>)
^^pub(name,val[,typeAnnotation])
^^pupdate(name,val[,subIndex])
^^pubview(domain,channel,value)
^^change(...)
```
Plus dynamic events registered by `norns.crow.register_event()` (single uppercase/lowercase letter codes) and any `tell('event', ...)` user script emissions.

---

## 3. Required Additions / Gaps

| Area | Current | Needed |
|------|---------|--------|
| Event emission primitive | None | `tell(event, ...)` Lua function |
| Argument serialization | None | Lightweight quote function (subset of norns `quote.lua`) |
| Public parameter API | Missing | `public.add`, `public.update`, `public.discover`, `public.view.*` |
| Ready handshake | Only on manual `crow_reset()` path | Emit `^^ready()` automatically after VM init and after `crow.reset()` |
| Script upload finalize | Present | Ensure emits `^^ready()` after executing script (match crow real behavior) |
| Dynamic events | Not implemented | Expose `tell()` so scripts can synthesize events |
| Version / identity strings | Basic or missing | Return stable crow-like versions (e.g. identity 'crow', semantic version read from build) |
| Kill (^^k) | If not mapped | Map to: deinit Lua VM, re-init, emit ready |
| Flash clear (^^c) | Present? (verify) | Ensure emits status lines or at least a success message |
| Error reporting | Only prints to USB | Optionally wrap in `^^error(message)` (future) |
| Public freeze support | Not required now | Defer (norns uses freeze by injecting code + persistent upload) |

---

## 4. Lua Layer Additions (crow_globals_lua Patch)

Append (or inject) after existing globals:

```lua
-- Minimal quote (numbers, strings, tables with numeric/string keys)
local function _q(v)
  local t = type(v)
  if t == 'number' then return string.format('%.6g', v)
  elseif t == 'string' then return string.format('%q', v)
  elseif t ~= 'table' then return tostring(v)
  else
    local parts = {}
    for k,val in pairs(v) do
      local key
      if type(k) == 'number' then key = string.format('[%g]', k) else key = string.format('[%q]', k) end
      parts[#parts+1] = key .. '=' .. _q(val)
    end
    return '{' .. table.concat(parts, ',') .. '}'
  end
end

function tell(ev, ...)
  local n = select('#', ...)
  local args = ''
  if n > 0 then
    local tmp = {}
    for i = 1, n do
      tmp[i] = _q(select(i, ...))
    end
    args = table.concat(tmp, ',')
  end
  -- C side intercepts this print OR we can directly call a C binding (preferred)
  print('^^' .. ev .. '(' .. args .. ')')
end

-- Public parameter subsystem
public = {
  _defs = {},  -- name -> {val=, typ=table(optional)}
  _order = {}
}

function public.add(name, val, typ)
  if not public._defs[name] then
    public._order[#public._order+1] = name
  end
  public._defs[name] = {val = val, typ = typ or {}}
  tell('pub', name, val, public._defs[name].typ)
end

function public.update(name, val, sub)
  local d = public._defs[name]
  if not d then return end
  d.val = val
  if sub then
    tell('pupdate', name, sub, val)
  else
    tell('pupdate', name, val)
  end
end

function public.view_input(ch, v)
  tell('pubview', 'input', ch, v)
end

function public.view_output(ch, v)
  tell('pubview', 'output', ch, v)
end

function public.discover()
  for _,name in ipairs(public._order) do
    local d = public._defs[name]
    tell('pub', name, d.val, d.typ)
  end
  tell('pub', '_end')
end
```

At end of initialization sequence add:
```lua
tell('ready')
```

`crow.reset()` should:
1. Rebuild globals (re-run crow globals chunk).
2. Emit `^^ready()` (via `tell('ready')`).
3. Optionally auto-run persistent script if present (already handled elsewhere).

---

## 5. C++ Layer Changes

1. Add utility (in `crow_emulator.cpp`):
```cpp
void CrowEmulator::send_event(const char* ev, const char* payload) {
    if (payload) {
        send_usb_printf("^^%s(%s)\n", ev, payload);
    } else {
        send_usb_printf("^^%s()\n", ev);
    }
}
```
(Keep payload formatting inside Lua to avoid duplication; small overhead acceptable.)

2. (Optional optimization) Expose C binding:
```cpp
static int crow_lua_c_tell(lua_State* L) {
    int n = lua_gettop(L);
    if (n < 1) return 0;
    const char* ev = luaL_checkstring(L, 1);
    // Concatenate pre-serialized args (Lua already serialized)
    // But since we serialize in Lua, crow_lua_c_tell becomes unnecessary unless we move serialization into C.
    return 0;
}
```
Decision: Use pure-Lua serialization first (simpler, no GC spikes expected given low event rate).

3. Ensure command parser mapping covers:
```
^^s - start upload
^^e - end execute
^^w - end execute persistent
^^v - version (emit ^^version("x.y.z"))
^^i - identity (emit ^^identity("crow"))
^^k - kill lua (CrowLua::deinit(); CrowLua::init(); emit ready)
^^c - flashclear (after clearing, emit a print/log and optionally ^^ready())
```

4. After successful `finalize_script_upload()` always call `tell('ready')` (mirrors real crow's ready after (re)load).

5. Memory: Keep quote + public tables in single Lua chunk (no dynamic code generation per event).

---

## 6. API Surface Mapping (Differences to Close)

| Feature | Real Crow | Current Emulator | Action |
|---------|-----------|------------------|--------|
| ready event | always on boot/reset/load | only manual reset sometimes | Emit automatically |
| tell() arbitrary events | yes | no | Implement |
| public param system | yes | no | Implement Lua public subsystem |
| public.discover() cycle | yes | no | Provide function |
| CASL / ASL / slopes | yes (various) | partial implemented | Keep; add example w/ public |
| input change/clock bridging | yes | detection system present | Wire sample script to emit public.view if desired (optional) |
| script upload handshake | yes | present | Verify emits ready at end |
| identity/version formatting | "crow", semantic version | placeholder | Add fixed strings |

---

## 7. Verification Strategy

### 7.1 Unit (C++ Logic)
- Inject command strings into parser: assert state transitions + expected USB output lines (mock sending function).
- Confirm script longer than MAX_SCRIPT_SIZE rejected gracefully.

### 7.2 Lua Runtime
- Load snippet that registers:
  ```
  public.add("gain", 0.5, {0,1,"float"})
  public.add("mode", {"a","b","c"}, {"option","a","b","c"})
  public.update("gain", 0.75)
  tell("custom", 42, {x=1})
  ```
- Capture outbound lines; assert pattern matches.

### 7.3 Host Simulation
- Small host harness (desktop Python or C++ test) feeding command sequence:
  - version, identity
  - upload test script
  - read until ^^pub(_end)
  - send Lua line invoking public.update
  - verify ^^pupdate arrives.

### 7.4 Behavioral
- Run existing test scripts still function (no regression).
- Add `test_public.lua` with public params and periodic emit.

### 7.5 Stress
- Loop 200 rapid `tell()` calls; verify no truncated lines, no heap explosion (monitor Lua memory via existing memory usage API).

---

## 8. Implementation Order (Incremental)

1. Patch `crow_globals_lua` (append quote + tell + public + ready emit).
2. Adjust `crow.reset()` path to re-run global init (currently deinit/init from C++ okay) and not duplicate memory.
3. Ensure finalize script upload emits `tell('ready')`.
4. Add version/identity handlers if missing (C++ wrappers calling send_usb_string).
5. Write `test_public.lua`.
6. Build host harness (optional for embedded, but useful off-target).
7. Update docs.

---

## 9. Rollback / Safety

If public system causes overhead:
- Wrap initialization under `#define ENABLE_NORNS_COMPAT 1`.
- Provide runtime flag (compile-time is enough for early stage).

---

## 10. Memory / Performance Considerations

- Quote function is recursive but shallow; typical crow events have small tables.
- Avoid retaining large tables in `public._defs` beyond user needs (bounded by script).
- No dynamic code generation per event (only string assembly once per emission).

---

## 11. Acceptance Criteria

All of:
- Boot logs include `^^ready()`.
- After script upload: `^^ready()` appears once.
- Running sample script yields correct `^^pub` sequence finishing with `_end`.
- `public.update()` triggers `^^pupdate`.
- `crow.reset()` yields `^^ready()` and preserves public definitions only if script redefines them (match real crow reset semantics—fresh VM).
- Norns host (unmodified) processes lines without errors (manual test).

---

## 12. Future Enhancements (Deferred)

- `^^error(...)` standardized error emission with stack traces.
- Batching multiple events in a single USB frame for throughput.
- Binary framing option (not needed for norns).
- Public freeze support (mirror `freezescript` process).

---

## 13. Work Checklist Mapping

- [x] Map API differences (see sections 3 & 6)
- [x] Verification strategy (section 7)
- [x] Execution / implementation plan (sections 8 & 11)

---

## 14. Next Steps (Implementation Phase)

Proceed to:
1. Modify `crow_lua.cpp` adding new Lua chunk section.
2. Rebuild and run on hardware; observe USB console.
3. Iterate until all acceptance criteria satisfied.
