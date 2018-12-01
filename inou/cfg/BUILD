#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "inou_cfg",
    srcs = glob(["*.cpp"], exclude=["lgcfg.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//meta:meta",
    ],
)

filegroup(
    name = "cfg_pts",
    srcs = glob(["tests/*.cfg", "tests/verilog_gld/*.v"]),
    visibility = ["//visibility:public"],
)

sh_test(
    name = "pyrope.sh",
    srcs = ["tests/pyrope.sh"],
    tags = ["manual"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//main:lgshell",
        ":cfg_pts"
        ],
    deps = [
        "//inou/yosys:scripts",
    ]
)



