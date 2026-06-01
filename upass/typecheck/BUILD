# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# buildifier: disable=load
load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_typecheck",
    srcs = ["upass_typecheck.cpp"],
    hdrs = ["upass_typecheck.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//lnast",
        "//upass/core:upass",
        "@abseil-cpp//absl/container:flat_hash_map",
    ],
    alwayslink = True,  # ensures the static plugin_typecheck registration fires
)
