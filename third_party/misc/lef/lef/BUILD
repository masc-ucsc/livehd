#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
licenses(["notice"]) # Apache License 2.0

load("//tools:lexyacc.bzl", "genlex", "genyacc")

genyacc(
    name = "lefyy",
    srcs = ["lef.y"],
    outs = [
        "lef.tab.cpp",
        "lef.tab.hpp",
     #   "location.hh",
     #   "stack.hh",
     #   "position.hh",
    ],
)

cc_library(
    name = "lef",
    #toolchains = ['//tools:clang-local'],
    srcs = [":lefyy"] + glob(["*.cpp"]),
    includes = ["."],
    hdrs = glob(["*.hpp"]) + ["lex.h"],
    visibility = ["//visibility:public"],
)

