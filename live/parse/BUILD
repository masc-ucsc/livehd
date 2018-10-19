#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "chunkify",
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
        ":chunkify",
    ],
)

sh_test(
    name = "chunkify_test.sh",
    srcs = ["tests/chunkify_test.sh"],
    data = [
        "//main:lgshell",
        "//inou/yosys:verilog_tests",
        "//projects/boom:firesim_boom",
        ],
)
