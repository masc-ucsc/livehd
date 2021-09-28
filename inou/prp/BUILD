# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "inou_prp",
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
        "@tree-sitter-pyrope//:tree-sitter-pyrope",
    ],
    alwayslink = True,
)

cc_binary(
    name = "ts_traverse_test",
    srcs = ["tests/ts_traverse_test.cpp"],
    copts = COPTS,
    deps = [
        ":inou_prp",
        # "@com_google_googletest//:gtest_main",
    ],
)
