#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "core",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    linkopts = ["-lstdc++fs"],  # for gcc-8 or older
    deps = [
        "@rapidjson//:headers",
        "@iassert//:iassert",
        "@boost//:multiprecision",
        "//eprp:eprp",
        "//mmap_lib:headers",
        "//lbench:headers",
        "//lemu:lemu",
        ],
    )

cc_library(
    name = "tree_lgdb_test",
    hdrs = glob(["tests/tree_lgdb_setup.hpp"]),
    includes = ["."],
    deps = [
        ":core",
        "@gtest//:gtest_main",
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
    deps = [
        ":tree_lgdb_test",
        ],
    )

cc_test(
    name = "graph_core_test",
    srcs = ["tests/graph_core_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":core",
        ],
    )

cc_test(
    name = "lgraph_test",
    srcs = ["tests/lgraph_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":core",
        ],
    )

cc_test(
    name = "attribute_test",
    srcs = ["tests/attribute_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":core",
        ],
    )

cc_test(
    name = "lgraph_each",
    srcs = ["tests/lgraph_each_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":core",
        ],
    )

cc_test(
    name = "node_test",
    srcs = ["tests/node_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":core",
        ],
    )

cc_test(
    name = "iter_test",
    srcs = ["tests/iter_test.cpp"],
    deps = [
        ":core",
        ],
    )

cc_test(
    name = "graph_bench",
    srcs = ["tests/graph_bench.cpp"],
    deps = [
        ":core",
         "@graph//:graph",
         "@range-v3//:range-v3",
         "@fmt//:fmt",
	 ":tree_lgdb_test",
        ],
    )

cc_test(
    name = "edge_test",
    srcs = ["tests/edge_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":core",
        ],
    )

