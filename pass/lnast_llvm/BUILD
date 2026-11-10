# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "lnast_llvm",
    srcs = [
        "lnast_llvm.cpp",
        "lnast_sim.cpp",
    ],
    hdrs = [
        "lnast_llvm.hpp",
        "lnast_sim.hpp",
        "lnast_sim_rt.hpp",
    ],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//core:file_name",
        "//graph",
        "//inou/cgen:inou_cgen",
        "//lnast",
        "//pass/lg2lnast:pass_lg2lnast",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
)

# Runs a generated kernel.o against a deterministic stimulus and prints one
# `TRACE` line per cycle (the same lines the Pyrope testbench prints).
cc_binary(
    name = "lnast_llvm_run",
    srcs = ["lnast_llvm_run.cpp"],
    copts = COPTS,
    visibility = ["//visibility:public"],
    deps = ["//inou/cgen:sim_native_rt"],
)

load("//tools:regression.bzl", "sh_test")

sh_test(
    name = "lnast_llvm_diff_test",
    size = "medium",
    srcs = ["tests/lnast_llvm_diff_test.sh"],
    data = glob(["tests/*.sv", "tests/*.prp", "tests/gen_tb.py"]) + ["//lhd", ":lnast_llvm_run"],
)
