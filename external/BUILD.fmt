# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fmt",
    hdrs = glob(["include/fmt/*.h"]),
    copts = [
        "-w",
        "-O2",
    ],
    # WARNING: Yosys dynamic library was giving problems without FMT_HEADER_ONLY
    #defines = ["FMT_HEADER_ONLY"],
    srcs = glob(["src/*.cc"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)
