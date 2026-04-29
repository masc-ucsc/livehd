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
        "@hif//hif",
        "@rapidjson",
    ],
    alwayslink = True,
)

cc_test(
    name = "lgtuple_test",
    srcs = ["tests/lgtuple_test.cpp"],
    copts = COPTS,
    includes = ["."],
    deps = [
        ":lgraph",
        "@googletest//:gtest_main",
    ],
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

cc_test(
    name = "iter_test",
    srcs = ["tests/iter_test.cpp"],
    tags = ["fixme"],
    deps = [
        ":lgraph",
    ],
)

cc_test(
    name = "edge_test",
    srcs = ["tests/edge_test.cpp"],
    deps = [
        ":lgraph",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "graph_core_test",
    srcs = ["tests/graph_core_test.cpp"],
    tags = ["fixme"],
    deps = [
        ":lgraph",
        "@boost//:graph",
        "@googletest//:gtest_main",
    ],
)
