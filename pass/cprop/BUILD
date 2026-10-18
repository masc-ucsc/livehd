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
    srcs = [
        "cprop_lowlane_test.cpp",
        "cprop_muxctx_test.cpp",
        "cprop_opshare_test.cpp",
        "cprop_test.cpp",
        "loop_hoist_test.cpp",
    ],
    copts = COPTS,
    deps = [
        ":pass_cprop",
        "//core",
        "//graph",
        "//pass/bitwidth:pass_bitwidth",
        "//pass/enableopt:pass_enableopt",
        "//pass/lec:lec_query",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)
