# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "lnast",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob([
        "*.hpp",
        "*.def",
    ]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "@abseil-cpp//absl/strings",
        "@hhds//hhds:core",
    ],
)

cc_test(
    name = "lnast_bench",
    srcs = ["tests/lnast_bench.cpp"],
    deps = [
        ":lnast",
        "@google_benchmark//:benchmark",
    ],
)

