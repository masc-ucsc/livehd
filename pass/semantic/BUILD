# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_semantic",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//inou/pyrope:inou_pyrope",
        "//pass/common:pass",
    ],
    alwayslink = True,
)

cc_library(
    name = "semantic_check",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "*test*.cpp",
            "pass*",
        ],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//inou/pyrope:inou_pyrope",
        "//pass/common:pass",
    ],
)

cc_test(
    name = "lnast_semantic_test",
    srcs = ["tests/lnast_semantic_test.cpp"],
    tags = ["fixme"],
    deps = [
        ":semantic_check",
    ],
)
