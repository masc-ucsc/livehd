# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_mockturtle",
    srcs = ["pass_mockturtle.cpp"],
    hdrs = ["pass_mockturtle.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//pass/common:pass",
        "@fmt",
        "@mockturtle",
    ],
    alwayslink = True,
)

cc_test(
    name = "mock_test",
    srcs = ["mock_test.cpp"],
    deps = [
        "//lbench:headers",
        "@fmt",
        "@mockturtle",
    ],
)

sh_test(
    name = "mt_test.sh",
    srcs = ["tests/mt_test.sh"],
    data = [
        "//inou/yosys:verilog_tests",
        "//main:lgshell",
    ],
    tags = ["fixme"],
    deps = [
        "//inou/yosys:scripts",
    ],
)
