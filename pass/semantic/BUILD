#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_semantic",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    alwayslink=True,
    copts = COPTS,
    deps = [
        "//pass/common:pass",
        "//elab:elab",
        "@com_google_absl//absl/container:flat_hash_set",
        "//inou/pyrope:inou_pyrope",
        "//lbench:headers",
    ]
)

cc_library(
    name = "semantic_check",
    srcs = glob(["*.cpp"],exclude=["*test*.cpp", "pass*"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    copts = COPTS,
    deps = [
        "//pass/common:pass",
        "//elab:elab",
        "@com_google_absl//absl/container:flat_hash_set",
        "//inou/pyrope:inou_pyrope",
    ]
)

cc_test(
    name = "lnast_semantic_test",
    srcs = ["tests/lnast_semantic_test.cpp",],
    deps = [
        ":semantic_check",
        ],
    )