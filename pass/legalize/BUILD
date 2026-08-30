#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "legalize",
    srcs = ["legalize.cpp"],
    hdrs = ["legalize.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
        "//pass/semdiff",
        "@hhds//hhds:graph",
        "@hlop//hlop",
    ],
)

cc_test(
    name = "legalize_test",
    srcs = ["tests/legalize_test.cpp"],
    copts = COPTS,
    linkopts = ["-lz"],  # via //pass/lec:lec_query -> @abc (zlib-backed readers)
    deps = [
        ":legalize",
        "//pass/lec:lec_query",
        "//pass/semdiff",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)
