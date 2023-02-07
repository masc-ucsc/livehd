# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_upass",
    srcs = glob(
        ["*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//lnast",
        "//pass/common:pass",
        "//upass/core:upass",
        "//upass/runner:upass_runner",
        "//upass/verifier:upass_verifier",
        "//upass/constprop:upass_constprop",
        "//upass/assert:upass_assert",
    ],
    alwayslink = True,  # Needed to have constructor called
)
