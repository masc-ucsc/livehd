#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//:copt_default.bzl", "COPTS")

cc_library(
    name = "task",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    linkopts= ["-lpthread"],
    includes = ["."],
    copts = COPTS,
)

cc_test(
    name = "thread_pool_test",
    srcs = ["tests/thread_pool_test.cpp", "tests/concurrentqueue.hpp"],
    # tags = ["long1"], # Run only with long1 set of tests
    deps = [
        "@com_google_googletest//:gtest_main",
        "//lbench:headers",
        ":task",
    ],
)

