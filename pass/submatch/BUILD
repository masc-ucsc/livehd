# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_submatch",
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
        "//third_party/misc/ezsat",
    ],
    alwayslink = True,
)

cc_test(
    name = "submatch_test",
    srcs = ["test_submatch.cpp"],
    deps = [
        ":pass_submatch",
        "@com_google_googletest//:gtest_main",
    ],
)
