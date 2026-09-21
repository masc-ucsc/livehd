# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_prp_writer",
    srcs = ["pass_prp_writer.cpp"],
    hdrs = ["pass_prp_writer.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//core:file_name",
        "//pass/common:pass",
        "//upass/prp_writer:upass_prp_writer",
    ],
    alwayslink = True,
)
