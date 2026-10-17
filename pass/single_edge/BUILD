# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_single_edge",
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
    ],
    alwayslink = True,  # static Pass_plugin registration
)

# The Sub inliner as it stood before its linear-cost rework, frozen: the
# identity reference proof_prep_test compares the live inliner against.
cc_library(
    name = "inline_sub_reference",
    testonly = True,
    srcs = ["tests/inline_sub_reference.cpp"],
    hdrs = ["tests/inline_sub_reference.hpp"],
    copts = COPTS,
    includes = ["tests"],
    deps = [
        "//core",
        "//graph",
        "@hhds//hhds:graph",
    ],
)

cc_test(
    name = "proof_prep_test",
    srcs = ["proof_prep_test.cpp"],
    copts = COPTS,
    deps = [
        ":inline_sub_reference",
        ":pass_single_edge",
        "//core",
        "//graph",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)

cc_test(
    name = "pass_single_edge_test",
    srcs = ["pass_single_edge_test.cpp"],
    copts = COPTS,
    deps = [
        ":pass_single_edge",
        "//graph",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)
