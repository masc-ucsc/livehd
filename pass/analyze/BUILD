# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# The analysis ENGINE, with no plugin registration, so unit tests can drive it
# on a hand-built graph without pulling in the CLI.
cc_library(
    name = "analyze",
    srcs = ["analyze.cpp"],
    hdrs = ["analyze.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//graph",
        "//pass/color:pass_color",
    ],
)

cc_library(
    name = "pass_analyze",
    srcs = ["pass_analyze.cpp"],
    hdrs = ["pass_analyze.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":analyze",
        "//core",
        "//pass/common:pass",
    ],
    alwayslink = True,  # static Pass_plugin registration
)

cc_test(
    name = "analyze_test",
    srcs = ["analyze_test.cpp"],
    copts = COPTS,
    deps = [
        ":analyze",
        "//core",
        "//graph",
        "@googletest//:gtest_main",
    ],
)
