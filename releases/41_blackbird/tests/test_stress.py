"""Load tests. The runner fails any test with ProcessSample overruns, a stalled
main loop or Lua errors in the USB output; these add their own checks on top.
Set BB_STRESS_SECONDS to run longer (default 10)."""
import os

SECONDS = float(os.environ.get("BB_STRESS_SECONDS", "10"))


def assert_no_drops(bb):
    st = bb.stats()
    drops = {k: v for k, v in st.items() if k.startswith("drops_") and v}
    assert not drops, f"dropped events: {drops}"


def test_everything_at_once(bb):
    """LFOs on all outputs, 8 fast metros, a clock coroutine, CV change
    detection and a pulse clock, all at once."""
    bb.eval("""
        for i = 1, 4 do output[i](lfo(0.01 + i * 0.003, 5)) end
        stress_ticks = 0
        for i = 1, 8 do
            metro[i].event = function() stress_ticks = stress_ticks + 1 end
            metro[i].time = 0.005 * i
            metro[i]:start()
        end
        clock.tempo = 240
        stress_beats = 0
        clock.run(function() while true do clock.sync(1/4); stress_beats = stress_beats + 1 end end)
        stress_edges = 0
        input[1].mode('change', 1.0, 0.1, 'both')
        input[1].change = function() stress_edges = stress_edges + 1 end
    """)
    bb.square("cv1", 100, low=0, high=3)
    bb.triangle("cv2", 0.5, -5, 5)
    bb.square("pulse1", 48, duty=0.1)
    bb.wait(SECONDS)
    ticks, beats, edges = (bb.query(v) for v in ("stress_ticks", "stress_beats", "stress_edges"))
    # 8 metros at 5..40ms: ~543 ticks/s; 240bpm in 1/4 beats: 16/s; 100Hz square, both edges: 200/s
    assert ticks >= 0.9 * 543 * SECONDS, f"metro ticks {ticks}"
    assert beats >= 0.9 * 16 * SECONDS, f"clock beats {beats}"
    assert edges >= 0.95 * 200 * SECONDS, f"input edges {edges}"
    assert_no_drops(bb)


def test_fast_input_edges(bb):
    """CV change detection near its limit, with Lua doing work per edge."""
    bb.eval("""
        edge_n = 0
        input[1].mode('change', 1.0, 0.1, 'both')
        input[1].change = function(s) edge_n = edge_n + 1; output[1].volts = s and 5 or 0 end
    """)
    bb.square("cv1", 500, low=0, high=3)
    bb.wait(SECONDS)
    n = bb.query("edge_n")
    assert n >= 0.9 * 1000 * SECONDS, n
    assert_no_drops(bb)


def test_lua_memory_is_stable(bb):
    """Allocate garbage from a fast metro; Lua memory must level off."""
    bb.eval("""
        metro[1].event = function()
            local t = {}
            for i = 1, 50 do t[i] = { i, tostring(i) } end
        end
        metro[1].time = 0.002
        metro[1]:start()
    """)
    bb.wait(SECONDS / 2)
    first_half_peak = bb.stats()["lua_mem_peak_bytes"]
    bb.wait(SECONDS / 2)
    peak = bb.stats()["lua_mem_peak_bytes"]
    assert peak <= first_half_peak * 1.25, f"Lua memory still growing: {first_half_peak} -> {peak}"
