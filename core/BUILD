# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

config_setting(
    name = "use_profiling",
    values = {"define": "profiling=1"},
)

cc_library(
    name = "core",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    defines = select({
        ":use_profiling": [
            "LIVEHD_PROFILING=1",
        ],
        "//conditions:default": [],
    }),
    includes = ["."],
    linkopts = ["-lpthread"],
    visibility = ["//visibility:public"],
    deps = [
        "@boost//:multiprecision",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@fmt",
        "@iassert",
    ] + select({
        ":use_profiling": [
            "@com_google_perfetto//:perfetto",
        ],
        "//conditions:default": [],
    }),
)

cc_test(
    name = "thread_pool_test",
    srcs = [
        "tests/concurrentqueue.hpp",
        "tests/thread_pool_test.cpp",
    ],
    # tags = ["long1"], # Run only with long1 set of tests
    deps = [
        ":core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "pool_bench",
    srcs = ["tests/pool_bench.cpp",
        "tests/concurrentqueue.hpp",
    ],
    deps = [
        ":core",
        "@com_google_benchmark//:benchmark",
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
    name = "lconst_test",
    srcs = ["tests/lconst_test.cpp"],
    deps = [
        ":core",
        "//simlib:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "lconst_bench",
    srcs = ["tests/lconst_bench.cpp"],
    deps = [
        ":core",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_benchmark//:benchmark",
    ],
)

cc_test(
    name = "blop_test",
    srcs = ["tests/blop_test.cpp"],
    deps = [
        ":core",
        "@com_google_googletest//:gtest_main",
    ],
)
