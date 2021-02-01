#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "archfp",
    srcs = glob(
        ["*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    includes = ["."],
    visibility = ["//visibility:public"],
    # turning on all warnings because ArchFP has been modified somewhat
    copts = ["-O3", "-Wall", "-Wextra", "-Wunused", "-Wno-overloaded-virtual"],
    deps = [
        "//core:core",
        ],
    )

cc_test(
    name = "floorplan_test",
    srcs = ["tests/archfp_test.cpp"],
    deps = [
        "@com_google_googletest//:gtest_main",
        ":archfp",
        ],
    )

# makefile and other non-source files are kept but not used.