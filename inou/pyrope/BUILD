#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "inou_pyrope",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//core:core"
    ],
)

filegroup(
    name = "pyrope_tests",
    srcs = glob([
        "tests/*.prp",
    ]),
    visibility = ["//visibility:public"],
)

sh_test(
    name = "pyrope_test.sh",
    srcs = ["tests/pyrope_test.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//main:lgshell",
        ":pyrope_tests",
        "//inou/yosys:verilog_tests",
        ],
)

cc_binary(
    name = "prp_test",
    srcs = ["prp_test.cpp"],
    deps = [
        "@gtest//:gtest_main",
        ":inou_pyrope",
    ],
)
