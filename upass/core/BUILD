# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# buildifier: disable=load
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# Header-only: the ONE reader of an LNAST array type's `[N]` dim text (see
# array_dim.hpp). Split out like range_bits_hdr so lnast.tolg -- which does not
# link the upass/core plugin -- can share it instead of re-deriving the rule.
cc_library(
    name = "array_dim_hdr",
    hdrs = ["array_dim.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = ["@hlop//hlop:hlop"],
)

cc_library(
    name = "upass",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":array_dim_hdr",
        ":range_bits_hdr",
        "//core",
        "//lnast",
    ],
    alwayslink = True,
)

# Header-only: canonical (max,min)-from-bits reconstruction. Depends only on
# hlop so lightweight consumers (e.g. the wrap_sat_hdr header) can pull it in
# without linking the full upass/core plugin.
cc_library(
    name = "range_bits_hdr",
    hdrs = ["range_bits.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = ["@hlop//hlop:hlop"],
)

cc_test(
    name = "symbol_table_test",
    srcs = ["tests/symbol_table_test.cpp"],
    deps = [
        ":upass",
        "@googletest//:gtest_main",
    ],
)
