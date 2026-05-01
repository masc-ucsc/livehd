# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_prp_writer",
    srcs = ["pass_prp_writer.cpp"],
    hdrs = ["pass_prp_writer.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//pass/common:pass",
        "//upass/prp_writer:upass_prp_writer",
    ],
    alwayslink = True,
)

sh_test(
    name = "prp_writer_roundtrip_test.sh",
    srcs = ["tests/prp_writer_roundtrip_test.sh"],
    data = [
        "//inou/prp:tests/comptime/trivial_if.prp",
        "//main:lgshell",
    ],
)
