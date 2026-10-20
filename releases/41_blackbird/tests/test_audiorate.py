"""Audio-rate output: ASL loops keep their frequency, oscillators fit the ISR
budget, and nothing on core 0 stalls the segment changes."""
from bbdbg import STAT_FIELDS


def asl_frequency(bb, freq, outputs=4, seconds=3.0):
    """Run lfo(1/freq) on `outputs` outputs; their frequency, from segment
    ends counted against core 1's own sample clock."""
    rate = bb.stats()["sample_rate_hz"]
    counter = bb.symbol("global_sample_counter")
    actions = bb.base + 4 * STAT_FIELDS.index("slope_actions")
    bb.eval(f"for i = 1, {outputs} do output[i](lfo({1 / freq!r}, 5)) end")
    bb.wait(0.5)
    a0, s0 = bb.mem.read_u32(actions), bb.mem.read_u32(counter)
    bb.wait(seconds)
    a1, s1 = bb.mem.read_u32(actions), bb.mem.read_u32(counter)
    secs = ((s1 - s0) & 0xFFFFFFFF) / rate
    return ((a1 - a0) & 0xFFFFFFFF) / secs / 2 / outputs


def test_asl_lfo_keeps_frequency_at_audio_rate(bb):
    # Regressions, all of which played audio-rate ASL flat or sharp:
    # - each segment ran ~1 sample long (the end overshoot and the wait for
    #   core 0 to start the next segment were dropped): 1kHz played at 907Hz
    # - times were Q16.16 seconds, rounded to 15us: 1kHz 0.7% flat, 4kHz 2.3% sharp
    # - overflow longer than a whole segment was dropped: 4 x 4kHz 25% flat
    for freq in (100, 1000, 4000):
        got = asl_frequency(bb, freq)
        assert abs(got / freq - 1) < 0.01, f"lfo at {freq}Hz x4 runs at {got:.1f}Hz"
        bb.reset()


def test_main_loop_has_no_gc_stalls(bb):
    # Regression: an idle GC step kept the collector cycling with nothing
    # allocating, and each cycle stalled the main loop ~1ms, which delayed ASL
    # segment changes (audio-rate ASL clicked ~10x a second)
    bb.eval("output[1](lfo(0.001, 5))")
    bb.wait(0.5)
    bb.reset_stats()
    bb.wait(3.0)
    worst = bb.stats()["loop_worst_us"]
    assert worst < 300, f"main loop stalled {worst}us"


def test_four_oscillators_fit_the_isr_budget(bb):
    # Regression: oscillate() used software float per sample (~14% of core 1
    # per voice at 24kHz); four voices saturated core 1 and the ISR overran
    # its sample period
    rate = bb.stats()["sample_rate_hz"]
    bb.eval("for i = 1, 4 do output[i](oscillate(1000, 5)) end")
    bb.wait(0.5)
    bb.reset_stats()
    load = bb.load(3.0)
    assert load["isr_max_us"] < 1e6 / rate * 0.75, load
    assert load["core1_busy_pct"] < 85, load
    assert abs(load["isr_rate_hz"] / rate - 1) < 0.01, load


def segment_cycle_lengths(bb, freq, outputs=1, captures=4):
    """lfo(1/freq) on `outputs` outputs: output 1's cycle lengths in samples,
    from core 1's capture of where each segment began, one list per capture
    (each of 31 consecutive cycles)."""
    starts = bb.symbol("g_slope_seg_starts")
    count = bb.symbol("g_slope_seg_start_count")
    bb.eval(f"for i = 1, {outputs} do output[i](lfo({1 / freq!r}, 2)) end")
    bb.wait(0.3)
    lengths = []
    for _ in range(captures):
        bb.mem.write_u32(count, 0)  # arm: the next 64 segment starts
        bb.wait(0.2)
        assert bb.mem.read_u32(count) >= 64, "segment capture didn't fill"
        seg = bb.mem.read_words(starts, 64)
        lengths.append([(seg[i + 2] - seg[i]) & 0xFFFFFFFF for i in range(0, 62, 2)])
    return lengths


def test_asl_cycles_have_no_jitter(bb):
    # Regression: each ASL segment started only when core 0 heard the previous
    # one had ended, 1-3 samples later and varying, so audio-rate ASL had
    # cycle-to-cycle jitter (heard as noise FM). With look-ahead, core 1 starts
    # each queued segment on the exact sample: whole-sample periods repeat
    # exactly, and fractional ones alternate around the ideal.
    rate = bb.stats()["sample_rate_hz"]
    for freq, outputs in ((1000, 1), (4000, 1), (4000, 4)):
        ideal = rate / freq
        lengths = sum(segment_cycle_lengths(bb, freq, outputs), [])
        assert set(lengths) == {ideal}, f"{freq}Hz x{outputs}: cycle lengths {sorted(set(lengths))}, want {ideal}"
        bb.reset()
    ideal = rate / 2500  # 9.6 samples
    for lengths in segment_cycle_lengths(bb, 2500):
        assert set(lengths) <= {9, 10}, sorted(set(lengths))
        drift = 0.0
        for n in lengths:  # each cycle starts on the sample at or after its ideal time
            drift += n - ideal
            assert abs(drift) < 1.0, (drift, lengths)


def test_short_segment_sequence_ends_and_calls_done(bb):
    # Look-ahead queues each next segment early, including the end of the
    # sequence: done must still fire once, when the last segment ends
    bb.eval("""
        output[1].volts = 0
        done_n = 0; done_us = nil
        output[1].done = function() done_n = done_n + 1; done_us = bb_dbg_us() end
    """)
    bb.wait(0.1)
    bb.eval("done_n = 0; t0 = bb_dbg_us(); output[1]{ to(5, 0.004), to(-5, 0.004), to(-2, 0), to(3, 0.004) }")
    bb.wait(0.2)
    assert bb.query("done_n") == 1
    took = bb.query("(done_us - t0) / 1000")
    assert 11 < took < 16, f"done after {took}ms, want ~12ms"
    assert abs(bb.query("output[1].volts") - 3) < 0.01


def test_held_short_segments_sustain_and_release(bb):
    bb.eval("output[1].volts = 0; output[1].action = adsr(0.003, 0.003, 2, 0.003, 'linear')")
    bb.wait(0.05)
    bb.eval("output[1](true)")
    bb.wait(0.1)
    assert abs(bb.query("output[1].volts") - 2) < 0.01, "didn't sustain at 2V"
    bb.wait(0.1)
    assert abs(bb.query("output[1].volts") - 2) < 0.01, "didn't hold the sustain"
    bb.eval("output[1](false)")
    bb.wait(0.1)
    assert abs(bb.query("output[1].volts")) < 0.01, "didn't release to 0V"


def test_restarting_a_running_lookahead_chain(bb):
    # A new ASL on a channel replaces the queued segment: the new one runs at
    # its own rate, nothing from the old chain leaks in
    bb.eval("output[1](lfo(1/3000, 2))")
    bb.wait(0.1)
    for freq in (1000, 3000, 1000):
        bb.eval(f"output[1](lfo({1 / freq!r}, 2))")
        bb.wait(0.05)
    rate = bb.stats()["sample_rate_hz"]
    lengths = sum(segment_cycle_lengths(bb, 1000), [])
    assert set(lengths) == {rate / 1000}, sorted(set(lengths))
