# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "lsp",
    srcs = ["livehd_lsp.cpp"],
    hdrs = ["livehd_lsp.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//lnast",
        "//inou/prp:inou_prp",
        "//pass/common:pass",
        "//pass/lnastfmt:pass_lnastfmt",
        "//pass/upass:pass_upass",
        "@rapidjson",
    ],
)
