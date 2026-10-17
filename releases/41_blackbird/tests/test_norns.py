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

from bbdbg import GENERIC_STIMULUS, Skip, apply_stimulus, expected_failure, outputs_moving
from nornsws import Norns, NornsError, NornsUnavailable

NORNS_PORT = int(os.environ.get("NORNS_PORT", "5555"))
SCRIPT_SECONDS = float(os.environ.get("BB_NORNS_SECONDS", "10"))
FLOOD_SECONDS = float(os.environ.get("BB_NORNS_FLOOD_SECONDS", "5"))

# path under dust/code -> Lua to run on norns after the script starts, to point it at crow
NORNS_SCRIPTS = {
    "awake/awake.lua": "params:set('out', 4)",          # "crow out 1+2"
    "wobblewobble/wobblewobble.lua": "",                # streams crow inputs, drives all 4 outputs
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
