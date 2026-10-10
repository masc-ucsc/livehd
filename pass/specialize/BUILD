#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

# Folded into //inou/cgen:cgen_salt and //pass/synth:synth_salt: a change here
# changes what both the simulator and the mapped regions see.
filegroup(
    name = "specialize_srcs",
    srcs = glob([
        "*.cpp",
        "*.hpp",
    ]),
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
