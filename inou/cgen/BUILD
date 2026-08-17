# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "inou_cgen",
    srcs = glob(
        ["*.cpp"],
        exclude = ["*_test.cpp"],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//graph",
        "//pass/common:pass",
        "@llvm-project//llvm:CodeGen",
        "@llvm-project//llvm:Core",
        "@llvm-project//llvm:MC",
        "@llvm-project//llvm:Passes",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:Target",
        "@llvm-project//llvm:TargetParser",
    ] + select({
        "@platforms//cpu:aarch64": ["@llvm-project//llvm:AArch64CodeGen"],
        "@platforms//cpu:x86_64": ["@llvm-project//llvm:X86CodeGen"],
        "//conditions:default": ["@llvm-project//llvm:AllTargetsCodeGens"],
    }),
    alwayslink = True,
)

cc_test(
    name = "cgen_llvm_test",
    srcs = ["cgen_llvm_test.cpp"],
    copts = COPTS,
    deps = [
        ":inou_cgen",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sim_color_plan_test",
    srcs = ["sim_color_plan_test.cpp"],
    copts = COPTS,
    deps = [
        ":inou_cgen",
        "//graph",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)
