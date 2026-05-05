# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# buildifier: disable=load
load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_lnast_to_lgraph",
    srcs = ["lnast_to_lgraph.cpp"],
    hdrs = ["lnast_to_lgraph.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//lgraph",
        "//lnast",
    ],
)
