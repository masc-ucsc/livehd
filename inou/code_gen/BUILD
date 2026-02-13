# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "inou_code_gen",
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
    name = "prp_v_LEC_test.sh",
    srcs = ["tests/prp_v_LEC_test.sh"],
    data = [
        "//inou/pyrope:pyrope_tests2",
        "//main:lgshell",
    ],
    tags = ["fixme"],
    deps = [
        "//inou/yosys:scripts",
    ],
)
