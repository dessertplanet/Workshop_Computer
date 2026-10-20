"""Recovering from a script that ran the Lua heap out.

Regression: ^^k and uploads cleared the old script by compiling Lua
("crow.reset()", a loop over _user), which fails with the heap full. A script
that held on to all the memory could then only be cleared by a power cycle."""
from bbdbg import HarnessError


def fill_heap(bb):
    """Fill the heap from a script, keeping it all referenced (a global)."""
    try:
        bb.eval("chain = nil for k = 1, 1000000 do chain = {chain, k} end")
    except HarnessError as e:
        assert "not enough memory" in str(e), e
    else:
        raise AssertionError("the heap didn't fill")
    try:
        bb.query("1 + 1")
    except HarnessError as e:
        assert "not enough memory" in str(e), e  # can't even compile a line
    else:
        raise AssertionError("the heap isn't full")


def test_kill_recovers_a_full_heap(bb):
    fill_heap(bb)
    bb.kill_lua()
    bb.wait(0.2)
    assert bb.query("chain == nil")
    assert bb.query("type(init)") == "function"
    assert bb.stats()["lua_mem_bytes"] < 100 * 1024


def test_upload_recovers_a_full_heap(bb):
    fill_heap(bb)
    out = bb.upload("recovered = 7\nfunction init() init_ran = true end\n")
    assert "^^ready()" in out, out
    assert bb.query("recovered") == 7
    assert bb.query("init_ran") is True
    assert bb.query("chain == nil")
