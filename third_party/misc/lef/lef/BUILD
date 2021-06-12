# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_hdl//dependency_support/org_gnu_bison:bison.bzl", "genyacc")

licenses(["notice"])  # Apache License 2.0

genyacc(
    name = "lef_y",
    src = "lef.y",
    header_out = "lef.tab.hpp",
    source_out = "lef.tab.cpp",
)

cc_library(
    name = "lef",
    #toolchains = ['//tools:clang-local'],
    srcs = [":lef_y"] + glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]) + ["lex.h"],
    copts = [
        "-w",
        "-O2",
    ],  # Always fast, no warnings to avoid spurious messages
    includes = ["."],
    visibility = ["//visibility:public"],
)
