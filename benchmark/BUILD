# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary")
load("//tools:copt_default.bzl", "COPTS")

cc_binary(
    name = "bm_format",
    srcs = ["bm_format.cpp"],
    copts = COPTS,
    deps = [
        "//core",
        "//lgraph",
        "//lnast",
        "//pass/lnast_tolg:pass_lnast_tolg",
        "@google_benchmark//:benchmark",
        "@hif//hif",
    ],
)
