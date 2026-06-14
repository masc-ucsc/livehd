# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

# L0 encoder (LGraph -> smt-switch terms) + L1 relational query (prove_equal,
# discharged through Pono). The reusable engine core; the pass.lec plugin (L3)
# layers on top.
cc_library(
    name = "lec_query",
    srcs = [
        "encode.cpp",
        "query.cpp",
    ],
    hdrs = [
        "encode.hpp",
        "query.hpp",
    ],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//graph",
        "@hhds//hhds:graph",
        "@pono",
    ],
)

# L3 plugin: pass.lec (the lec.* set-option registry + lhd lec entry point).
cc_library(
    name = "pass_lec",
    srcs = ["pass_lec.cpp"],
    hdrs = ["pass_lec.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":lec_query",
        "//core",
        "//pass/common:pass",
    ],
    alwayslink = True,  # static Pass_plugin registration
)

# Milestone-1 build de-risk: cvc5 link smoke test (lec.md build sequence step 1).
cc_test(
    name = "cvc5_link_test",
    srcs = ["tests/cvc5_link_test.cpp"],
    copts = COPTS,
    deps = [
        "@cvc5",
        "@googletest//:gtest_main",
    ],
)

# Build sequence step 2: smt-switch (cvc5 backend) link smoke test.
cc_test(
    name = "smtswitch_link_test",
    srcs = ["tests/smtswitch_link_test.cpp"],
    copts = COPTS,
    deps = [
        "@cvc5",  # smt-switch is built header-only against cvc5; link it here
        "@googletest//:gtest_main",
        "@smtswitch//:smt_switch",
    ],
)

# Build sequence step 3: full cvc5 -> smt-switch -> Pono stack link test +
# L1 discharge-mechanism validation (latched miter, k-induction/BMC).
cc_test(
    name = "pono_link_test",
    srcs = ["tests/pono_link_test.cpp"],
    copts = COPTS,
    deps = [
        "@googletest//:gtest_main",
        "@pono",
    ],
)

# M1 acceptance: encode + prove_equal on tiny combinational graphs.
cc_test(
    name = "comb_equiv_test",
    srcs = ["tests/comb_equiv_test.cpp"],
    copts = COPTS,
    deps = [
        ":lec_query",
        "//graph",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)

# M1 cross-check oracle: `lhd lec --set lec.cross=true` must agree with lgcheck
# on an equal and a different Verilog pair. lhd finds lgcheck/yosys via its own
# runfiles (//lhd already deps //inou/yosys:scripts).
sh_test(
    name = "lec_cross_test",
    srcs = ["tests/lec_cross_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # invokes the lgcheck yosys subprocess
)
