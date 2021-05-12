# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "headers",
    srcs = glob(["include/*.cpp"]),
    hdrs = glob(["include/*.hpp"]),
    copts = COPTS,
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@iassert",
    ],
)

cc_library(
    name = "mmap_lib_test_lib",
    hdrs = glob(["tests/*.hpp"]),
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":headers",
    ],
)

cc_test(
    name = "tree_test",
    srcs = ["tests/tree_test.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "@com_google_googletest//:gtest_main",
        "@fmt",
    ],
)

cc_test(
    name = "strbench",
    srcs = ["tests/strbench.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "//lbench:headers",
        "@com_google_googletest//:gtest_main",
        "@fmt",
    ],
)

cc_test(
    name = "bench_tree",
    srcs = ["tests/bench_tree.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "//lbench:headers",
        "@fmt",
    ],
)

cc_test(
    name = "bench_map_use",
    srcs = ["tests/bench_map_use.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "//lbench:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:node_hash_map",
        "@fmt",
    ],
)

cc_test(
    name = "bench_set_use",
    srcs = ["tests/bench_set_use.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "//lbench:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:node_hash_set",
    ],
)

cc_test(
    name = "bench_vector_use",
    srcs = ["tests/bench_vector_use.cpp"],
    tags = ["fixme"],
    deps = [
        ":mmap_lib_test_lib",
        "//lbench:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_googletest//:gtest_main",
        "@fmt",
    ],
)

cc_test(
    name = "bench_vset_use",
    srcs = ["tests/bench_vset_use.cpp"],
    deps = [
        "mmap_lib_test_lib",
        "//lbench:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_googletest//:gtest_main",
        "@fmt",
    ],
)

cc_test(
    name = "bench_pstr_use",
    srcs = ["tests/bench_pstr_use.cpp"],
    deps = [
        "mmap_lib_test_lib",
        "//lbench:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_googletest//:gtest_main",
        "@fmt",
    ],
)

cc_test(
    name = "mmap_map_test",
    srcs = ["tests/mmap_map_test.cpp"],
    #tags = [ "fixme", ],
    deps = [
        ":mmap_lib_test_lib",
        "//lbench:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_googletest//:gtest_main",
        "@fmt",
    ],
)

cc_test(
    name = "mmap_gc_test",
    srcs = ["tests/mmap_gc_test.cpp"],
    deps = [
        ":headers",
        "//lbench:headers",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "mmap_vector_test",
    srcs = ["tests/mmap_vector_test.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "//lbench:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "mmap_str_test",
    srcs = ["tests/mmap_str_test.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "//lbench:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_googletest//:gtest_main",
        "@fmt",
    ],
)
