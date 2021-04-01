# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache License 2.0

genrule(
    name = "lefyy",
    srcs = ["lef.y"],
    outs = [
        "lef.tab.cpp",
        "lef.tab.hpp",
    ],
    cmd = "M4=$(M4) $(BISON) --defines=$(location lef.tab.hpp) --output-file=$(location lef.tab.cpp) $<",
    # "--name-prefix=xxx",
    toolchains = [
        "@rules_bison//bison:current_bison_toolchain",
        "@rules_m4//m4:current_m4_toolchain",
    ],
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
