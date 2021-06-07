# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "eprp",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//elab",
        "//task",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_test(
    name = "eprp_test",
    srcs = ["tests/eprp_test.cpp"],
    deps = [
        ":eprp",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)
