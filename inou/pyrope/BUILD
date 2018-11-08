#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "inou_pyrope",
    srcs = ["inou_pyrope.cpp"],
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta"
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
