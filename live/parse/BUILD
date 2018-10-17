#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "parse",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [ "//eprp:eprp" ]
)

cc_test(
    name = "chunkify_verilog_test",
    srcs = ["chunkify_verilog_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":parse",
    ],
)

