# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_color",
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
        "//third_party/viecut",
    ],
    alwayslink = True,  # static Pass_plugin registration
)

[cc_test(
    name = test_name,
    srcs = [test_name + ".cpp"],
    copts = COPTS,
    deps = [
        ":pass_color",
        "//core",
        "//graph",
        "@googletest//:gtest_main",
    ],
) for test_name in [
    "acyclic_test",
    "synth_test",
    "path_test",
    "mincut_test",
]]
