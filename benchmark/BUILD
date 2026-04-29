# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_python//python:defs.bzl", "py_library", "py_test")
load("//tools:copt_default.bzl", "COPTS")

config_setting(
    name = "opt_build",
    values = {"compilation_mode": "opt"},
)

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
    ],
)

py_library(
    name = "constprop_gen",
    srcs = ["constprop_gen.py"],
    imports = ["."],
    visibility = ["//visibility:public"],
)

# Contract: prp parse+upass must outperform yosys read_verilog+opt on a
# constant-propagation synthetic. Auto-runs only when the build is -c opt
# (target_compatible_with skips it under fastbuild/dbg). No "manual" tag —
# intentionally part of the default test set under -c opt.
py_test(
    name = "constprop_contract",
    srcs = ["constprop_contract.py"],
    deps = [":constprop_gen"],
    data = [
        "//main:lgshell",
        "//inou/yosys:yosys2",
    ],
    target_compatible_with = select({
        ":opt_build": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
    tags = ["benchmark"],
)
