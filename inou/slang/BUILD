#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

load("//tools:copt_default.bzl", "COPTS")

cmake(
    name = "slang",
    generate_args = ["-GNinja"],
    lib_source = "@slang//:all",
    out_static_libs = ["libslangruntime.a", "libslangparser.a", "libslangcompiler.a", "libslangcore.a"],
)

cc_library(
    name = "inou_slang",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    copts = COPTS,
    deps = [
        "//pass/common:pass",
        ":slang",
    ],
)

cc_binary(
    name = "rewriter",
    srcs = ["tests/rewriter.cpp"],
    copts = COPTS,
    deps = [
        ":inou_slang",
        ],
    )

cc_binary(
    name = "driver",
    srcs = ["tests/driver.cpp"],
    copts = COPTS,
    deps = [
        ":inou_slang",
        ],
    )
