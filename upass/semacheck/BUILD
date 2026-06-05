# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# buildifier: disable=load
load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_semacheck",
    srcs = ["upass_semacheck.cpp"],
    hdrs = ["upass_semacheck.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//lnast",
        "//upass/core:upass",
    ],
    alwayslink = True,  # ensures the static plugin_semacheck registration fires
)
