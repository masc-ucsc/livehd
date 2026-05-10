# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_ssa",
    srcs = ["upass_ssa.cpp"],
    hdrs = ["upass_ssa.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//lnast",
    ],
)
