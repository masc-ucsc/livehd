#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "pass_sample",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta",
        "//core:core",
    ]
)

sh_test(
    name = "sample_test1.sh",
    srcs = ["tests/sample_test1.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//main:lgshell",
        "//inou/yosys:verilog_tests",
        ],
)

cc_test(
    name = "sample_test2",
    srcs = ["sample_test2.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":pass_sample",
    ],
)
