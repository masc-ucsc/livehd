# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

filegroup(
    name = "all",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "fmt",
    hdrs = glob(["include/fmt/*.h"]) + glob(["src/*.cc"]),
    copts = [
        "-w",
        "-O2",
    ],
    # WARNING: Yosys dynamic library was giving problems without FMT_HEADER_ONLY
    #defines = ["FMT_HEADER_ONLY"],
    srcs = glob(["src/format.cc"]),
    includes = ["include", "src"],
    visibility = ["//visibility:public"],
    data = glob(["**"]),
)
