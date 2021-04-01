# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

licenses(["notice"])  # ISC

cc_library(
    name = "ezsat",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "demo*",
            "test*",
            "sat_lima*",
            "puzzle*",
        ],
    ),
    hdrs = glob(["*.hpp"]),
    copts = [
        "-w",
        "-O2",
    ],  # Always fast, no warnings to avoid spurious messages
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@cryptominisat",
    ],
)

cc_test(
    name = "testbench",
    srcs = ["testbench.cpp"],
    copts = [
        "-w",
        "-O2",
    ],
    deps = [
        ":ezsat",
    ],
)
