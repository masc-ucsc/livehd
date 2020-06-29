#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "inou_liveparse",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//elab:elab",
        "//pass/common:pass",
    ]
)

cc_test(
    name = "chunkify_verilog_test",
    srcs = ["chunkify_verilog_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":inou_liveparse",
    ],
)

sh_test(
    name = "chunkify_test.sh",
    tags = ["fixme"],
    srcs = ["tests/chunkify_test.sh"],
    data = [
        "//main:lgshell",
        "//inou/yosys:verilog_tests",
        "//projects/boom:firesim_boom",
        "//tests/benchmarks/boom:boombase",
        ],
)
