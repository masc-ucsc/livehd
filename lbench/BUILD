# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "headers",
    hdrs = glob(["include/*.hpp"]),
    copts = COPTS,
    includes = ["include"],
    visibility = ["//visibility:public"],
)
