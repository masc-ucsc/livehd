# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_lec",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//pass/common:pass",
        "//third_party/misc/ezsat",
        "@boolector",
        "@cryptominisat",
    ],
    alwayslink = True,
)

cc_test(
    name = "lec_test1",
    srcs = ["lec_test1.cpp"],
    deps = [
        ":pass_lec",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "boolector_test",
    srcs = ["boolector_test.cpp"],
    deps = [
        "@boolector",
        "@cryptominisat",
    ],
)
