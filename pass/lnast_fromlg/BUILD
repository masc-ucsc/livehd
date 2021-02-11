#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_lnast_fromlg",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    copts = COPTS,
    deps = [
        "//pass/common:pass",
    ]
)

sh_test(
    name = "lgtoln_verif_from_verilog.sh",
    srcs = ["tests/lgtoln_verif_from_verilog.sh"],
    data = [
        "//inou/yosys:verilog_tests",
        "//main:lgshell",
    ],
    deps = [
        "//inou/yosys:scripts",
    ]
)

sh_test(
    name = "lgtoln_verif_from_pyrope.sh",
    srcs = ["tests/lgtoln_verif_from_pyrope.sh"],
    data = [
        "//inou/pyrope:pyrope_tests",
        "//main:lgshell",
    ],
    deps = [
        "//inou/yosys:scripts",
    ]
)
