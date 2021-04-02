# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "inou_graphviz",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    data = [
        "//inou/pyrope:pyrope_tests",
    ],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//inou/firrtl:inou_firrtl_cpp",
        "//pass/common:pass",
    ],
    alwayslink = True,
)
