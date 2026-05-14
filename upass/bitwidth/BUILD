# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# buildifier: disable=load
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_bitwidth",
    srcs = ["upass_bitwidth.cpp"],
    hdrs = [
        "lnast_range.hpp",
        "upass_bitwidth.hpp",
    ],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//lnast",
        "//pass/common:pass",
        "//upass/core:upass",
    ],
    alwayslink = True,  # ensures the static plugin_bitwidth registration fires
)

cc_test(
    name = "upass_bitwidth_test",
    srcs = ["upass_bitwidth_test.cpp"],
    copts = COPTS,
    deps = [
        ":upass_bitwidth",
        "//core",
        "//lnast",
        "//upass/core:upass",
        "//upass/runner:upass_runner",
        "@googletest//:gtest_main",
    ],
)
