# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

licenses(["notice"])  # ISC

cc_library(
    name = "viecut",
    # VieCut is header-only apart from mincut.cpp, which is the standalone app
    # entry point (it defines main()). Keeping it in the library makes any
    # consumer that links libviecut pull in that main(), shadowing the
    # consumer's own main (e.g. gtest_main). It belongs only in the cc_binary
    # below, so the library carries headers only.
    srcs = [],
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
