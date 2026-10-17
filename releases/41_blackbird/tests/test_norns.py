"""blackbird and a real norns, driven from both ends.

Plug blackbird into norns over USB and keep the SWD probe on blackbird. These
tests run Lua on norns through matron's REPL websocket (the one maiden uses)
and check the result on the card over SWD, and the other way round.

Set NORNS_HOST to norns' address (norns.local doesn't resolve inside the dev
container). If norns can't be reached, every test here skips within a couple
of seconds and the rest of the run carries on.

The tests unload whatever script is running on norns (norns.script.clear()).

Script corpus: NORNS_SCRIPTS lists scripts installed on norns to load and run,
each with optional setup Lua so it actually uses crow. Scripts that aren't
installed are skipped. Add more with BB_NORNS_SCRIPTS="path/a.lua,path/b.lua"
(relative to dust/code).
"""
import os
import re
import statistics

from bbdbg import GENERIC_STIMULUS, Skip, apply_stimulus, expected_failure, outputs_moving
from nornsws import Norns, NornsError, NornsUnavailable

NORNS_PORT = int(os.environ.get("NORNS_PORT", "5555"))
SCRIPT_SECONDS = float(os.environ.get("BB_NORNS_SECONDS", "10"))
FLOOD_SECONDS = float(os.environ.get("BB_NORNS_FLOOD_SECONDS", "5"))
LATENCY_NOTES = int(os.environ.get("BB_LATENCY_NOTES", "50"))
LATENCY_P95_MS = float(os.environ.get("BB_LATENCY_P95_MS", "10"))
LATENCY_MAX_MS = float(os.environ.get("BB_LATENCY_MAX_MS", "25"))

# path under dust/code -> Lua to run on norns after the script starts, to point it at crow
NORNS_SCRIPTS = {
    # tehn/awake: "out" option 4 = "crow out 1+2" (pitch on 1, ar() envelope action on 2)
    "awake/awake.lua": "params:set('out', 4)",
    # schollz/wobblewobble: uses crow by default; streams both inputs, drives all 4 outputs
    "wobblewobble/wobblewobble.lua": "",
    # lylepmills/buoys: crow input 1 is a clock by default. Outputs come from
    # per-buoy options, and buoys only exist on a grid, so with a grid attached
    # make four buoys, one per crow output in voltage mode.
    "buoys/buoys.lua": """
        if g and g.cols > 0 then
            for i = 1, 4 do
                local x, y = i * 2, 2
                buoys[y][x] = buoys[y][x] or Buoy:new()
                buoys[y][x].options["crow output"] = i
                buoys[y][x].options["crow output mode"] = 1
                buoys[y][x]:activate()
            end
        end
    """,
    # markwheeler/loom: "output" option 4 = "Crow Out 1+2" (pitch on 1,
    # output[2].execute() of a to() envelope). Notes normally come from a grid;
    # encoder 3 on the first page adds random ones.
    "loom/loom.lua": "params:set('output', 4); for _ = 1, 12 do enc(3, 1) end",
    # schollz/acid-test: "out_crow" 2 = yes (pitch with per-note slew for slides
    # on 1, gate on 2). With the jf output off it also sends ii.jf.mode(0) on
    # every note, so blackbird's ii stub gets steady traffic. It doesn't play
    # until started (K3), hence toggle_playing.
    "acid-test/acid-test.lua": "params:set('out_crow', 2); toggle_playing(true)",
    # vicimity-dndrks/less-concepts: voice 1 -> crow 1/2, voice 2 -> crow 3/4
    # (pitch on 1 and 3, 5ms trigger actions "{to(5,0),to(0,0.005)}" fired on 2
    # and 4). Plays from init with its default seed. init() also sends raw
    # "ii.wsyn.ar_mode(1)" strings, which go to blackbird's ii stub.
    "less-concepts/less-concepts.lua": "params:set('voice_1_crow_1', 2); params:set('voice_2_crow_2', 2)",
    # oliviercreurer/dunes: "ext_out" 3 = "crow 1+2" (pitch on 1, an ASL
    # envelope action per step on 2); its step commands also send random
    # voltages to 3 and envelopes to 4. The sequencer waits for K2/transport.
    "dunes/dunes.lua": "params:set('ext_out', 3); running = true",
}
for _p in filter(None, os.environ.get("BB_NORNS_SCRIPTS", "").split(",")):
    NORNS_SCRIPTS.setdefault(_p.strip(), "")

# path -> reason, for known issues (reported as XFAIL)
EXPECTED = {
}

NORNS_ERRORS = re.compile(r"load fail|stack traceback|attempt to |cleanup failed|### .*error", re.IGNORECASE)

_norns = None
_norns_error = None


