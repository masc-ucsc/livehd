# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "tree-sitter-pyrope",
    hdrs = glob(["src/tree_sitter/*.h"]),
    copts = [
        "-w",
        "-O2",
    ],
    srcs = glob(["src/*.c"]),
    includes = ["src/tree_sitter"],
    visibility = ["//visibility:public"],
    deps = ["@tree-sitter//:tree-sitter" ],
)

