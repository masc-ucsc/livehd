# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:copt_default.bzl", "COPTS")

cc_library(
    name = "pass_prp_writer",
    srcs = ["pass_prp_writer.cpp"],
    hdrs = ["pass_prp_writer.hpp"],
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//pass/common:pass",
        "//upass/prp_writer:upass_prp_writer",
    ],
    alwayslink = True,
)

sh_test(
    name = "prp_writer_roundtrip_test.sh",
    srcs = ["tests/prp_writer_roundtrip_test.sh"],
    data = [
        "//inou/prp:tests/comptime/trivial_if.prp",
        "//lhd",
    ],
)

sh_test(
    name = "prp_writer_kw_bundle_test.sh",
    srcs = ["tests/prp_writer_kw_bundle_test.sh"],
    data = [
        "tests/kw_bundle_base.prp",
        "//lhd",
    ],
)

# REGRESSION: the writer's `___ssa_N` -> `__wN` demotion must not land on a
# name the source already uses — `__wN` is the writer's OWN output namespace,
# so generated Pyrope is full of them. A collision merges two variables into
# one emitted identifier (a silent miscompile of the emitted source). Twin of
# the SSA-entry demotion gated by //lhd/tests:upass_converged_rewalk_test.
sh_test(
    name = "prp_writer_ssa_demote_collision_test.sh",
    srcs = ["tests/prp_writer_ssa_demote_collision_test.sh"],
    data = [
        "tests/ssa_demote_collision.prp",
        "//lhd",
    ],
)

sh_test(
    name = "prp_writer_kw_fields_test.sh",
    srcs = ["tests/prp_writer_kw_fields_test.sh"],
    data = [
        "tests/kw_bundle_keywords.prp",
        "//lhd",
    ],
)

# REGRESSION: generated Pyrope must always RE-PARSE. Two analyses used to claim
# the same statement — analyze_muxes folded a ternary's consuming store away as
# an enclosing mux's else seed, while analyze_expr_inlines picked that same store
# as the merge temp's single use and dropped the temp's definition — so the
# writer emitted a read of a `_mux_N` it never declared. Equivalence coverage of
# the same shape: //inou/prp:prp-v2v-writer_undefined_mux (and prp-v2prp-*).
sh_test(
    name = "prp_writer_mux_decl_test.sh",
    srcs = ["tests/prp_writer_mux_decl_test.sh"],
    data = [
        "tests/mux_seed_fold.v",
        "//lhd",
    ],
)

# REGRESSION: the writer must not LOSE information when it rebuilds a nested
# `mut` declaration at the function top (it used to record the NAME only, so an
# array declared inside an `if` came back as `mut mem = 0` — type and `initial`
# contents both gone), and must backtick-escape a module name Pyrope cannot
# tokenize bare (a Verilog escaped id, `\d\e`). Equivalence coverage of the same
# two shapes: //inou/prp:prp-v2v-tolg_field_read_zero and prp-v2v-writer_bad_char.
sh_test(
    name = "prp_writer_decl_emit_test.sh",
    srcs = ["tests/prp_writer_decl_emit_test.sh"],
    data = [
        "tests/decl_emit.v",
        "//lhd",
    ],
)
