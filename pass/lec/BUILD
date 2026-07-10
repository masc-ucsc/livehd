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

# cvc5 link smoke check: the prebuilt static cvc5 links and a QF_ABV query runs
# through cvc5's own C++ API (the API pass/lec is built directly on). Standalone
# (no corresponding cvc5_link.cpp), so no _test suffix.
cc_test(
    name = "cvc5_link_smoke",
    srcs = ["tests/cvc5_link_smoke.cpp"],
    copts = COPTS,
    deps = [
        "@cvc5",
        "@googletest//:gtest_main",
    ],
)

# M1 acceptance: encode + prove_equal (the query.cpp API) on tiny combinational
# graphs.
cc_test(
    name = "query_test",
    srcs = ["tests/query_test.cpp"],
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
    name = "lec_mem_init_test",
    srcs = ["tests/lec_mem_init_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # type==2 init-contents tie via the in-process cvc5 array engine
)

sh_test(
    name = "cex_debug_test",
    srcs = ["tests/cex_debug_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # in-process cvc5 BMC + first-diverging-cut readback
)

sh_test(
    name = "lec_arrfield_test",
    srcs = ["tests/lec_arrfield_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # compiles RTL via both readers + lgcheck
)

sh_test(
    name = "lec_combarray_test",
    srcs = ["tests/lec_combarray_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # compiles RTL via both readers + lgcheck
)

# lec.partitions / lec.split: parallel input-space case-split. Auto-picks the
# control selector, forks cube-workers, folds the datapath per cube; PROVEN/REFUTED
# via case-split, disabled by partitions=1, graceful fallback with no selector.
sh_test(
    name = "lec_partitions_test",
    srcs = ["tests/lec_partitions_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # forks in-process cvc5 cube-workers
)

# lec.timeout regression: nonlinear-multiplier miters that freeze cvc5 forever
# must come back promptly as UNKNOWN once a per-query solver time limit is set.
sh_test(
    name = "lec_timeout_test",
    srcs = ["tests/lec_timeout_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # runs the in-process cvc5 solver under a time limit
)

# Verdict -> exit-code policy: PROVEN/UNKNOWN exit 0 (UNKNOWN with a loud
# inconclusive warning), REFUTED and `--set lec.strict` UNKNOWN exit non-zero.
sh_test(
    name = "lec_verdict_policy_test",
    srcs = ["tests/lec_verdict_policy_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # runs the in-process cvc5 solver
)

# lecfail witness reproduction (`lhd lec` + --workdir): a REFUTED verdict writes a
# self-contained Pyrope testbench (lec.prpfail) driving BOTH designs and runs
# `lhd sim` to dump one VCD (lec.prpfailrun). Covers the default-on behavior, the
# three knobs, and the no-workdir / PROVEN gating.
sh_test(
    name = "lec_witness_prpfail_test",
    srcs = ["tests/lec_witness_prpfail_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # in-process cvc5 BMC + shells `lhd compile` / `lhd sim --setup-only`
)

# Agentic per-block progress: every resolved block emits one machine-parseable
# `info`-severity diagnostic (lec-block-proven / -refuted / -inconclusive) with a
# verdict, the winning engine, and duration_ms, without touching the exit code.
sh_test(
    name = "lec_progress_test",
    srcs = ["tests/lec_progress_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # runs the in-process cvc5 solver
)

# auto parallel portfolio (lec.engine=auto): race ind + bmc as forked workers,
# take the first trustworthy verdict (ind-Proven=PASS, bmc-Refuted=FAIL); an
# inductive false-refute on an unreachable state is a HINT, never a hard fail.
sh_test(
    name = "lec_auto_test",
    srcs = ["tests/lec_auto_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # forks the in-process cvc5 solver as race workers
)

# Proven-module black-box collapse (lhd lec --collapse <def>): force a same-library
# submodule to the sound UF black-box (hide its internals) while still refuting on
# diverging leaf inputs or out-of-leaf logic. Exercises the hhds forward_hier +
# edge-resolver opacity (Hier_opaque_scope).
sh_test(
    name = "lec_collapse_test",
    srcs = ["tests/lec_collapse_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # runs the in-process cvc5 solver on compiled RTL
)

# Sequential black-box (stateful collapse): outputs = UF(inputs, state), next =
# UF2(inputs, state) with a shared state cut threaded across BMC cycles, so a
# collapsed stateful leaf's output varies per cycle and a timing difference is
# refuted (a combinational box would force it constant and false-prove).
sh_test(
    name = "lec_seqbox_test",
    srcs = ["tests/lec_seqbox_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # runs the in-process cvc5 UF+BV solver on compiled RTL
)

# Bottom-up hierarchical driver (lec.hierarchical=true): topo-order the def DAG,
# LEC leaves-first under auto, collapse proven children into parents, and flatten
# an unprovable child into its parent (CEGAR fallback).
sh_test(
    name = "lec_hier_test",
    srcs = ["tests/lec_hier_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # forks the in-process cvc5 solver per def
)

# Structural def-diff reduction (lec.semdiff=structural, M3): a def whose ref/impl
# are structurally identical (and whose children are proven) is dropped with no
# solver call; only the changed def is solved, and a non-equivalent edit still
# refutes (the skip is children-guarded).
sh_test(
    name = "lec_semdiff_test",
    srcs = ["tests/lec_semdiff_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # runs pass.semdiff + the in-process cvc5 solver
)

# Every formal-engine source, hashed at build time into the verdict-cache salt
# (lhd//:formal_salt genrule): a change to the prover auto-invalidates cached
# verdicts with no human in the loop (2f-fcore ruling 2026-07-10).
filegroup(
    name = "formal_srcs",
    srcs = glob([
        "*.cpp",
        "*.hpp",
    ]),
    visibility = ["//visibility:public"],
)
