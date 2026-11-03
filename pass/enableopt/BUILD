# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//tools:regression.bzl", "cc_test")
load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_enableopt",
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
        "//pass/cprop:pass_cprop",
        "//core",
        "//pass/common:pass",
    ],
    alwayslink = True,
)

cc_test(
    name = "enableopt_test",
    srcs = ["enableopt_test.cpp"],
    copts = COPTS,
    deps = [
        ":pass_enableopt",
        "//graph",
        "@googletest//:gtest_main",
    ],
)
