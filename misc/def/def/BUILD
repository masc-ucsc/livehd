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
    #toolchains = ['//tools:clang-local'],
    srcs = [":defyy"] + glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]) + ["lex.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

