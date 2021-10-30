# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "elab",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    linkopts = ["-lpthread"],
    visibility = ["//visibility:public"],
    deps = [
        "//task",
        "//lemu",
        "//mmap_lib:headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@fmt",
        "@iassert",
    ],
)

filegroup(
    name = "pyrope_test",
    srcs = ["tests/synth1.eprp"],
)

filegroup(
    name = "verilog_test",
    srcs = ["tests/hierarchy.v"],
)

cc_test(
    name = "ast_test",
    srcs = ["tests/ast_test.cpp"],
    deps = [
        ":elab",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "symbol_table_test",
    srcs = ["tests/symbol_table_test.cpp"],
    deps = [
        ":elab",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "ast_test2",
    srcs = ["tests/ast_test2.cpp"],
    deps = [
        ":elab",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "elab_scanner_test",
    srcs = ["tests/elab_scanner_test.cpp"],
    args = ["$(location :pyrope_test)"],
    data = [
        ":pyrope_test",
    ],
    deps = [
        ":elab",
    ],
)

cc_test(
    name = "elab_scanner_unit_test",
    srcs = ["tests/elab_scanner_unit_test.cpp"],
    tags = ["fixme"],
    deps = [
        ":elab",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "lnast_bench",
    srcs = ["tests/lnast_bench.cpp"],
    deps = [
        ":elab",
        "@com_google_benchmark//:benchmark",
    ],
)

cc_test(
    name = "elab_verilog_test",
    srcs = ["tests/elab_verilog_test.cpp"],
    args = ["$(location :verilog_test)"],
    data = [
        ":verilog_test",
    ],
    deps = [
        ":elab",
    ],
)

cc_test(
    name = "elab_xlang_test",
    srcs = ["tests/elab_xlang_test.cpp"],
    deps = [
        ":elab",
    ],
)
