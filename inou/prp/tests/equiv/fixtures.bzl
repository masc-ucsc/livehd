"""The canonical HDL side of each Verilog/SystemVerilog and Pyrope pair."""

def hdl_sources():
    """Return stem -> source, rejecting ambiguous .v/.sv twins at load time."""
    sources = {}
    for source in native.glob(["tests/equiv/*.v", "tests/equiv/*.sv"], allow_empty = True):
        stem = source.split("/")[-1].rsplit(".", 1)[0]
        if stem in sources:
            fail("HDL pair %s has both .v and .sv sources" % stem)
        sources[stem] = source
    return sources

# Old target names resolve to these canonical pairs; no source is duplicated.
LEGACY_YOSYS_PAIRS = {
    "add": "basic_addition",
    "add1": "basic_addition",
    "add2": "basic_addition",
    "assigns": "basic_wiring",
    "common_sub": "basic_logic",
    "compare": "basic_logic",
    "concat_truncated_not": "basic_wiring",
    "consts": "basic_wiring",
    "dce1": "basic_logic",
    "empty_task": "basic_state",
    "fn_ret_bool": "basic_logic",
    "inc_after_nb": "inc_after_nb",
    "logic_bitwise_op_gld": "basic_logic",
    "multiassign": "basic_wiring",
    "nshift": "basic_shifts",
    "offset_input": "basic_wiring",
    "reduce": "basic_logic",
    "shift": "basic_shifts",
    "signs": "basic_state",
    "simple_add": "basic_addition",
    "trivial": "basic_logic",
    "trivial1": "basic_logic",
    "trivial_and": "basic_logic",
    "trivial_join": "basic_wiring",
    "trivial_reduce": "basic_logic",
}
