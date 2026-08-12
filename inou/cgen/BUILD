# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "inou_cgen",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*_test.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//graph",
        "//pass/common:pass",
    ],
    alwayslink = True,
)

cc_test(
    name = "sim_color_plan_test",
    srcs = ["sim_color_plan_test.cpp"],
    copts = COPTS,
    deps = [
        ":inou_cgen",
        "//graph",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)
