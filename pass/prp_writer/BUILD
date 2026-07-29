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
