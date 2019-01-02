#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "meta",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = ["//core:core",
            ],
)

cc_test(
    name = "lgraph_each",
    srcs = ["tests/lgraph_each_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":meta",
    ],
)

