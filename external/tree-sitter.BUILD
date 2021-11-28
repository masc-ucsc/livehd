
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "tree-sitter",
    hdrs = glob(["lib/include/tree_sitter/*.h", "lib/src/unicode/*.h", "lib/src/*.h"]),
    copts = [
        "-w",
        "-O2",
    ],
    srcs = glob(["lib/src/*.c"], exclude = ["lib/src/lib.c"]),
    includes = ["lib/include", "lib/src"],
    visibility = ["//visibility:public"],
)

