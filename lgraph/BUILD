# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "lgraph",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS + ["-Wno-error=shadow"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        # "//eprp",
        "//core",
        "//graph",
        "@hhds//hhds:graph",
        "@hif//hif",
        "@rapidjson",
    ],
    alwayslink = True,
)

cc_test(
    name = "lgraph_test",
    srcs = ["tests/lgraph_test.cpp"],
    deps = [
        ":lgraph",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "lgraph_each",
    srcs = ["tests/lgraph_each_test.cpp"],
    deps = [
        ":lgraph",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "node_test",
    srcs = ["tests/node_test.cpp"],
    deps = [
        ":lgraph",
        "@googletest//:gtest_main",
    ],
)
