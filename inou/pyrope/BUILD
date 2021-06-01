# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "inou_pyrope",
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

filegroup(
    name = "pyrope_tests",
    srcs = glob([
        "tests/*.prp",
        "tests/compiler/*.prp",
        "tests/compiler/verilog_gld/*.v",
    ]),
    visibility = ["//visibility:public"],
)

sh_test(
    name = "pyrope_test.sh",
    srcs = ["tests/pyrope_test.sh"],
    data = [
        ":pyrope_tests",
        "//inou/yosys:verilog_tests",
        "//main:lgshell",
    ],
    tags = ["fixme"],
)

cc_binary(
    name = "prp_test",
    srcs = ["tests/prp_test.cpp"],
    copts = COPTS,
    deps = [
        ":inou_pyrope",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_binary(
    name = "prp_lnast_test",
    srcs = ["tests/prp_lnast_test.cpp"],
    copts = COPTS,
    deps = [
        ":inou_pyrope",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "prp_unit_test",
    srcs = ["tests/prp_unit_test.cpp"],
    tags = [
        "fixme",
    ],
    deps = [
        ":inou_pyrope",
        "@com_google_googletest//:gtest_main",
    ],
)
