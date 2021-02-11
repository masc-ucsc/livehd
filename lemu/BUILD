#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "lemu",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    copts = COPTS,
    deps = [
        "@fmt//:fmt",
        "@boost//:multiprecision",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/strings",
        "//lbench:headers",
        "//mmap_lib:headers",
        "@iassert//:iassert",
        ],
    )

cc_test(
    name = "lconst_test",
    srcs = ["tests/lconst_test.cpp"],
    deps = [
        ":lemu",
        "@com_google_googletest//:gtest_main",
        "//simlib:headers",
        ],
    )

cc_binary(
    name = "lconst_dump",
    srcs = ["tests/lconst_dump.cpp"],
    copts = COPTS,
    deps = [
        ":lemu",
        "//simlib:headers",
        "//mmap_lib:headers",
        ],
    )

