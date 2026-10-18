# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//inou/slang:slang_ladder.bzl", "SLANG_LADDER")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "inou_slang",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@slang",  # Must be before pass because both have FMT and pass has priority (or slang fails)
    ] + [
        "//core",
        "//pass/common:pass",
        "//pass/liberty:liberty_dff",
        # prpparse's OWN keyword table (prp_keywords.def) -- a Verilog parameter
        # named like a Pyrope reserved word cannot be emitted as a generic. Same
        # dep, same reason, as upass/prp_writer: never hand-copy the list.
        "@prpparse//:prp_keywords",
    ],
    alwayslink = True,  # Needed to have constructor called
)

cc_binary(
    name = "rewriter",
    srcs = ["tests/rewriter.cpp"],
    copts = COPTS,
    deps = [
        ":inou_slang",
    ],
)

cc_binary(
    name = "slang",
    srcs = ["tests/slang.cpp"],
    copts = COPTS,
    deps = [
        ":inou_slang",
    ],
)

# Verilog -> Pyrope emission must be byte-reproducible: the reader linearizes
# pointer-keyed sets, and a non-total sort key let generate-loop replicas
# permute, which RENAMES them through lname_of's `_sN` uniquing.
sh_test(
    name = "slang_emit_determinism",
    srcs = ["tests/slang_emit_determinism.sh"],
    data = ["//lhd"],
)

sh_test(
    name = "slang_escaped_top",
    srcs = ["tests/escaped_top.sh"],
    data = ["//lhd"],
)

sh_test(
    name = "slang_missing_source",
    srcs = ["tests/missing_source.sh"],
    data = ["//lhd"],
)

# Run-time severity tasks are not hardware: ignored with one warning per call.
sh_test(
    name = "severity_task_warning",
    srcs = ["tests/severity_task_warning.sh"],
    data = [
        "tests/sv/severity_tasks_ignored.sv",
        "//lhd",
    ],
    env = {"FIXTURES": "inou/slang/tests/sv"},
)

sh_test(
    name = "automatic_packed_local",
    srcs = ["tests/automatic_packed_local.sh"],
    data = ["//lhd"],
)

# `--reader slang -- -F filelist.f` passthrough: the verilog sources ride the
# raw slang driver args (a `-F` command file) with no positional `*.v` input.
sh_test(
    name = "slang_filelist",
    size = "large",
    srcs = ["tests/slang_filelist.sh"],
    data = glob(["tests/filelist/*"]) + ["//lhd"],
)

# Unknown-module (blackbox) policy: without a user --ignore-unknown-modules
# the compile fails cleanly; with it, unknown instances become blackbox
# sub-instances the pyrope emission imports, and supplying their .prp
# definitions LEC-proves against the golden.
sh_test(
    name = "slang_blackbox",
    size = "large",
    srcs = ["tests/slang_blackbox.sh"],
    data = glob(["tests/blackbox/*"]) + ["//lhd"],
)

# The --reader slang coverage ladder (todo/ 2s subtask E): one slang_compile-*
# target per ladder entry (an inou/yosys/tests/*.v source not subsumed by a
# larger sibling), pinned to its strongest passing tier
# in slang_ladder.bzl (lec > verilog > lnast > error — see
# inou/slang/README.md). tests/slang_compile.sh enforces the expectation both
# ways, so a promotion or regression is an explicit ladder edit.
#
# Concurrency: inou.slang runs one isolated slang Driver/Compilation per file,
# sequentially. Parallelism comes from running independent bazel test actions
# concurrently — `bazel test //inou/slang:all` is the parallel sweep.

[sh_test(
    name = "slang_compile-%s" % t,
    size = "enormous" if t.startswith("long_") or t.startswith("nocheck_") else "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        SLANG_LADDER[t],
        "$(location //inou/yosys:tests/%s.v)" % t,
    ],
    data = [
        "//inou/prp:tests/lec.py",
        "//inou/yosys:tests/%s.v" % t,
        "//lhd",
    ],
    # `long1` is one of the tags .bazelrc excludes from the default `bazel test`
    # sweep: long_nocheck_iwls_square takes far too long for the default run.
    tags = [
        "slang_compile",
    ] + (["long1"] if t == "long_nocheck_iwls_square" else []),
) for t in SLANG_LADDER.keys()]

