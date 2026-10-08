# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_isabelle",
    srcs = [
        "pass_isabelle.cpp",
    ],
    hdrs = [
        "pass_isabelle.hpp",
    ],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
        "//pass/common:pass",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@hhds//hhds:graph",
    ],
    alwayslink = True,
)

cc_test(
    name = "emission_smoke",
    srcs = ["tests/emission_smoke.cpp"],
    copts = COPTS,
    deps = [
        ":pass_isabelle",
        "//graph",
        "@googletest//:gtest_main",
    ],
)
