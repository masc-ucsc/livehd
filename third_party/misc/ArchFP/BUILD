# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "archfp",
    srcs = glob(
        ["*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    # turning on all warnings because ArchFP has been modified somewhat
    copts = [
        "-O3",
        "-Wall",
        "-Wextra",
        "-Wunused",
        "-Wno-overloaded-virtual",
    ],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
    ],
)

cc_test(
    name = "ann_test",
    srcs = ["tests/ann_test.cpp"],
    tags = [
        #"fixme",
    ],
    deps = [
        ":archfp",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "floorplan_test",
    srcs = ["tests/archfp_test.cpp"],
    deps = [
        ":archfp",
        "@com_google_googletest//:gtest_main",
    ],
)

# makefile and other non-source files are kept but not used.
