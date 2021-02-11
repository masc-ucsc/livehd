#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_lnast_tolg",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    copts = COPTS,
    deps = [
        "//pass/common:pass",
        "//pass/cprop:pass_cprop",
    ],
)


