# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

# L0 encoder (LGraph -> cvc5 bit-vector terms) + L1 relational query
# (prove_equal, a direct cvc5 combinational-miter checkSat). The reusable engine
# core; the pass.lec plugin (L3) layers on top.
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
        "@cvc5",  # prebuilt cvc5 (combined+localized) -- the SMT backend
        "@hhds//hhds:graph",
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

# cvc5 link smoke test: the prebuilt static cvc5 links and a QF_ABV query runs
# through cvc5's own C++ API (the API pass/lec is built directly on).
cc_test(
    name = "cvc5_link_test",
    srcs = ["tests/cvc5_link_test.cpp"],
    copts = COPTS,
    deps = [
        "@cvc5",
        "@googletest//:gtest_main",
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

sh_test(
    name = "lec_phase_test",
    srcs = ["tests/lec_phase_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # invokes the in-process cvc5 BMC engine
)

sh_test(
    name = "lec_mem_test",
    srcs = ["tests/lec_mem_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # compiles RTL + runs the in-process cvc5 array engine
)

sh_test(
    name = "lec_arrfield_test",
    srcs = ["tests/lec_arrfield_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # compiles RTL via both readers + lgcheck
)
