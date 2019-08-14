#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "pass_mockturtle",
    srcs = glob(["pass_mockturtle.cpp"]),
    hdrs = glob(["pass_mockturtle.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        "//core:core",
        "@mockturtle//:mockturtle",
        "@fmt//:fmt",
    ]
)

cc_test(
    name = "mock_test",
    srcs = ["mock_test.cpp"],
    deps = [
        "@mockturtle//:mockturtle",
        "@fmt//:fmt",
    ],
)


sh_test(
    name = "mockturtle_test.sh",
    srcs = ["tests/mockturtle_test.sh"],
    data = [
        "//inou/yosys:liblgraph_yosys.so",
        "//main:lgshell",
        "//inou/yosys:verilog_tests"
        ],
    deps = [
        "//inou/yosys:scripts",
        ]
)
