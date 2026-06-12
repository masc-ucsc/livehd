# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
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

# Direct --reader slang (SV -> LNAST) on slang v11.0 (todo/ 1s). The
# slang_compile-* sh_tests below are the tracked pass set; each drives
# `lhd compile --reader slang` + `lhd synth ln:` round-trip. The broader
# inou/yosys/tests/*.v corpus is exercised (best-effort) by slang_compile.sh's
# default mode and by the tests/verilog sky130 cases — wider SystemVerilog
# coverage and a tracked test ladder are 2s, not 1s.
#
# Concurrency: inou.slang now runs one isolated slang Driver/Compilation per
# file, sequentially (the old in-process thread_pool fan-out was never sound).
# Parallelism comes from running independent `lhd elaborate --reader slang`
# invocations concurrently under Bazel — every slang_compile-* target above is
# its own action, so `bazel test //inou/slang:all` is the parallel smoke test.

[sh_test(
    name = "slang_compile-%s" % t,
    srcs = ["tests/slang_compile.sh"],
    args = ["$(location //inou/yosys:tests/%s.v)" % t],
    data = [
        "//inou/yosys:tests/%s.v" % t,
        "//lhd",
    ],
    tags = [
        "slang_compile",
    ],
) for t in [
    "add",
    "add1",
    "add2",
    "assigns",
    "compare2",
    "consts",
    "issue_047",
    "mt_basic_test",
    "offset_input",
    "satlarge",
    "satsmall",
    "simple_weird2",
    "trivial",
    "trivial_and",
    "trivial_join",
    "trivial_offset",
]]
