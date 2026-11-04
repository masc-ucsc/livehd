# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("//tools:regression.bzl", "cc_test", "sh_test")
load("@rules_cc//cc:defs.bzl", "cc_library")

load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "upass_tolg",
    srcs = ["upass_tolg.cpp"],
    hdrs = ["upass_tolg.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//graph",
        "//lnast",
        "//upass/core:array_dim_hdr",
        "//upass/core:default_prologue_hdr",
        "//upass/core:io_port_rules_hdr",
        "//upass/core:loop_index_ports_hdr",
        "//upass/core:range_bits_hdr",
        "//pass/common:pass",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "upass_tolg_test",
    srcs = ["upass_tolg_test.cpp"],
    copts = COPTS,
    deps = [
        ":upass_tolg",
        "//core",
        "//graph",
        "//lnast",
        "@googletest//:gtest_main",
    ],
)

# Loop lowering QoR guard: a rolled loop's per-bit writes into a packed word
# merge into one narrow write that folds to a constant position once synthesis
# expands the loop (a `unique if` merge stays one-hot), and the copies of
# `for j in (i + 1)..<N` share one lifted definition (see
# tests/loop_bit_write_qor_test.sh).
sh_test(
    name = "loop_bit_write_qor_test",
    size = "medium",
    srcs = ["tests/loop_bit_write_qor_test.sh"],
    data = [
        "tests/loop_gray2bin.prp",
        "tests/loop_lru_matrix.prp",
        "tests/loop_unique_window.prp",
        "//inou/prp:abc_tests",
        "//lhd",
    ],
)

# Rolled-loop window writes stay the variable's width: K shift-form bit writes
# per body grew a K*8K-bit chain while the `upper` shift was cut only when the
# window could pass the top bit (review2 L2; tests/loop_window_width_test.sh).
sh_test(
    name = "loop_window_width_test",
    srcs = ["tests/loop_window_width_test.sh"],
    data = ["//lhd"],
)

# `reg q:[initial=5] = 9` names two values for a flop that holds one: the
# attribute wins and the dropped declared value is a `reset-init-mismatch`
# warning (tests/reg_initial_attr_warning_test.sh).
sh_test(
    name = "reg_initial_attr_warning_test",
    srcs = ["tests/reg_initial_attr_warning_test.sh"],
    data = ["//lhd"],
)
