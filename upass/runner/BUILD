# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# buildifier: disable=load
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_runner",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//lnast",
        "//upass/bitwidth:lnast_range_hdr",
        "//upass/core:array_dim_hdr",
        "//upass/core:upass",
    ],
    alwayslink = True,
)

cc_test(
    name = "upass_runner_test",
    srcs = ["upass_runner_test.cpp"],
    copts = COPTS,
    deps = [
        ":upass_runner",
        "//upass/attributes:upass_attributes",  # constprop's declared dep (2b/I)
        "//upass/constprop:upass_constprop",
        "//upass/noop:upass_noop",
        "//upass/typecheck:upass_typecheck",  # constprop's declared dep (2b/I)
        "@googletest//:gtest_main",
    ],
)

# upass must stay O(N): the loop roll planner walks a body once per class of
# names, never once per name or per loop (tests/loop_roll_linear_test.sh:
# pass.upass grows linearly from K to 4K invariants, untyped invariants,
# final-only outputs, and loops in a row).
sh_test(
    name = "loop_roll_linear_test",
    size = "medium",
    srcs = ["tests/loop_roll_linear_test.sh"],
    data = ["//lhd"],
)
