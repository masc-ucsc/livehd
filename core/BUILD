# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "core",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS + ["-Wno-error=shadow"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//eprp",
        "//lemu",
        "//mmap_lib:headers",
        "//task",
        "@boost//:multiprecision",
        "@com_google_absl//absl/container:btree",
        "@iassert",
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
        ":core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "lgtuple_test",
    srcs = ["tests/lgtuple_test.cpp"],
    copts = COPTS,
    includes = ["."],
    deps = [
        ":core",
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
    name = "graph_core_test",
    srcs = ["tests/graph_core_test.cpp"],
    #tags = ["fixme"],
    deps = [
        ":core",
        "@boost//:graph",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "lgraph_test",
    srcs = ["tests/lgraph_test.cpp"],
    deps = [
        ":core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "attribute_test",
    srcs = ["tests/attribute_test.cpp"],
    deps = [
        ":core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "lgraph_each",
    srcs = ["tests/lgraph_each_test.cpp"],
    deps = [
        ":core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "node_test",
    srcs = ["tests/node_test.cpp"],
    deps = [
        ":core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "iter_test",
    srcs = ["tests/iter_test.cpp"],
    tags = ["fixme"],
    deps = [
        ":core",
    ],
)

cc_test(
    name = "graph_bench",
    srcs = ["tests/graph_bench.cpp"],
    tags = ["fixme"],
    deps = [
        ":core",
        ":tree_lgdb_test",
        "@fmt",
    ],
)

cc_test(
    name = "edge_test",
    srcs = ["tests/edge_test.cpp"],
    deps = [
        ":core",
        "@com_google_googletest//:gtest_main",
    ],
)
