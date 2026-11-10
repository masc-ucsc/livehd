# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_lg2lnast",
    srcs = ["pass_lg2lnast.cpp"],
    hdrs = ["pass_lg2lnast.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
        "//lnast",
        "//pass/common:pass",
        "@abseil-cpp//absl/container:flat_hash_map",
    ],
    alwayslink = True,
)

load("//tools:regression.bzl", "sh_test")

sh_test(
    name = "lg2lnast_roundtrip_test",
    size = "medium",
    srcs = ["tests/lg2lnast_roundtrip_test.sh"],
    data = glob(["tests/*.sv", "tests/*.prp"]) + ["//lhd"],
)
