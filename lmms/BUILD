#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "headers",
    hdrs = glob(["include/*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["include"],
    deps = ["@sparsehash//:headers"]
)

cc_test(
    name = "dense_test",
    srcs = ["tests/dense_test.cpp"],
    deps = [
        ":headers",
    ],
)

cc_test(
    name = "use_map_bench",
    srcs = ["tests/use_map_bench.cpp"],
    deps = [
        ":headers",
        "//bench:headers",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_test(
    name = "use_set_bench",
    srcs = ["tests/use_set_bench.cpp"],
    deps = [
        ":headers",
        "//bench:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@bm//:headers",
    ],
)

cc_test(
    name = "map_test",
    srcs = ["tests/map_test.cpp"],
    deps = [
        ":headers",
        "//bench:headers",
        "@gtest//:gtest_main",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)

cc_test(
    name = "dense_bench",
    srcs = ["tests/dense_bench.cpp"],
    deps = [
        ":headers",
        "@yas//:headers",
        "//bench:headers",
    ],
)

cc_test(
    name = "char_test",
    srcs = ["tests/char_test.cpp"],
    deps = [
        ":headers",
    ],
)

