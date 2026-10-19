#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# Folded into //lhd:compile_salt: a change here changes the compiled graphs.
filegroup(
    name = "sweep_srcs",
    srcs = glob(
        [
            "*.cpp",
            "*.hpp",
        ],
        exclude = [
            "*_smoke.cpp",
            "*_test.cpp",
        ],
    ),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "sweep",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "*_smoke.cpp",
            "*_test.cpp",
        ],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
        "//pass/synth:affine_amount",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@hhds//hhds:graph",
        "@hlop//hlop",
    ],
)

cc_test(
    name = "sweep_smoke",
    srcs = ["sweep_smoke.cpp"],
    copts = COPTS,
    deps = [
        ":sweep",
        "//graph",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)
