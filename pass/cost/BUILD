# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# Host memory facts for ABC memory admission (pass/abc). Deliberately
# dependency-free (no //graph, no @abc) and parked outside pass/abc: pass.color
# --stats is meant to warn with the same budget, and pass/abc already depends on
# pass/partition which depends on pass/color -- so anything those two end up
# sharing must sit BELOW all three or it is a dependency cycle.
cc_library(
    name = "host_mem",
    srcs = ["host_mem.cpp"],
    hdrs = ["host_mem.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "host_mem_test",
    srcs = ["host_mem_test.cpp"],
    copts = COPTS,
    deps = [
        ":host_mem",
        "@googletest//:gtest_main",
    ],
)
