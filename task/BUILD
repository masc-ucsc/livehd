#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "task",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    linkopts= ["-lpthread"],
    includes = ["."],
)

cc_test(
    name = "thread_pool_test",
    srcs = ["tests/thread_pool_test.cpp"],
    # tags = ["long1"], # Run only with long1 set of tests
    deps = [
        "@gtest//:gtest_main",
        ":task",
    ],
)

