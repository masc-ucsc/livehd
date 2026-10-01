# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# buildifier: disable=load
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_prp_writer",
    srcs = ["lnast_prp_writer.cpp"],
    hdrs = ["lnast_prp_writer.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//lnast",
        # A defaulted comb input's prologue is not the module's hardware.
        "//upass/core:default_prologue_hdr",
        # The Verilog reader's implicit-clock convention (verilog_clock_name).
        "//upass/core:io_port_rules_hdr",
        # The emitter's reserved-word escape set IS the parser's keyword table
        # (prpparse/prp_keywords.def) — see is_pyrope_reserved_ident.
        "@prpparse//:prp_keywords",
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