def norns_or_skip():
    """The shared norns connection; Skip if norns is unreachable (tried once per run)."""
    global _norns, _norns_error
    if _norns is None and _norns_error is None:
        try:
            _norns = Norns(port=NORNS_PORT, timeout=2.0)
            _norns.run("return 1")
        except (NornsUnavailable, NornsError, OSError) as e:
            _norns_error = f"norns not reachable ({e}); set NORNS_HOST"
            _norns = None
    if _norns_error:
        raise Skip(_norns_error)
    return _norns


def connected_norns(bb):
    """norns, with blackbird connected to it and a clean slate on both sides."""
    n = norns_or_skip()
    if not n.query("norns.crow.connected()"):
        raise Skip("blackbird isn't connected to norns (see test_norns_detects_blackbird)")
    n.run("norns.script.clear()", timeout=10)
    n.run("norns.crow.init()")  # crow.reset() on blackbird, clear norns-side crow callbacks
    bb.wait(0.2)
    return n


def norns_errors(n, since):
    return [l for l in n.output_since(since) if NORNS_ERRORS.search(l)]


def close(a, b, tol):
    return isinstance(a, (int, float)) and abs(a - b) <= tol


# ---------------------------------------------------------------------------
# protocol
# ---------------------------------------------------------------------------
def test_norns_detects_blackbird(bb):
    n = norns_or_skip()
    assert n.query("norns.crow.connected()"), "norns doesn't see blackbird as a crow (USB plugged into norns?)"
    since = n.mark()
    n.run("crow.identity()")
    n.wait_for(r"crow identity:", since)
    n.run("crow.version()")
    n.wait_for(r"crow version:", since)


def test_norns_sets_output_volts(bb):
    n = connected_norns(bb)
    for i, v in ((1, 3.3), (2, -2.5), (3, 0.0), (4, 7.25)):
        n.run(f"crow.output[{i}].volts = {v}")
    bb.wait(0.1)
    for i, v in ((1, 3.3), (2, -2.5), (3, 0.0), (4, 7.25)):
        got = bb.query(f"output[{i}].volts")
        assert close(got, v, 0.02), f"output[{i}] = {got}, norns set {v}"


def test_norns_output_action(bb):
    n = connected_norns(bb)
    n.run("crow.output[2].action = 'lfo(0.25, 4)'; crow.output[2]()")
    values = []
    for _ in range(10):
        bb.wait(0.05)
        values.append(bb.query("output[2].volts"))
    assert max(values) - min(values) > 2, f"output[2] barely moved: {values}"


def test_input_stream_reaches_norns(bb):
    n = connected_norns(bb)
    bb.set_input("cv1", 2.0)
    n.run("bbt_v = nil; crow.input[1].stream = function(v) bbt_v = v end; crow.input[1].mode('stream', 0.05)")
    bb.wait(0.4)
    assert close(n.query("bbt_v"), 2.0, 0.05), n.query("bbt_v")
    bb.set_input("cv1", -4.0)
    bb.wait(0.4)
    assert close(n.query("bbt_v"), -4.0, 0.05), n.query("bbt_v")


def test_input_query_reaches_norns(bb):
    n = connected_norns(bb)
    bb.set_input("cv2", 1.5)
    n.run("bbt_q = nil; crow.input[2].stream = function(v) bbt_q = v end; crow.input[2].mode('none')")
    bb.wait(0.1)
    n.run("crow.input[2].query()")
    bb.wait(0.3)
    assert close(n.query("bbt_q"), 1.5, 0.05), n.query("bbt_q")


def test_input_change_reaches_norns(bb):
    n = connected_norns(bb)
    n.run("bbt_n = 0; crow.input[1].change = function(s) bbt_n = bbt_n + 1 end;"
          "crow.input[1].mode('change', 1.0, 0.1, 'both')")
    bb.wait(0.1)
    bb.square("cv1", 5, low=0, high=3)
    bb.wait(2.0)
    bb.release_input("cv1")
    got = n.query("bbt_n")
    assert 18 <= got <= 22, f"norns saw {got} changes, expected ~20"


def test_forwarded_function_arguments(bb):
    """norns assigns a function on crow; crow calls it; norns gets the arguments
    back through _c.tell and quote, with their types intact. (Only nested keys
    are forwarded this way, so it goes in a table.)"""
    n = connected_norns(bb)
    n.run('crow "bbt = {}"')
    n.run("bbt_args = nil; crow.bbt.cb = function(...) bbt_args = {...} end")
    bb.wait(0.2)
    bb.eval("bbt.cb(1, 'two', 3.5, true)")
    bb.wait(0.3)
    ok = n.query("bbt_args ~= nil and bbt_args[1] == 1 and bbt_args[2] == 'two' "
                 "and bbt_args[3] == 3.5 and bbt_args[4] == true")
    if not ok:
        got = n.run("return bbt_args and (tostring(bbt_args[1]) .. ',' .. tostring(bbt_args[2]) .. ','"
                    " .. tostring(bbt_args[3]) .. ',' .. tostring(bbt_args[4]))")
        raise AssertionError(f"norns got {got}, expected 1,two,3.5,true")


