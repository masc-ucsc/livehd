# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "lgyosys",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "*test*.cpp",
            "yosys_driver.cpp",
        ],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    defines = [
        "_YOSYS_",
        # "YOSYS_ENABLE_PLUGINS",
        # "YOSYS_ENABLE_READLINE",
        "YOSYS_ENABLE_GLOB",
        #"YOSYS_ENABLE_TCL",
        "YOSYS_ENABLE_ABC",
        "YOSYS_ENABLE_COVER",
        'ABCEXTERNAL=\\"abc\\"',
    ],
    features = ["-use_header_modules"],
    visibility = ["//visibility:public"],
    deps = [
        "//pass/common:pass",
        "@at_clifford_yosys//:kernel",
        "@at_clifford_yosys//:version",
        "@mustache//:headers",
    ],
    #alwayslink=1,
    alwayslink = True,
)

sh_library(
    name = "scripts",
    srcs = [
        "lgcheck",
        ":inou_yosys_scripts",
        ":yosys2",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "verilog_tests",
    srcs = glob([
        "tests/*.v",
    ]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "inou_yosys_scripts",
    srcs = glob([
        "inou_yosys_*.ys",
    ]),
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "yosys2",
    srcs = [
        "yosys_driver.cpp",
    ],
    copts = COPTS,
    features = ["-use_header_modules"],
    linkstatic = True,
    visibility = ["//visibility:public"],
    deps = [
        ":lgyosys",
        "//core",
        #"@dk_thrysoee_libedit//:pretend_to_be_gnu_readline_system",
    ],
)
