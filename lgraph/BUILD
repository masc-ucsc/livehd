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
        "@rapidjson//:headers",
    ],
    alwayslink = True,
)

cc_library(
    name = "tree_lgdb_test",
    hdrs = ["tests/tree_lgdb_setup.hpp"],
    copts = COPTS,
    includes = ["."],
    deps = [
        ":lgraph",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "lgtuple_test",
    srcs = ["tests/lgtuple_test.cpp"],
    copts = COPTS,
    includes = ["."],
    deps = [
        ":lgraph",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "traversal_hierarchy_test",
    srcs = ["tests/traversal_hierarchy_test.cpp"],
    deps = [
        ":tree_lgdb_test",
    ],
)

cc_test(
    name = "traverse_test",
    srcs = ["tests/traverse_test.cpp"],
    tags = ["fixme"],
    deps = [
        ":tree_lgdb_test",
    ],
)

cc_test(
    name = "lgraph_test",
    srcs = ["tests/lgraph_test.cpp"],
    deps = [
        ":lgraph",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "lgraph_each",
    srcs = ["tests/lgraph_each_test.cpp"],
    deps = [
        ":lgraph",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "node_test",
    srcs = ["tests/node_test.cpp"],
    deps = [
        ":lgraph",
        "@com_google_googletest//:gtest_main",
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
    name = "graph_bench",
    srcs = ["tests/graph_bench.cpp"],
    tags = ["fixme"],
    deps = [
        ":lgraph",
        ":tree_lgdb_test",
        "@fmt",
    ],
)

cc_test(
    name = "edge_test",
    srcs = ["tests/edge_test.cpp"],
    deps = [
        ":lgraph",
        "@com_google_googletest//:gtest_main",
    ],
)
