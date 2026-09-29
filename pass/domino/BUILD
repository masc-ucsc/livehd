# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:copt_default.bzl", "COPTS")

# The domino GATE MODEL: truth tables, unateness, minimum-transistor
# series-parallel factoring and the fit decision (fan-in / stack / budget). No
# graph dependency, so the mapper's core is testable exhaustively on its own.
cc_library(
    name = "domino_gate",
    srcs = ["domino_gate.cpp"],
    hdrs = ["domino_gate.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_map",
    ],
)

cc_test(
    name = "domino_gate_test",
    size = "small",
    srcs = ["domino_gate_test.cpp"],
    copts = COPTS,
    deps = [
        ":domino_gate",
        "@googletest//:gtest_main",
    ],
)

load("@rules_cc//cc:defs.bzl", "cc_binary")

# A/B tool: pass.usyn's transistor count against Sp_factorer on the LUTs of
# real covers (usyn_ab.cpp). Not a test: it needs a compiled design and a
# Liberty.
cc_binary(
    name = "usyn_ab",
    srcs = ["usyn_ab.cpp"],
    copts = COPTS,
    deps = [
        ":domino_gate",
        "//core",
        "//graph",
        "//pass/abc:pass_abc",
        "//pass/common:pass",
        "//pass/synth",
        "//pass/usyn:pass_usyn",
        # The region pipeline lowers memories through emitted RTL: cgen and the
        # yosys reader must be registered in the process.
        "//inou/cgen:inou_cgen",
        "//inou/yosys:lgyosys",
    ],
)
