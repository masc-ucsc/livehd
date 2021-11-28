# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "replxx",
    srcs = glob([
        "src/*.cxx",
        "src/*.cpp",
    ]),
    hdrs = glob([
        "src/*.hxx",
        "src/*.h",
        "include/*.hxx",
        "include/*.h",
    ]),
    copts = [
        "-w",
        "-O2",
    ],  # disable turning warnings into errors for external lib
    includes = ["include"],  # Needed because some includes use <foo.h>
    visibility = ["//visibility:public"],
)
