#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "headers",
    hdrs = glob(["include/*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["include"],
    deps = [
        "@iassert//:iassert",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_library(
    name = "mmap_lib_test_lib",
    hdrs = glob(["tests/*.hpp"]),
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
        "@gtest//:gtest_main",
        "@fmt//:fmt",
    ],
)

cc_test(
    name = "bench_map_use",
    srcs = ["tests/bench_map_use.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "//bench:headers",
        "@sparsehash//:headers",
        "@fmt//:fmt",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:node_hash_map",
    ],
)

cc_test(
    name = "bench_set_use",
    srcs = ["tests/bench_set_use.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "//bench:headers",
        "@sparsehash//:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:node_hash_set",
    ],
)

cc_test(
    name = "bench_vector_use",
    srcs = ["tests/bench_vector_use.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "//bench:headers",
        "@sparsehash//:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@gtest//:gtest_main",
        "@fmt//:fmt",
    ],
)

cc_test(
    name = "mmap_map_test",
    srcs = ["tests/mmap_map_test.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "//bench:headers",
        "@sparsehash//:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@gtest//:gtest_main",
        "@fmt//:fmt",
    ],
)

cc_test(
    name = "mmap_gc_test",
    srcs = ["tests/mmap_gc_test.cpp"],
    deps = [
        ":headers",
        "@gtest//:gtest_main",
        "//bench:headers",
    ],
)

cc_test(
    name = "mmap_vector_test",
    srcs = ["tests/mmap_vector_test.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "//bench:headers",
        "@gtest//:gtest_main",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)

cc_test(
    name = "bench_vector_yas",
    srcs = ["tests/bench_vector_yas.cpp"],
    deps = [
        ":mmap_lib_test_lib",
        "//bench:headers",
        "@yas//:headers",
    ],
)

