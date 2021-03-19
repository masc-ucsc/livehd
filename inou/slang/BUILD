#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_foreign_cc//tools/build_defs:cmake.bzl", "cmake_external")

load("//tools:copt_default.bzl", "COPTS")

cmake_external(
    name = "slang",
    cmake_options = ["-GNinja -DSLANG_INCLUDE_TESTS=OFF"],
    lib_source = "@slang//:all",
    static_libraries = ["libslangruntime.a", "libslangparser.a", "libslangcompiler.a", "libslangcore.a"],
    make_commands = ["ninja", # libslangruntime.a libslangparser.a libslangcompiler.a libslangcore.a",
                     "ninja install" ],
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
