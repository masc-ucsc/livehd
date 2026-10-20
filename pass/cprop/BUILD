# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_cprop",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(
        ["*.hpp"],
        exclude = ["cprop_test_eval.hpp"],
    ),
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

# The evaluators the cprop gtests and pass/aegraph's parity/differential smokes
# share (mux_eval, Mux_graph, the Dlop evaluator). Test-only: it includes gtest.
cc_library(
    name = "cprop_test_eval",
    testonly = True,
    hdrs = ["cprop_test_eval.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//pass:__subpackages__"],
    deps = [
        "//core",
        "//graph",
        "@googletest//:gtest",
        "@hhds//hhds:graph",
    ],
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
        ":cprop_test_eval",
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
