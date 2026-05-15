# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_attributes",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//lnast",
        "//upass/bitwidth:wrap_sat_hdr",
        "//upass/core:upass",
    ],
    alwayslink = True,
)

# Skeleton placeholder; tests will be added as Phase 1 logic lands.
# cc_test(
#     name = "upass_attributes_test",
#     srcs = ["upass_attributes_test.cpp"],
#     copts = COPTS,
#     deps = [
#         ":upass_attributes",
#         "@googletest//:gtest_main",
#     ],
# )
