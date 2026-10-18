# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_aegraph",
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
        "//pass/cprop:pass_cprop",
    ],
    alwayslink = True,
)

cc_test(
    name = "aegraph_test",
    srcs = ["aegraph_test.cpp"],
    copts = COPTS,
    deps = [
        ":pass_aegraph",
        "//core",
        "//graph",
        "//pass/cprop:pass_cprop",
        "//pass/lec:lec_query",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)
