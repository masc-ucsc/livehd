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
    visibility = ["//visibility:public"],
    deps = [
        "//pass/common:pass",
        "@at_clifford_yosys//:kernel",
        "@at_clifford_yosys//:version",
        "@mustache//:headers",
    ],
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
