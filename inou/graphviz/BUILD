#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "inou_graphviz",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    copts = COPTS,
    deps = [
        "//inou/firrtl:inou_firrtl_cpp",
        "//pass/common:pass",
    ],
    data = [
        "//inou/pyrope:pyrope_tests",
    ],
)

