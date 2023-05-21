# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

filegroup(
    name = "all",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "mimalloc",
    srcs = ["src/static.c"],
    textual_hdrs = glob(["src/**"], exclude = ["src/static.c"]),
    hdrs = glob(["include/**"]),
    includes = ["include"],
    copts = ["-DMI_MALLOC_OVERRIDE"],
    visibility = ["//visibility:public"],
)

