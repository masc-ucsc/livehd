# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The --reader slang coverage ladder (todo/ 2s subtask E): EVERY
# inou/yosys/tests/*.v source appears here with its strongest passing tier
# (lec > verilog > lnast > error). tests/slang_compile.sh enforces the
# expectation BOTH ways, so promotions/regressions are explicit edits.
# Comments on non-lec entries say why (see inou/slang/README.md).

SLANG_LADDER = {
    "Snxn1k": "lec",
    "add": "lec",
    "add1": "verilog",  # compiles; LEC gap tracked
    "add2": "lec",
    "aldff": "error",  # non-LRM: procedural write to a net (yosys-only laxness); slang rejects per 1800
    "arith": "lec",
    "arraycells": "error",  # instance arrays / paramod shapes not lowered yet
    "assigns": "lec",
    "common_sub": "lec",
    "compare": "lec",
    "compare2": "lec",
    "consts": "lec",
    "cprop": "error",  # non-LRM: procedural write to a net (yosys-only laxness); slang rejects per 1800
    "cse_basic": "lec",
    "dce1": "lec",
    "dce2": "verilog",  # compiles; LEC gap tracked
    "dce3": "error",  # non-LRM: procedural write to a net (yosys-only laxness); slang rejects per 1800
    "expression_00002": "lec",
    "fflop": "verilog",  # compiles; LEC gap tracked
    "fixme_array": "lec",
    "fixme_async": "lec",
    "fixme_hier_test": "verilog",  # compiles; LEC gap tracked
    "fixme_latch": "error",  # non-LRM: procedural write to a net (yosys-only laxness); slang rejects per 1800
    "fixme_mem_offset": "verilog",  # capped: 46-entry array, slow memory LEC
    "fixme_multiport": "verilog",  # compiles; LEC gap tracked
    "fixme_nlatch": "error",  # non-LRM: procedural write to a net (yosys-only laxness); slang rejects per 1800
    "fixme_nocheck_implicit_en": "error",  # nested dynamic lvalue (mem element part-select)
    "fixme_noloop": "lec",
    "fixme_paramods": "error",  # instance arrays / paramod shapes not lowered yet
    "fixme_sha256": "verilog",  # compiles to verilog (1-bit-cond + bool-net fixes); LEC gap on the wide reduction
    "fixme_with_tuples": "error",  # non-LRM: procedural write to a net (yosys-only laxness); slang rejects per 1800
    "flop": "lec",
    "gates": "lec",
    "graphtest": "lec",
    "grid_hier_test": "error",  # instance arrays / paramod shapes not lowered yet
    "hierarchy": "lec",
    "issue_047": "verilog",  # compiles; LEC gap tracked
    "issue_057": "lec",
    "logic_bitwise_op_gld": "lec",
    "long_BTBsa": "error",  # typecheck kind gap on a generated netlist shape
    "long_gcd": "verilog",  # compiles; LEC gap tracked
    "long_gcd_small": "error",  # duplicate definition; slang rejects per 1800
    "long_iwls_adder": "lec",
    "long_kogg_stone_64": "verilog",  # compiles; LEC gap tracked
    "long_mem": "verilog",  # LEC-capable but memory LEC is slow on big arrays; small-array coverage rides simple_rf1/rf2/tuplish
    "long_mem3": "verilog",  # capped: slow memory LEC (see long_mem)
    "long_nocheck_iwls_square": "verilog",  # compiles; LEC gap tracked
    "long_regfile1r1w": "error",  # non-LRM: procedural write to a net (yosys-only laxness); slang rejects per 1800
    "long_regfile2r1w": "error",  # non-LRM: procedural write to a net (yosys-only laxness); slang rejects per 1800
    "long_shared_ports": "error",  # nested dynamic lvalue (mem element part-select)
    "loop_in_lg": "lec",
    "loop_in_lg2": "lec",
    "mem_reset": "error",  # non-LRM: undeclared identifiers (yosys-only laxness)
    "mem_sync_init": "verilog",  # compiles; LEC gap tracked
    "mismatch": "lec",
    "mt_basic_test": "lec",
    "multiassign": "lec",
    "mux": "lec",
    "mux2": "lec",
    "nocheck_blackboxing2": "error",  # fail-unknown-module
    "nocheck_chunk_FetchTargetQueue": "error",  # fail-unsupported-system-task
    "nocheck_cpp_api": "error",  # fail-unknown-module
    "nocheck_gcd_large": "error",  # duplicate definition; slang rejects per 1800
    "nocheck_join_fadd": "error",  # non-LRM: procedural write to a net (yosys-only laxness); slang rejects per 1800
    "nocheck_slang_foreach": "verilog",  # foreach lowers to an async-read memory; memory LEC inconclusive
    "nocheck_slang_loops": "lec",
    "not_vslogicnot": "lec",
    "null_port": "lec",
    "offset": "lec",
    "offset_input": "lec",
    "operators": "lec",
    "params": "lec",
    "params_submodule": "verilog",  # compiles; LEC gap tracked
    "pick": "lec",
    "punch": "error",  # non-LRM: undeclared identifiers (yosys-only laxness)
    "punch.gld": "error",  # illegal identifier in the auto-generated golden (unescaped dot)
    "punching": "lec",
    "punching_3": "lec",
    "random_delay": "lec",
    "reduce": "lec",
    "sample_stage1": "verilog",  # compiles; LEC gap tracked
    "satlarge": "lec",
    "satpick": "lec",
    "satsmall": "lec",
    "shift": "lec",
    "shiftx": "lec",
    "shiftx_simple": "verilog",  # compiles; LEC gap tracked
    "signs": "lec",
    "simple_add": "lec",
    "simple_flop": "lec",
    "simple_hier_test": "lec",
    "simple_rf1": "lec",
    "simple_rf2": "lec",
    "simple_weird": "lec",
    "simple_weird2": "lec",
    "srasll": "lec",
    "submodule": "lec",
    "submodule2": "lec",
    "submodule_offset": "lec",
    "test": "lec",
    "trivial": "lec",
    "trivial1": "lec",
    "trivial2": "lec",
    "trivial2a": "lec",
    "trivial3": "lec",
    "trivial_and": "lec",
    "trivial_join": "lec",
    "trivial_offset": "lec",
    "trivial_reduce": "lec",
    "tuplish": "lec",
    "unconnected": "lec",
    "wires": "lec",
}
