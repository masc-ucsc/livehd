#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_mockturtle",
    srcs = glob(["pass_mockturtle.cpp"]),
    hdrs = glob(["pass_mockturtle.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    copts = COPTS,
    deps = [
        "//pass/common:pass",
        "@mockturtle//:mockturtle",
        "@fmt//:fmt",
    ]
)

cc_test(
    name = "mock_test",
    srcs = ["mock_test.cpp"],
    deps = [
        "@mockturtle//:mockturtle",
        "//lbench:headers",
        "@fmt//:fmt",
    ],
)


sh_test(
    name = "mt_test.sh",
    srcs = ["tests/mt_test.sh"],
    data = [
        "//main:lgshell",
        "//inou/yosys:verilog_tests"
        ],
    deps = [
        "//inou/yosys:scripts",
        ]
)