def test_output_flood_from_norns(bb):
    """The wobblewobble case: norns sending output volts as fast as it can.
    blackbird must keep up without overruns, dropped USB input or losing the
    last value."""
    n = connected_norns(bb)
    rate = 100  # per output, 4 outputs
    n.run(f"""
        bbt_flood = clock.run(function()
            local t = 0
            while true do
                t = t + 1
                for i = 1, 4 do crow.output[i].volts = ((t + i) % 100) / 20 end
                clock.sleep({1 / rate})
            end
        end)
    """)
    bb.wait(FLOOD_SECONDS)
    n.run("clock.cancel(bbt_flood)")
    n.run("for i = 1, 4 do crow.output[i].volts = i end")
    bb.wait(0.3)
    for i in range(1, 5):
        assert close(bb.query(f"output[{i}].volts"), i, 0.02), f"output[{i}] lost the final value"
    st = bb.stats()
    assert st["drops_usb_rx"] == 0, f"blackbird dropped {st['drops_usb_rx']} USB packets"
    bb.info["main loop worst"] = f"{st['loop_worst_us']}us"


# ---------------------------------------------------------------------------
# latency: timed entirely on blackbird's microsecond clock
# ---------------------------------------------------------------------------
# On blackbird, bb.asap (run every main loop iteration) watches output[1] and
# notes when each note's voltage lands. The note number is recovered from the
# voltage, (note - 60) / 12, so late or lost replies can't be mismatched.
LATENCY_PROBE = """
    lat = { sent = {}, got = {} }
    local last = output[1].volts
    bb.asap = function()
        local v = output[1].volts
        if v ~= last then
            last = v
            local note = math.floor(v * 12 + 60.5)
            local t0 = lat.sent[note]
            if t0 and not lat.got[note] then lat.got[note] = bb_dbg_us() - t0 end
        end
    end
"""

# The norns side of the MIDI path, written the way norns scripts usually are:
# find the device, connect, and set crow output 1 from note-ons.
NORNS_MIDI_HANDLER = """
    bbt_port = nil
    for i, v in pairs(midi.vports) do
        if v.name and (v.name:find("telephone") or v.name:find("crow")) then bbt_port = i end
    end
    if bbt_port then
        bbt_m = midi.connect(bbt_port)
        bbt_m.event = function(data)
            local msg = midi.to_msg(data)
            if msg.type == "note_on" then crow.output[1].volts = (msg.note - 60) / 12 end
        end
    end
    return bbt_port
"""


def measure_round_trips(bb, trigger):
    """Send LATENCY_NOTES notes with `trigger` (Lua on blackbird, with %d for the
    note) ~120ms apart, and return (latencies in ms, notes that never came back)."""
    import random
    bb.eval("output[1].volts = -5")  # a voltage no note maps to
    bb.eval(LATENCY_PROBE)
    notes = [40 + (i % 60) for i in range(LATENCY_NOTES)]  # distinct within any 60
    try:
        for note in notes:
            bb.eval(f"lat.sent[{note}] = bb_dbg_us(); lat.got[{note}] = nil; " + trigger % note)
            bb.wait(random.uniform(0.09, 0.15))  # off the USB frame grid
        bb.wait(0.5)
        out = bb.eval("for n, d in pairs(lat.got) do print(n, d) end")
    finally:
        bb.eval("bb.asap = nil")
    got = {int(n): int(d) / 1000.0 for n, d in (line.split() for line in out.splitlines() if line.strip())}
    lost = sorted(set(notes) - set(got))
    return sorted(got.values()), lost


def report_latency(bb, label, ms, lost):
    if not ms:
        raise AssertionError(f"{label}: no notes came back ({len(lost)} sent)")
    p95 = ms[min(len(ms) - 1, int(0.95 * len(ms)))]
    bb.info[label] = (f"min {ms[0]:.1f}  median {statistics.median(ms):.1f}  "
                      f"p95 {p95:.1f}  max {ms[-1]:.1f} ms  ({len(ms)} notes, {len(lost)} lost)")
    assert not lost, f"{label}: {len(lost)} of {LATENCY_NOTES} notes never reached output 1"
    assert p95 <= LATENCY_P95_MS, f"{label}: p95 {p95:.1f}ms > {LATENCY_P95_MS}ms (BB_LATENCY_P95_MS)"
    assert ms[-1] <= LATENCY_MAX_MS, f"{label}: worst {ms[-1]:.1f}ms > {LATENCY_MAX_MS}ms (BB_LATENCY_MAX_MS)"


