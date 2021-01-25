#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//:copt_default.bzl", "COPTS")

cc_library(
    name = "inou_pyrope",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    copts = COPTS,
    deps = [
        "//pass/common:pass",
    ],
)

filegroup(
    name = "pyrope_tests",
    srcs = glob([
        "tests/*.prp", "tests/compiler/*.prp", "tests/compiler/verilog_gld/*.v"
    ]),
    visibility = ["//visibility:public"],
)

sh_test(
    name = "pyrope_test.sh",
    tags = ["fixme"],
    srcs = ["tests/pyrope_test.sh"],
    data = [
        "//main:lgshell",
        ":pyrope_tests",
        "//inou/yosys:verilog_tests",
        ],
)

cc_binary(
    name = "prp_test",
    srcs = ["tests/prp_test.cpp"],
    copts = COPTS,
    deps = [
        "@com_google_googletest//:gtest_main",
        ":inou_pyrope",
    ],
)

cc_binary(
    name = "prp_lnast_test",
    srcs = ["tests/prp_lnast_test.cpp"],
    copts = COPTS,
    deps = [
        "@com_google_googletest//:gtest_main",
        ":inou_pyrope",
    ],
)

cc_test(
    name = "prp_unit_test",
    srcs = ["tests/prp_unit_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        ":inou_pyrope",
    ],
)
