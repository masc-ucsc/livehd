# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_ssa",
    srcs = ["upass_ssa.cpp"],
    hdrs = ["upass_ssa.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//lnast",
        "//upass/core:array_dim_hdr",
        "//upass/core:range_bits_hdr",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
)

cc_test(
    name = "upass_ssa_test",
    srcs = ["upass_ssa_test.cpp"],
    copts = COPTS,
    deps = [
        ":upass_ssa",
        "//lnast",
        "@googletest//:gtest_main",
    ],
)
