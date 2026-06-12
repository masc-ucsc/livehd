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

sh_test(
    name = "slang_test.sh",
    srcs = ["tests/slang_test.sh"],
    data = [
        "slang",
        "//inou/yosys:verilog_tests",
    ],
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
