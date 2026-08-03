# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_bitfuzz",
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
        "//pass/bitwidth:pass_bitwidth",
        "//pass/common:pass",
    ],
    alwayslink = True,  # static Pass_plugin registration
)

cc_test(
    name = "bitfuzz_test",
    srcs = ["bitfuzz_test.cpp"],
    copts = COPTS,
    deps = [
        ":pass_bitfuzz",
        "//core",
        "//graph",
        "@googletest//:gtest_main",
    ],
)