# Fully defined ready chains must not pass LEC by turning a forward read into X.
# One target runs the `lec_no_x` tier on every fixture in turn (a failure still
# prints FAIL(<top>) naming the fixture).
_READY_SIGNAL_FIXTURES = ["ready_signal_chain", "ready_signal_chain_1", "ready_signal_slices", "ready_signal_masked"]

sh_test(
    name = "slang_ready_signal_no_x",
    srcs = ["tests/slang_compile.sh"],
    args = ["lec_no_x"] + ["$(location //inou/prp:tests/equiv/%s.v)" % n for n in _READY_SIGNAL_FIXTURES],
    data = ["//inou/prp:tests/equiv/%s.v" % n for n in _READY_SIGNAL_FIXTURES] + [
        "//inou/prp:tests/lec.py",
        "//lhd",
    ],
    # `lec_no_x` runs a full LEC: reserve solver capacity like every other
    # formal-bearing slang case, and join the `slang_compile` sweep.
    tags = [
        "cpu:4",
        "slang_compile",
    ],
)

# Sets of `// :test: error` fixtures (each keeps its own `// :error:` regex)
# that run as ONE target apiece through `slang_compile.sh auto f1 f2 ...`:
# the clock-ruling rejects, and the reader's other clean-rejection diagnostics.
_SV_CONST_CLOCK_REJECTS = [
    "tests/sv/const_clock_%s_reject.v" % n
    for n in [
        "alias",
        "gate",
        "instance",
        "mem_instance",
        "mem_local",
        "mem_two_ports",
        "mem_unconnected",
        "missing",
        "unconnected_gated",
        "unconnected",
    ]
]

_SV_DERIVED_CLOCK_REJECTS = [
    "tests/sv/derived_clock_%s_reject.v" % n
    for n in [
        "and_data",
        "instance",
        "inverted",
        "mux",
        "or",
        "xor",
    ]
]

_SV_READER_REJECTS = [
    "tests/sv/bit_clock_blocking_reject.v",
    "tests/sv/bit_clock_dynamic_reject.v",
    "tests/sv/bit_clock_overlap_reject.v",
    "tests/sv/blocking_array_multiple_writers.sv",
    "tests/sv/deferred_for_runtime.v",
    "tests/sv/dual_async_set_reset_reject.v",
    "tests/sv/loop_continue.sv",
    "tests/sv/stream_partial.sv",
    "tests/sv/unbounded_while.sv",
    "tests/sv/while_counter_wrap.sv",
]

_SV_REJECT_SETS = {
    "slang_const_clock_rejects": _SV_CONST_CLOCK_REJECTS,
    "slang_derived_clock_rejects": _SV_DERIVED_CLOCK_REJECTS,
    "slang_reader_rejects": _SV_READER_REJECTS,
}

_SV_SOURCES = glob(
    [
        "tests/sv/*.v",
        "tests/sv/*.sv",
    ],
    allow_empty = True,
    exclude = [
        # These retain checks that a source/round-trip comparison cannot express.
        "tests/sv/scalar_initial_no_reset.v",
        "tests/sv/mem_struct_slice_write.v",
        "tests/sv/ignored_assertion_past.sv",
        "tests/sv/packed_array_loop_reg_sroa.v",
        "tests/sv/*_tb.v",
        "tests/sv/*_helper.v",
        "tests/sv/*_helper.sv",
    ] + _SV_CONST_CLOCK_REJECTS + _SV_DERIVED_CLOCK_REJECTS + _SV_READER_REJECTS,
)

# Fixtures with a `// :lec_solver: lgyosys` header: they shell out to lgcheck.
_SV_LGYOSYS = [
    "tests/sv/memory_sync_reset.v",
    "tests/sv/nb_dynamic_part_select_chain.v",
]

