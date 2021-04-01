# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "json",
    hdrs = glob([
        "include/nlohmann/*.hpp",
        "include/nlohmann/**/*.hpp",
    ]),
    copts = [
        "-w",
        "-O2",
    ],
    includes = ["include"],
    visibility = ["//visibility:public"],
)
