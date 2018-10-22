#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "core",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    linkopts = ['-lpthread'],
    deps = ["@sparsehash//:headers",
            "@bm//:headers",
            "@rapidjson//:headers",
            "@spdlog//:headers",
            ],
)

cc_test(
    name = "thread_pool_test",
    srcs = ["thread_pool_test.cpp"],
    tags = ["long1"], # Run only with long1 set of tests
    deps = [
        "@gtest//:gtest_main",
        ":core",
    ],
)

cc_test(
    name = "dense_test",
    srcs = ["tests/dense_tst.cpp"],
    deps = [
        ":core",
    ],
)

cc_test(
    name = "char_test",
    srcs = ["tests/char_tst.cpp"],
    deps = [
        ":core",
    ],
)

cc_test(
    name = "edge_test",
    srcs = ["tests/edge.cpp"],
    deps = [
        ":core",
        "//meta:meta",
    ],
)

cc_test(
    name = "mmap_test",
    srcs = ["tests/mmap_allocator.cpp"],
    deps = [
        ":core",
    ],
)

cc_test(
    name = "iter_test",
    srcs = ["tests/iter.cpp"],
    deps = [
        ":core",
        "//meta:meta",
    ],
)