def setup_midi_path(bb):
    n = connected_norns(bb)
    port = n.run(NORNS_MIDI_HANDLER)
    if port == "nil":
        names = n.run("local t = {} for i, v in pairs(midi.vports) do t[#t+1] = i .. '=' .. tostring(v.name) end "
                      "return table.concat(t, ', ')")
        raise AssertionError(f"norns has no MIDI port for blackbird (vports: {names})")
    return n


def teardown_midi_path(n):
    n.run("if bbt_m then bbt_m.event = nil end")


def test_latency_midi_note_to_norns_to_bb(bb):
    """The 'play a note' path: MIDI note-on from blackbird's USB MIDI port ->
    norns MIDI handler -> crow.output[1].volts -> blackbird output 1."""
    n = setup_midi_path(bb)
    try:
        ms, lost = measure_round_trips(bb, "bb.midi_note_on(%d, 100, 1)")
    finally:
        teardown_midi_path(n)
    report_latency(bb, "midi -> norns -> bb", ms, lost)


def test_latency_crow_event_to_norns_to_bb(bb):
    """Baseline without MIDI: a crow event (_c.tell over serial) -> norns ->
    crow.output[1].volts -> blackbird. Compare with the MIDI path to see which
    leg costs what."""
    n = connected_norns(bb)
    n.run('crow "bbt_lat = {}"')
    n.run("crow.bbt_lat.note = function(note) crow.output[1].volts = (note - 60) / 12 end")
    bb.wait(0.2)
    ms, lost = measure_round_trips(bb, "bbt_lat.note(%d)")
    report_latency(bb, "crow event -> norns -> bb", ms, lost)


def test_latency_midi_while_busy(bb):
    """The MIDI path again, in a busy session: LFOs on outputs 2-4 and input 1
    streaming to norns at 100Hz."""
    n = setup_midi_path(bb)
    bb.eval("for i = 2, 4 do output[i](lfo(0.2 + i * 0.1, 5)) end")
    bb.triangle("cv1", 1, -5, 5)
    n.run("bbt_s = 0; crow.input[1].stream = function(v) bbt_s = bbt_s + 1 end; crow.input[1].mode('stream', 0.01)")
    try:
        ms, lost = measure_round_trips(bb, "bb.midi_note_on(%d, 100, 1)")
    finally:
        teardown_midi_path(n)
        n.run("crow.input[1].mode('none')")
    report_latency(bb, "midi -> norns -> bb (busy)", ms, lost)


# ---------------------------------------------------------------------------
# script corpus
# ---------------------------------------------------------------------------
def run_norns_script(bb, path, setup):
    n = norns_or_skip()
    if not n.query("norns.crow.connected()"):
        raise Skip("blackbird isn't connected to norns")
    if not n.query(f"util.file_exists(_path.code .. {path!r})"):
        raise Skip(f"{path} isn't installed on norns")
    n.run("norns.script.clear()", timeout=10)
    since = n.mark()
    n.run(f"norns.script.load(_path.code .. {path!r})", timeout=20)
    try:
        m = n.wait_for(r"# script init|load fail|required", since, timeout=30)
    except NornsError:
        raise AssertionError(f"{path} didn't start within 30s")
    assert "# script init" in m.string, f"{path} didn't load: {m.string}"
    bb.wait(1.0)  # let init() finish
    if setup:
        n.run(setup)

    apply_stimulus(bb, GENERIC_STIMULUS)
    bb.info["outputs moving"] = outputs_moving(bb, SCRIPT_SECONDS) or "none"

    errors = norns_errors(n, since)
    still_connected = n.query("norns.crow.connected()")
    n.run("norns.script.clear()", timeout=10)
    assert not errors, "errors on norns:\n    " + "\n    ".join(errors)
    assert still_connected, "norns lost blackbird during the script"
    drops = {k: v for k, v in bb.stats().items() if k.startswith("drops_") and v}
    assert not drops, f"dropped events on blackbird: {drops}"


def _make_script_test(path, setup):
    def test(bb):
        run_norns_script(bb, path, setup)
    test.__name__ = "test_norns_script_" + re.sub(r"\W", "_", path[:-4] if path.endswith(".lua") else path)
    if path in EXPECTED:
        test = expected_failure(EXPECTED[path])(test)
    return test


for _path, _setup in NORNS_SCRIPTS.items():
    _t = _make_script_test(_path, _setup)
    globals()[_t.__name__] = _t
