# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_cprop",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//graph",
        "//core",
        "//pass/common:pass",
    ],
    alwayslink = True,
)

cc_test(
    name = "cprop_test",
    srcs = ["cprop_test.cpp"],
    copts = COPTS,
    deps = [
        ":pass_cprop",
        "//pass/enableopt:pass_enableopt",
        "//pass/bitwidth:pass_bitwidth",
        "//core",
        "//graph",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "cprop_lowlane_test",
    srcs = ["cprop_lowlane_test.cpp"],
    copts = COPTS,
    deps = [
        ":pass_cprop",
        "//core",
        "//graph",
        "@googletest//:gtest_main",
    ],
)
