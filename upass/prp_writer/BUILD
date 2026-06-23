# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# buildifier: disable=load
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_prp_writer",
    srcs = ["lnast_prp_writer.cpp"],
    hdrs = ["lnast_prp_writer.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//lnast",
    ],
)

# Slang->Pyrope emit regression for submodule output-port inlining (no LEC, so
# it sidesteps the pre-existing multi-module library-read gaps).
sh_test(
    name = "inst_out_inline_test",
    srcs = ["tests/inst_out_inline_test.py"],
    args = ["-i", "$(location tests/inst_out_inline.v)"],
    data = [
        "tests/inst_out_inline.v",
        "//lhd",
    ],
)

cc_test(
    name = "upass_prp_writer_test",
    srcs = ["lnast_prp_writer_test.cpp"],
    copts = COPTS,
    deps = [
        ":upass_prp_writer",
        "//inou/prp:inou_prp",
        "//upass/attributes:upass_attributes",  # constprop's declared dep (2b/I)
        "//upass/constprop:upass_constprop",
        "//upass/noop:upass_noop",
        "//upass/runner:upass_runner",
        "//upass/typecheck:upass_typecheck",  # constprop's declared dep (2b/I)
        "@googletest//:gtest_main",
    ],
)
