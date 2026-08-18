# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
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

cc_library(
    name = "pass_partition",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*test*.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//graph",
        "//pass/color:pass_color",
        "//pass/common:pass",
    ],
    alwayslink = True,  # static Pass_plugin registration
)
