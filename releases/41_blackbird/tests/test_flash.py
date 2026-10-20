"""Flash writes while audio runs. These write the card's flash (wear, and they
change the stored script), so they only run with --allow-flash. Each one puts
the card back on First.lua (^^c) afterwards."""
import time

from bbdbg import writes_flash

SCRIPT = "-- harness flash test\nharness_flash = 1\n"
USER_SCRIPT_ADDR = 0x10000000 + 0x200000 - 0x4000  # last 16KB of the 2MB flash


def core1_samples(bb):
    """Low word of global_sample_counter, which only core 1 (audio) advances."""
    return bb.mem.read_u32(bb.symbol("global_sample_counter"))


@writes_flash
def test_audio_keeps_running_through_script_upload(bb):
    rate = bb.stats()["sample_rate_hz"]
    bb.eval("output[1](lfo(0.05, 5))")
    try:
        s0, t0 = core1_samples(bb), time.monotonic()
        bb.repl("^^s")
        bb.repl(SCRIPT)
        bb.repl("^^w")  # erase + program; core 0 has interrupts off throughout
        s1, t1 = core1_samples(bb), time.monotonic()
        expected = (t1 - t0) * rate
        got = (s1 - s0) & 0xFFFFFFFF
        # If core 1 had stopped for the write, it would be short by the write time (~100ms+)
        assert got >= expected - 0.05 * rate, f"core 1 ran {got} samples in {t1 - t0:.3f}s, expected ~{expected:.0f}"
        # blackbird's ^^w saves without running (it asks for a reset), so check
        # what landed in flash: [status word][32-byte name][script]
        stored = bb.mem.read_bytes(USER_SCRIPT_ADDR, 4 + 32 + len(SCRIPT))
        status = int.from_bytes(stored[:4], "little")
        assert status & 0xF == 0xA, f"no user script magic in flash (status 0x{status:08x})"
        assert stored[36:].decode() == SCRIPT, f"flash holds {stored[36:]!r}"
    finally:
        bb.repl("^^c")


@writes_flash
def test_repeated_flash_writes(bb):
    """The old core-1 reset path leaked DMA channels and failed after ~5 writes."""
    try:
        for i in range(8):
            bb.repl("^^s")
            bb.repl(f"harness_flash = {i}\n")
            out = bb.repl("^^w")
            assert "flash write failed" not in out, out
            assert bb.stats()["heartbeat"] > 0
        assert bb.query("1") == 1
    finally:
        bb.repl("^^c")


@writes_flash
def test_core1_survives_flash_writes_under_load(bb):
    """Six saves to flash while core 1 is doing as much as it can: every ASL
    shape, quantized outputs, noise, every input detection mode, metros,
    clocks and pulse-out clocks. Core 1 must never touch flash meanwhile; if it
    does, it faults (the runner reports HardFaults) or stops counting samples.
    This is the dynamic check for what util/core1_flash_audit.py can't see,
    such as flash data reached through pointers (it found a vtable read)."""
    rate = bb.stats()["sample_rate_hz"]
    bb.eval("""
        output[1].scale({0, 2, 4, 5, 7, 9, 11})
        output[1](loop{ to(5, 0.05, 'sine'), to(-5, 0.05, 'expo'), to(2, 0.03, 'log'), to(0, 0.02, 'now') })
        output[2](loop{ to(4, 0.04, 'over'), to(-4, 0.04, 'under'), to(1, 0.02, 'rebound'), to(0, 0.01, 'linear') })
        output[3](lfo(0.03, 5, 'sine'))
        output[4](bb.noise(0.5))
        input[1].mode('change', 1.0, 0.1, 'both'); input[1].change = function() end
        input[2].mode('stream', 0.01); input[2].stream = function() end
        bbt_m = 0
        for i = 1, 4 do metro[i].event = function() bbt_m = bbt_m + 1 end; metro[i].time = 0.01 * i; metro[i]:start() end
        clock.tempo = 200
        clock.run(function() while true do clock.sync(1/8) end end)
        bb.pulseout[1]:clock(0.5)
    """)
    bb.square("cv1", 60, low=0, high=4)
    bb.triangle("cv2", 2, -5, 5)
    bb.square("pulse1", 24, duty=0.3)
    modes = ["input[2].mode('window', {-1, 1, 3}, 0.1)", "input[2].mode('scale', {0, 7}, 12, 1)",
             "input[2].mode('volume', 0.05)", "input[2].mode('peak', 0.5, 0.1)", "input[2].mode('stream', 0.01)"]
    try:
        for i in range(6):
            bb.eval(modes[i % len(modes)] + "; input[2].window = function() end; input[2].scale = function() end; "
                    "input[2].volume = function() end; input[2].peak = function() end")
            s0, t0 = core1_samples(bb), time.monotonic()
            bb.repl("^^s")
            bb.repl(f"harness_flash = {i}\n")
            bb.repl("^^w")
            s1, t1 = core1_samples(bb), time.monotonic()
            got, expected = (s1 - s0) & 0xFFFFFFFF, (t1 - t0) * rate
            assert got >= expected - 0.05 * rate, f"write {i}: core 1 ran {got} samples in {t1 - t0:.3f}s, expected ~{expected:.0f}"
        assert not bb.faults(), bb.faults()
    finally:
        bb.repl("^^c")
