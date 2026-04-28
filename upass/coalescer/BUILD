# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_coalescer",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//lnast",
        "//upass/core:upass",
    ],
    alwayslink = True,
)

cc_test(
    name = "upass_coalescer_test",
    srcs = ["upass_coalescer_test.cpp"],
    copts = COPTS,
    deps = [
        ":upass_coalescer",
        "@googletest//:gtest_main",
    ],
)
