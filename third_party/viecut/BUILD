# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

licenses(["notice"])  # ISC

cc_library(
    name = "viecut",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "test*",
        ],
    ),
    hdrs = glob(["**/*.h"]),
    copts = [
        "-w",
        "-O2",
    ],  # Always fast, no warnings to avoid spurious messages
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
    ],
)

cc_binary(
    name = "mincut",
    srcs = ["mincut.cpp"],
    copts = [
        "-w",
        "-O2",
    ],
    deps = [
        ":viecut",
    ],
)
