# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_color",
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
        "//graph",
        "//pass/liberty:liberty_dff",
        # Virtual flattening: the synth algorithms color the FLAT view of the
        # hierarchy so a region can cross module boundaries.
        "//pass/partition:flatten",
        "//pass/common:pass",
        "//pass/synth:affine_amount",
        "//third_party/viecut",
        "@rapidjson",
    ],
    alwayslink = True,  # static Pass_plugin registration
)

cc_test(
    name = "color_test",
    srcs = [
        "color_acyclic_test.cpp",
        "color_cgen_test.cpp",
        "color_flat_test.cpp",
        "color_mincut_test.cpp",
        "color_path_test.cpp",
        "color_reduce_test.cpp",
        "color_size_test.cpp",
        "color_synth_cones_test.cpp",
        "color_synth_test.cpp",
    ],
    copts = COPTS,
    deps = [
        ":pass_color",
        "//core",
        "//graph",
        "@googletest//:gtest_main",
    ],
)
