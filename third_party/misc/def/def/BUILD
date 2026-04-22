# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@livehd//packages:yosys_utils.bzl", "yosys_bison")

licenses(["notice"])  # Apache License 2.0

yosys_bison(
    name = "defyy",
    srcs = ["def.yy"],
    outs = [
        "def.tab.cpp",
        "def.tab.hpp",
    ],
    args = [
        "--defines=$(execpath def.tab.hpp)",
        "--output-file=$(execpath def.tab.cpp)",
        "$(execpath def.yy)",
    ],
    bison = "@bison//:bison",
)

cc_library(
    name = "def",
    srcs = [":defyy"] + glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]) + ["lex.h"],
    copts = [
        "-w",
        "-O2",
    ],  # Always fast, no warnings to avoid spurious messages
    includes = ["."],
    visibility = ["//visibility:public"],
)