# A new ordinary SV regression is one source file: no per-case BUILD or script.
# Optional // :test: roundtrip|roundtrip_sim|error|lec_no_x headers select shared
# checks (see tests/slang_compile.sh). A roundtrip_sim fixture carries its
# :verilog_re:/:verilog_not_re: structure checks and either a sibling
# <stem>_tb.prp native testbench or a :sim_unsupported: refusal tripwire; an
# optional <stem>_tb.v event-level bench runs only with LHD_EXTERNAL_SIM=1.
[sh_test(
    name = "slang_%s" % src.split("/")[-1].rsplit(".", 1)[0],
    srcs = ["tests/slang_compile.sh"],
    args = [
        "auto",
        "$(location %s)" % src,
    ],
    data = [
        src,
        "//inou/prp:tests/lec.py",
        "//lhd",
    ] + glob(
        [
            src.rsplit(".", 1)[0] + "_tb.prp",
            src.rsplit(".", 1)[0] + "_tb.v",
        ],
        allow_empty = True,
    ) + (["//inou/yosys:scripts"] if src in _SV_LGYOSYS else []),
    # `lhd sim` finds the host C++ compiler (and LHD_EXTERNAL_SIM iverilog) on PATH.
    env_inherit = ["PATH"],
    # Keep bounded formal checks from oversubscribing frontend/solver capacity.
    tags = ["cpu:4"],
) for src in _SV_SOURCES]

[sh_test(
    name = name,
    srcs = ["tests/slang_compile.sh"],
    args = ["auto"] + ["$(location %s)" % src for src in srcs],
    data = srcs + [
        "//inou/prp:tests/lec.py",
        "//lhd",
    ],
    env_inherit = ["PATH"],
    tags = ["cpu:4"],
) for name, srcs in _SV_REJECT_SETS.items()]

sh_test(
    name = "slang_scalar_initial_no_reset",
    size = "large",
    srcs = ["tests/scalar_initial_no_reset_test.sh"],
    data = [
        "tests/sv/scalar_initial_no_reset.v",
        "//inou/prp:tests/lec.py",
        "//lhd",
    ],
)

sh_test(
    name = "slang_ignored_assertion_past",
    srcs = ["tests/ignored_assertion_past_test.sh"],
    data = [
        "tests/sv/ignored_assertion_past.sv",
        "//lhd",
    ],
)

sh_test(
    name = "slang_deep_fold_chain",
    srcs = ["tests/deep_fold_chain_test.sh"],
    data = [
        "//lhd",
    ],
)

sh_test(
    name = "slang_packed_array_loop_reg_sroa",
    size = "large",
    srcs = ["tests/packed_array_loop_reg_sroa_test.sh"],
    args = ["$(location tests/sv/packed_array_loop_reg_sroa.v)"],
    data = [
        "tests/sv/packed_array_loop_reg_sroa.v",
        "//lhd",
    ],
)

sh_test(
    name = "slang_mem_struct_slice_write",
    size = "large",
    srcs = ["tests/mem_struct_slice_write_test.sh"],
    data = [
        "tests/sv/mem_struct_slice_write.v",
        "//lhd",
    ],
)

sh_test(
    name = "slang_memory_clock_diagnostic",
    size = "large",
    srcs = ["tests/memory_clock_diagnostic.sh"],
    data = [
        "//lhd",
        "//tools:native_sim.py",
        "//ware:cgen_memory_rtl",
    ],
    # `lhd sim` finds the host C++ compiler (and LHD_EXTERNAL_SIM iverilog) on PATH.
    env_inherit = ["PATH"],
)

test_suite(
    name = "integration",
    tests = [":slang_" + src.split("/")[-1].rsplit(".", 1)[0] for src in _SV_SOURCES] + [":" + name for name in _SV_REJECT_SETS],
)

sh_test(
    name = "slang_reachable_modules",
    size = "small",
    timeout = "short",
    srcs = ["tests/reachable_modules.sh"],
    data = ["//lhd"],
)
