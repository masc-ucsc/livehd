# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_foreign_cc//tools/build_defs:cmake.bzl", "cmake_external")
load("//tools:copt_default.bzl", "COPTS")

cmake_external(
    name = "slang",
    #cmake_options = ["-GNinja -DSLANG_INCLUDE_TESTS=OFF -DCMAKE_C_FLAGS=-fPIC -DCMAKE_CXX_FLAGS=-fPIC"],
    cmake_options = ["-GNinja -DSLANG_INCLUDE_TESTS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON"],
    lib_source = "@slang//:all",
    make_commands = [
        "ninja",  # libslangruntime.a libslangparser.a libslangcompiler.a libslangcore.a",
        "ninja install",
    ],
    # defines = [ "DEBUG" ],
    static_libraries = [
        "libslangruntime.a",
        "libslangparser.a",
        "libslangcompiler.a",
        "libslangcore.a",
    ],
)

cc_library(
    name = "inou_slang",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":slang",
        "//pass/common:pass",
    ],
    alwayslink = True,  # Needed to have constructor called
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
