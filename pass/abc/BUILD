# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_abc",
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
        "//pass/common:pass",
        "//pass/partition:pass_partition",
        "@abc",
    ],
    alwayslink = True,  # static Pass_plugin registration
)

# Functional unit test for the backend-agnostic adder/comparator builders
# (abc_arith.hpp), instantiated with a software bit model — no ABC dependency.
cc_test(
    name = "abc_arith_test",
    srcs = [
        "abc_arith.hpp",
        "abc_arith_test.cpp",
    ],
    copts = COPTS,
    includes = ["."],
    deps = ["@googletest//:gtest_main"],
)
