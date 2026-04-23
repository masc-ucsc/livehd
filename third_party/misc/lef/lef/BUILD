# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@livehd//packages:yosys_utils.bzl", "yosys_bison")

licenses(["notice"])  # Apache License 2.0

yosys_bison(
    name = "lefyy",
    srcs = ["lef.y"],
    outs = [
        "lef.tab.cpp",
        "lef.tab.hpp",
    ],
    args = [
        "--defines=$(execpath lef.tab.hpp)",
        "--output-file=$(execpath lef.tab.cpp)",
        "$(execpath lef.y)",
    ],
    bison = "@bison//:bison",
)

cc_library(
    name = "lef",
    #toolchains = ['//tools:clang-local'],
    srcs = [":lefyy"] + glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]) + ["lex.h"],
    copts = [
        "-w",
        "-O2",
    ],  # Always fast, no warnings to avoid spurious messages
    includes = ["."],
    visibility = ["//visibility:public"],
)

