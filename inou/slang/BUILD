# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")
load("//inou/slang:slang_ladder.bzl", "SLANG_LADDER")

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
        "//pass/common:pass",
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
    name = "slang_test.sh",
    srcs = ["tests/slang_test.sh"],
    data = [
        "slang",
        "//inou/yosys:verilog_tests",
    ],
)

# `--reader slang -- -F filelist.f` passthrough: the verilog sources ride the
# raw slang driver args (a `-F` command file) with no positional `*.v` input.
sh_test(
    name = "slang_filelist",
    size = "large",
    srcs = ["tests/slang_filelist.sh"],
    data = glob(["tests/filelist/*"]) + ["//lhd"],
    deps = [
        "//inou/yosys:scripts",  # lgcheck + yosys2 for the LEC check
    ],
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
# target per inou/yosys/tests/*.v source, pinned to its strongest passing tier
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
        "//inou/yosys:tests/%s.v" % t,
        "//lhd",
    ],
    tags = [
        "slang_compile",
    ],
    deps = [
        "//inou/yosys:scripts",  # lgcheck + yosys2 for the lec tier
    ],
) for t in SLANG_LADDER.keys()]

# Legacy no-argument mode: the sky130 cell-instance set (unknown-module policy).
sh_test(
    name = "slang_compile_sky130",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    data = glob(["tests/verilog/*"]) + ["//lhd"],
    tags = [
        "slang_compile",
    ],
)

# Named structured assignment patterns are resolved by field, with explicit
# member setters overriding `default:`. This syntax is a slang-reader contract;
# the legacy Yosys parser used by the other compile suite does not accept it.
#
# tests/sv/ is the home for slang-only POSITIVE fixtures, because neither sweep
# directory can hold one: tests/verilog/ is swept at the `error` tier (a file
# that compiles is a FAILURE there), and //inou/yosys:tests/*.v is globbed into
# yosys_compile-* targets that this syntax would break. Pin new positive
# slang-reader fixtures here, one explicit target each.
sh_test(
    name = "slang_struct_pattern_named_default",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/struct_pattern_named_default.v)",
    ],
    data = [
        "tests/sv/struct_pattern_named_default.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# CVA6 forms whose constants become visible only after function/config binding:
# preserve `2 ** cfg.field` as a shift and a canonical cfg-bounded loop as an
# LNAST range loop. Also pins zero-fill assignment patterns and whole-array
# copies on unpacked arrays of packed structs, including a two-dimensional
# split-memory next-state copy and a full unpacked-array slice.
sh_test(
    name = "slang_deferred_cva6_forms",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/deferred_cva6_forms.v)",
    ],
    data = [
        "tests/sv/deferred_cva6_forms.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

# A bound that remains runtime after uPass must fail at the comptime-loop
# boundary, not in inou.slang and not by silently dropping the loop body.
sh_test(
    name = "slang_deferred_for_runtime",
    size = "large",
    srcs = ["tests/deferred_for_runtime_smoke.sh"],
    args = ["$(location tests/sv/deferred_for_runtime.v)"],
    data = [
        "tests/sv/deferred_for_runtime.v",
        "//lhd",
    ],
)

# Storage-aware scalar replacement of a packed scalar-element array. Constant
# element accesses route directly to leaves, a dynamic read preserves the
# SystemVerilog selector semantics, and whole-value use reassembles only at the
# output boundary.
sh_test(
    name = "slang_packed_array_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_packed_vector_port_sroa",
    size = "large",
    srcs = ["tests/packed_vector_port_sroa_test.sh"],
    data = [
        "tests/sv/packed_vector_port_sroa.v",
        "//lhd",
    ],
)

# `{{N{v[msb]}}, v}` is a sign extension, not a broadcast + shift + or. Pins
# both halves: the shapes that MUST fold to `sext`, and the look-alikes that
# must not.
sh_test(
    name = "slang_concat_sext",
    size = "large",
    srcs = ["tests/concat_sext_test.sh"],
    data = [
        "tests/sv/concat_sext.v",
        "//lhd",
    ],
)

# A plain packed local whose first lowered write sits inside a comptime-decided
# `if` arm used to lose its declaration entirely (the arm, and the declare with
# it, is dead-code-eliminated), so the emitted Pyrope did not recompile. Round
# trip, because the first leg emits fine.
sh_test(
    name = "slang_comptime_arm_declare_hoist",
    srcs = ["tests/comptime_arm_declare_hoist_test.sh"],
    data = [
        "tests/sv/comptime_arm_declare_hoist.v",
        "//lhd",
    ],
)

# Packed 2-D array eligibility, both directions: a net-initialized lane table
# read at a runtime index must SPLIT (barrel shift -> lane mux), and an array
# written through a whole-array range select must stay FLAT (the split
# representations drop the write).
sh_test(
    name = "slang_packed_array_net_init_mux",
    size = "large",
    srcs = ["tests/packed_array_net_init_mux_test.sh"],
    data = [
        "tests/sv/packed_array_net_init_mux.v",
        "//lhd",
    ],
)

sh_test(
    name = "slang_packed_array_reg_array_emit",
    size = "large",
    srcs = ["tests/packed_array_reg_array_emit_test.sh"],
    data = [
        "tests/sv/packed_array_reg_array_emit.v",
        "//lhd",
    ],
)

sh_test(
    name = "slang_packed_array_wire_init_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_wire_init_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_wire_init_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_packed_array_reg_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_reg_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_reg_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
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
    deps = [
        "//inou/yosys:scripts",
    ],
)

# Ascending packed indices deliberately reverse source index versus physical
# lane ordinal. Dynamic access includes out-of-range values and whole-value
# packing must keep the declared [0:3] ordering.
sh_test(
    name = "slang_packed_array_ascending_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_ascending_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_ascending_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_packed_array_signed_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_signed_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_signed_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_packed_array_port_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_port_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_port_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_packed_array_struct_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_struct_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_struct_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)

sh_test(
    name = "slang_packed_array_struct_reg_sroa",
    size = "large",
    srcs = ["tests/slang_compile.sh"],
    args = [
        "lec",
        "$(location tests/sv/packed_array_struct_reg_sroa.v)",
    ],
    data = [
        "tests/sv/packed_array_struct_reg_sroa.v",
        "//lhd",
    ],
    deps = [
        "//inou/yosys:scripts",
    ],
)
