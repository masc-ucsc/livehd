#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "pass_dce",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta",
    ]
)

filegroup(
    name = "verilog_tests",
    srcs = glob([
        "tests/*.v",
    ]),
    visibility = ["//visibility:public"],
)

sh_test(
    name = "dce.sh",
    srcs = ["tests/dce.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//main:lgshell",
        "//inou/yosys:scripts",
        ":verilog_tests",
        ],
    deps = [
        "//inou/yosys:scripts",
    ]
)

