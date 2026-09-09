# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# Hashed into //pass/abc:abc_salt: the partitioner decides region boundaries and
# boundary port names, which is exactly what the ABC incremental cache keys on,
# so a change here must invalidate cached mapped bodies (docs/opt_loop_incr.md
# L2, finding F8).
filegroup(
    name = "partition_srcs",
    srcs = glob([
        "*.cpp",
        "*.hpp",
    ]),
    visibility = ["//visibility:public"],
)

# The hierarchy flattener, as a LEAF library. pass.color needs it (virtual
# flattening: cones colors the flat view so its regions can cross module
# boundaries) and pass.partition already depends on pass.color, so this cannot
# live in :pass_partition without a dependency cycle.
cc_library(
    name = "flatten",
    srcs = ["flatten.cpp"],
    hdrs = ["flatten.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
    ],
)

cc_library(
    name = "pass_partition",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "*test*.cpp",
            "flatten.cpp",
        ],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":flatten",
        "//graph",
        "//pass/color:pass_color",
        "//pass/common:pass",
    ],
    alwayslink = True,  # static Pass_plugin registration
)

cc_test(
    name = "pass_partition_test",
    srcs = ["pass_partition_test.cpp"],
    copts = COPTS,
    deps = [
        ":pass_partition",
        "//graph",
        "@googletest//:gtest_main",
    ],
)
