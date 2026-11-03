# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//tools:regression.bzl", "sh_test")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_shell//shell:sh_library.bzl", "sh_library")

load("//tools:copt_default.bzl", "COPTS")
load("//inou/prp:tests/equiv/fixtures.bzl", "LEGACY_YOSYS_PAIRS")

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

# General HDL round trips are covered by the canonical Pyrope/HDL pairs.
# Keep dedicated regressions for importer-specific operand and memory lowering.
[sh_test(
    name = "yosys_import-" + stem,
    srcs = ["//inou/prp:tests/yvr_test.py"],
    args = ["--yosys-oracle", "-i", "$(location //inou/slang:tests/reader/%s.v)" % stem],
    data = [
        "//inou/slang:tests/reader/%s.v" % stem,
        "//inou/prp:tests/lec.py",
        "//lhd",
    ],
    deps = [":scripts"],
) for stem in ["bitwise_extension", "packed_byte_ram_write", "memory_mixed_read_latency"]]

# Compatibility names reuse the canonical paired tests; no duplicate actions.
[test_suite(
    name = "yosys_compile-" + old,
    tests = ["//inou/prp:prp-equiv-" + pair],
) for old, pair in LEGACY_YOSYS_PAIRS.items()]

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
_STATE_MATCH_GROUPS = ["temporary", "packed", "encoding", "memory", "rtl", "bank", "loop", "init_names", "poweron", "budget"]

[
    sh_test(
        name = "lgcheck_state_" + group,
        srcs = ["tests/yosys_state_match_test.sh"],
        args = [group],
        data = [":scripts"],
    )
    for group in _STATE_MATCH_GROUPS
]

test_suite(
    name = "lgcheck_state_test",
    tests = [":lgcheck_state_" + group for group in _STATE_MATCH_GROUPS],
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

# A flop with an initializer AND a reset keeps one value (the reset value): the
# reader warns when the initializer differs and is dropped, and stays quiet when
# nothing is lost (same value, x bits, or a sync reset that stays a data mux).
sh_test(
    name = "yosys_init_reset_warning_test",
    srcs = ["tests/yosys_init_reset_warning_test.sh"],
    data = ["//lhd"],
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
)

# Lower large designs once before the proof budget; changed inputs in the same
# work directory must still refute rather than reuse stale prepared designs.
sh_test(
    name = "lgcheck_prepared_inputs_smoke",
    srcs = ["tests/yosys_prepared_inputs_smoke.sh"],
    data = [":scripts"],
)

# Escaped SV instance delimiters must not become illegal RTLIL characters or
# collide with nested instance paths and literal occurrence-encoding markers.
sh_test(
    name = "yosys_escaped_hierarchy_smoke",
    srcs = ["tests/yosys_escaped_hierarchy_smoke.sh"],
    data = [":scripts"],
)
