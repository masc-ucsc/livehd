# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# buildifier: disable=load
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
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
        "//lgraph",
        "//lnast",
        "//upass/core:upass",
    ],
    alwayslink = True,
)

cc_test(
    name = "upass_runner_cycle_test",
    srcs = ["upass_runner_cycle_test.cpp"],
    copts = COPTS,
    deps = [
        ":upass_runner",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "upass_runner_lgraph_test",
    srcs = ["upass_runner_lgraph_test.cpp"],
    copts = COPTS,
    deps = [
        ":upass_runner",
        "//core",
        "@googletest//:gtest_main",
    ],
)
