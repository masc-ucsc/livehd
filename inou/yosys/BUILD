# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_shell//shell:sh_library.bzl", "sh_library")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "lgyosys",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "*test*.cpp",
        ],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS + ["-Wno-shadow"],
    visibility = ["//visibility:public"],
    deps = [
        "//graph",
        "//inou/cgen:inou_cgen",
        "//pass/common:pass",
        "@at_clifford_yosys2//:kernel",
        "@at_clifford_yosys2//:version",
        "@mustache//:headers",
    ],
    alwayslink = True,
)

sh_library(
    name = "scripts",
    srcs = [
        "lgcheck",
        ":inou_yosys_scripts",
        ":yosys2",
    ],
    data = [
        "clock_domains.py",
        "power_on.py",
        "rtlil_children.py",
        "rtlil_split_port_adapter.py",
        "//ware:cgen_memory_rtl",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "inou_yosys_scripts",
    srcs = glob([
        "inou_yosys_*.ys",
    ]),
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "yosys2",
    srcs = [],
    copts = COPTS + ["-Wno-shadow"],
    features = ["-use_header_modules"],
    linkopts = [
        "-rdynamic",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":lgyosys",
        "@at_clifford_yosys2//:driver",
    ],
)

VERILOG_TESTS = glob(
    [
        "tests/*.v",
    ],
    exclude = [
        "tests/fixme*.v",
        "tests/long*.v",
        "tests/nocheck*.v",
    ],
)

ALL_VERILOG_TESTS = glob(["tests/*.v"])

filegroup(
    name = "verilog_tests",
    srcs = VERILOG_TESTS,
    visibility = ["//visibility:public"],
)

_YOSYS_TEST_PREFIX = "tests/"

_YOSYS_TEST_SUFFIX = ".v"

# Fixtures that stay in verilog_tests (the slang ladder still compiles each one)
# but get no yosys_compile target of their own: each is a strict sub-case of a
# kept yosys_compile fixture, or only regresses the native slang reader.
_YOSYS_COMPILE_SKIP = [
    # Identical operator cells to a kept, richer fixture. Only shift: the slang
    # ladder keeps its slang_compile twin, while trivial_and/compare/reduce/
    # simple_add/trivial keep their yosys_compile target because the slang
    # ladder dropped theirs.
    "tests/shift.v",
    # Regressions for the native slang reader; yosys elaborates them away.
    "tests/fn_ret_bool.v",
    "tests/generate_reverse_tree.v",
    "tests/hier_generate_slice.v",
    "tests/empty_task.v",
    "tests/inc_after_nb.v",
    # inou_yosys_read.ys flattens the hierarchy, leaving a subset of a kept case.
    "tests/submodule.v",
    "tests/submodule2.v",
    "tests/params_submodule.v",
    "tests/trivial2.v",
    # Weaker member of a pair of overlapping small cases.
    "tests/simple_flop.v",
    "tests/trivial_offset.v",
    "tests/simple_weird2.v",
]

YOSYS_COMPILE_TESTS = [
    (
        t[len(_YOSYS_TEST_PREFIX):-len(_YOSYS_TEST_SUFFIX)],
        t,
    )
    for t in VERILOG_TESTS
    if t not in _YOSYS_COMPILE_SKIP
]

[sh_test(
    name = "yosys_compile-%s" % t[0],
    srcs = ["tests/yosys_compile.sh"],
    args = ["$(location %s)" % t[1]],
    data = [
        t[1],
        "//inou/prp:tests/lec.py",
        "//lhd",
    ],
    tags = [
    ],
    deps = [
        ":scripts",
    ],
) for t in YOSYS_COMPILE_TESTS]

exports_files(ALL_VERILOG_TESTS)

filegroup(
    name = "memory_import_srcs",
    srcs = [
        "inou_yosys_api.cpp",
        "lgyosys_tolg.cpp",
    ],
    visibility = ["//visibility:public"],
)

# State-name recovery and budget scheduling belong to the external solver;
# the lhd CLI equivalence test covers the default CVC5 driver separately.
sh_test(
    name = "lgcheck_state_test",
    srcs = ["tests/yosys_state_match_test.sh"],
    data = [":scripts"],
)

sh_test(
    name = "lgcheck_memory_poweron_smoke",
    srcs = ["tests/yosys_memory_poweron_smoke.sh"],
    data = [":scripts"],
)

sh_test(
    name = "lgcheck_input_adapter_smoke",
    srcs = ["tests/yosys_input_adapter_smoke.sh"],
    data = [":scripts"],
)

# A multi-chunk SIGNED operand is assembled from finite selections and signed
# ONCE, on the completed result. Signing the top chunk too made a 1-bit chunk
# the 1-bit signed {-1, 0}; mixed with unsigned 1-bit values in a selection the
# union [-1..1] needs two SIGNED bits, which concat_lane_violation then refused
# for driving a 1-bit lane (CORE-ET minion_dcache_top). The width inference and
# the refusal are both correct and stay as they are; a single-chunk signed read
# keeps its sign (tests/issue_047.v).
sh_test(
    name = "yosys_concat_lane_sign_test",
    srcs = ["tests/yosys_concat_lane_sign_test.sh"],
    data = ["//lhd"],
    deps = [":scripts"],
)

# A case that lists every selector value has no unmatched path. read_slang --no-proc leaves the block's "hold
# the previous value" start in front of it and proc_mux wires that into the dead path, which was a false
# combinational loop (Vortex FPU/ALU). lhd_full_case marks such cases full_case before proc; cases that miss a
# value are left alone and keep their latch.
sh_test(
    name = "yosys_full_case_test",
    srcs = ["tests/yosys_full_case_test.sh"],
    data = [
        "tests/full_case_dontcare.sv",
        "tests/full_case_hold.sv",
        "tests/full_case_incomplete.sv",
        "//lhd",
    ],
    deps = [":scripts"],
)

# Bounded-check reset protocol: the reset window must clock the design and
# un-reset power-on state is X (a don't-care on gold), so a QN-flop netlist of
# a synchronous-reset design is not falsely refuted while real post-reset
# divergences still are.
sh_test(
    name = "lgcheck_qn_reset_test",
    srcs = ["tests/yosys_qn_reset_test.sh"],
    data = [":scripts"],
)

# The temporal-induction stage must not constrain a conventionally named reset
# inside its checked window (a vacuous base case PROVED f^d == f+d when the
# port was named rst). Non-equivalent pairs refute with and without the name.
sh_test(
    name = "yosys_reset_soundness_test",
    srcs = ["tests/yosys_reset_soundness_test.sh"],
    data = [":scripts"],
)

# The structural/induction engines ignore which clock and edge drive a
# register; lgcheck runs them only on single-clock-edge designs. Clock/edge
# mismatches (edge flip, another clock, gated clock, mixed edges, latch
# polarity, memory port edge) must never be proven; mapped netlists with
# inverted/buffered clocks, QN state and an ICG vs an enable must still prove.
sh_test(
    name = "yosys_clock_domain_test",
    srcs = ["tests/yosys_clock_domain_test.sh"],
    data = [":scripts"],
    # Isolate the short solver-budget check from concurrent builds
    # starving debug Yosys before its bounded check reaches the fallback.
    tags = ["exclusive"],
)
