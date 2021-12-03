# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_submatch",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//pass/common:pass",
    ],
    alwayslink = True,
)

sh_test(
    name = "basic-test",
    srcs = ["tests/basic_test.sh"],
    data = [
        "//main:lgshell",
        "//pass/cprop:pass_cprop",
        "//pass/lnast_tolg:pass_lnast_tolg",
        "//inou/yosys/tests:verilog_tests",
    ]
)
