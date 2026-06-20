# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# Single-design formal property prover: a demand-driven cone encoder (reusing
# pass/lec's Encoder helpers + Val) + cvc5 queries (is_true / is_false / equal /
# is_onehot0 / is_constant) under a deterministic cone-scaled budget. The
# reusable library any pass calls (pass.formal driver below, future pass/memory).
cc_library(
    name = "formal",
    srcs = ["prove.cpp"],
    hdrs = ["prove.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//graph",
        "//pass/lec:lec_query",  # the shared cvc5 Encoder + Val + fit_to/real_width/flop_state_key
        "@cvc5",
        "@hhds//hhds:graph",
    ],
)

# L3 plugin: pass.formal (the pass.formal.* set-option registry + the always-on
# driver: Hotmux one-hotness now, user assert/assume materialized in tolg next).
cc_library(
    name = "pass_formal",
    srcs = ["pass_formal.cpp"],
    hdrs = ["pass_formal.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":formal",
        "//core",
        "//graph",
        "//pass/common:pass",
        "@hhds//hhds:graph",
    ],
    alwayslink = True,  # static Pass_plugin registration
)

# Prover acceptance: tautology / contradiction / onehot0 / cone-gate verdicts on
# tiny hand-built graphs (no reader needed), mirroring pass/lec comb_equiv_test.
cc_test(
    name = "prove_test",
    srcs = ["tests/prove_test.cpp"],
    copts = COPTS,
    deps = [
        ":formal",
        "//graph",
        "@googletest//:gtest_main",
        "@hhds//hhds:graph",
    ],
)
