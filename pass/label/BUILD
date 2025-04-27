# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_label",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//pass/common:pass",
        "//third_party/viecut",
    ],
    alwayslink = True,  # Needed to have constructor called
)

cc_test(
    name = "acyclic_test",
    srcs = ["acyclic_test.cpp"],
    deps = [
        ":pass_label",
        "@googletest//:gtest_main",
        "@fmt//:fmt",
    ],
)

cc_test(
    name = "synth_test",
    srcs = ["synth_test.cpp"],
    deps = [
        ":pass_label",
        "@googletest//:gtest_main",
        "@fmt//:fmt",
    ],
)

cc_test(
    name = "mincut_test",
    srcs = ["mincut_test.cpp"],
    deps = [
        ":pass_label",
        "//third_party/viecut",
        "@googletest//:gtest_main",
        "@fmt//:fmt",
    ],
)
