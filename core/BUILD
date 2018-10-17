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
    deps = [
        "@gtest//:gtest_main",
        ":core",
    ],
)
