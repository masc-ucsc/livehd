# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_tolg",
    srcs = ["upass_tolg.cpp"],
    hdrs = ["upass_tolg.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
        "//lnast",
        "//upass/core:array_dim_hdr",
        "//upass/core:default_prologue_hdr",
        "//upass/core:io_port_rules_hdr",
        "//upass/core:range_bits_hdr",
        "//pass/common:pass",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "upass_tolg_test",
    srcs = ["upass_tolg_test.cpp"],
    copts = COPTS,
    deps = [
        ":upass_tolg",
        "//core",
        "//graph",
        "//lnast",
        "@googletest//:gtest_main",
    ],
)
