#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
licenses(["notice"]) # Apache License 2.0

load("//tools:lexyacc.bzl", "genlex", "genyacc")

genyacc(
    name = "defyy",
    srcs = ["def.y"],
    outs = [
        "def.tab.cpp",
        "def.tab.hpp",
     #   "location.hh",
     #   "stack.hh",
     #   "position.hh",
    ],
)

cc_library(
    name = "def",
    copts = ["-w","-O2"],  # Always fast, no warnings to avoid spurious messages
    srcs = [":defyy"] + glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]) + ["lex.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

