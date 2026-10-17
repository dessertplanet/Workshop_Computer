"""Flash writes while audio runs. These write the card's flash (wear, and they
change the stored script), so they only run with --allow-flash. Each one puts
the card back on First.lua (^^c) afterwards."""
import time

from bbdbg import writes_flash

SCRIPT = "-- harness flash test\nharness_flash = 1\n"


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
        assert bb.query("harness_flash") == 1
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
