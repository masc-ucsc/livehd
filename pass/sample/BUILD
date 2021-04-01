# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_sample",
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
    alwayslink = True,  # Needed to have constructor called
)

sh_test(
    name = "sample_test1.sh",
    srcs = ["tests/sample_test1.sh"],
    data = [
        "//inou/yosys:verilog_tests",
        "//main:lgshell",
    ],
    tags = ["fixme"],
)

cc_test(
    name = "sample_test2",
    srcs = ["sample_test2.cpp"],
    # tags = [ "fixme", ],
    deps = [
        ":pass_sample",
        "@com_google_googletest//:gtest_main",
    ],
)
