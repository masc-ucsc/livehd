# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//tools:regression.bzl", "cc_test")
load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass",
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
        "@hhds//hhds:graph",
    ],
    alwayslink = True,
)

cc_test(
    name = "eprp_test",
    srcs = ["tests/eprp_test.cpp"],
    deps = [
        ":pass",
        "@googletest//:gtest_main",
    ],
)
