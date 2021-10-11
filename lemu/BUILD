# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "lemu",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//task",
        "//mmap_lib:headers",
        "@boost//:multiprecision",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@fmt",
        "@iassert",
    ],
)

cc_test(
    name = "lconst_test",
    srcs = ["tests/lconst_test.cpp"],
    deps = [
        ":lemu",
        "//simlib:headers",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "blop_test",
    srcs = ["tests/blop_test.cpp"],
    deps = [
        ":lemu",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_binary(
    name = "lconst_dump",
    srcs = ["tests/lconst_dump.cpp"],
    copts = COPTS,
    deps = [
        ":lemu",
        "//mmap_lib:headers",
        "//simlib:headers",
    ],
)
