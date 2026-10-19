#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# Folded into //inou/cgen:cgen_salt and //pass/synth:synth_salt: a change here
# changes what both the simulator and the mapped regions see. Tests are not
# part of what they see, so editing one must not invalidate either cache.
filegroup(
    name = "specialize_srcs",
    srcs = glob(
        [
            "*.cpp",
            "*.hpp",
        ],
        exclude = ["*_test.cpp"],
    ),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "specialize",
    srcs = ["specialize.cpp"],
    hdrs = ["specialize.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
        "//pass/bitwidth:pass_bitwidth",
        "//pass/cprop:pass_cprop",
        "@hhds//hhds:graph",
        "@hlop//hlop",
    ],
)

# inherit_colors: the O(V + E) coloring equals the sweep-to-fixpoint reference
# and stays linear on a chain colored from its far end.
cc_test(
    name = "specialize_test",
    srcs = ["specialize_test.cpp"],
    copts = COPTS,
    deps = [
        ":specialize",
        "//graph",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)
