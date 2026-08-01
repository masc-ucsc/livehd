# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:cc_headers.bzl", "cc_direct_headers")
load("//tools:copt_default.bzl", "COPTS")

exports_files([
    "tests/pyrope/proc_lambda.prp",
    "tests/pyrope/simple.prp",
    "tests/comptime/trivial_if.prp",
])

# The pyrope/verilog golden pairs (used by the lhd flow-matrix tests).
filegroup(
    name = "equiv_tests",
    srcs = glob(["tests/equiv/*"]),
    visibility = ["//visibility:public"],
)

# Hierarchical Pyrope fixtures shared by the color / partition / abc pass tests
# (a combinational and a sequential multi-level design).
filegroup(
    name = "hier_tests",
    srcs = [
        "tests/pyrope/hier_comb.prp",
        "tests/pyrope/hier_seq.prp",
    ],
    visibility = ["//visibility:public"],
)

# pass.abc fixtures (task 2a-abc): a flat combinational Pyrope design plus a
# small self-contained Liberty so the ABC tech-map + LEC test is hermetic (no
# PDK dependency).
filegroup(
    name = "abc_tests",
    srcs = [
        "tests/abc/test.lib",
        "tests/pyrope/abc_arith.prp",
        "tests/pyrope/abc_block_attr.prp",
        "tests/pyrope/abc_comb.prp",
        "tests/pyrope/abc_constmul.prp",
        "tests/pyrope/abc_flat_names.prp",
        "tests/pyrope/abc_mathops.prp",
        "tests/pyrope/abc_mem.prp",
        "tests/pyrope/abc_seq.prp",
        "tests/pyrope/hier_seq.prp",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "inou_prp",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "*test*.cpp",
            # Standalone gtest smoke check (no _test suffix: it exercises only
            # the external prpparse API, not a sibling .cpp here).
            "prpparse_stream_smoke.cpp",
        ],
    ),
    hdrs = glob(["*.hpp"]),
    copts = COPTS,
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//core",
        "//lnast",
        "//pass/common:pass",
        "@prpparse//:prpparse",
    ],
    alwayslink = True,
)

# 2f-stream: the streaming seam keeps the CST arena bounded to one construct.
# Standalone smoke check of the external prpparse streaming API (no sibling
# prpparse_stream.cpp here, so no _test suffix).
cc_test(
    name = "prpparse_stream_smoke",
    srcs = ["prpparse_stream_smoke.cpp"],
    copts = COPTS,
    deps = [
        "@googletest//:gtest_main",
        "@prpparse//:prpparse",
    ],
)

[
    sh_test(
        name = "prp-%s" % prp_file.split("/")[-1].split(".")[0],
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:%s)" % prp_file,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:%s" % prp_file,
            "//lhd",
        ],
    )
    for prp_file in glob([
        "tests/pyrope/*.prp",
        "tests/comptime/*.prp",
        "tests/verification/*.prp",
        "tests/errors/*.prp",
    ])
]

# Fast multi-module prp_writer regression: emit sibling files, then compile
# only the emitted top and require importer-directory-relative discovery to
# pull in its emitted callee. This intentionally does not invoke LEC.
sh_test(
    name = "prp-p2p-sibling-recompile",
    srcs = ["tests/p2p_recompile_test.py"],
    args = [
        "-i",
        "$(location //inou/prp:tests/pyrope/sibling_recompile.prp)",
    ],
    data = [
        "//inou/prp:tests/p2p_recompile_test.py",
        "//inou/prp:tests/pyrope/sibling_recompile.prp",
        "//lhd",
    ],
)

# Fast R5 regression: the ordinary pyrope glob above catches the pre-fix tolg
# failure; this companion checks that accepting the clock tuple did not merely
# collapse it to one shared clock in emitted Verilog.
sh_test(
    name = "prp-memory-multiclock-verilog",
    srcs = ["tests/memory_multiclock_test.py"],
    args = [
        "-i",
        "$(location //inou/prp:tests/pyrope/memory_multiclock_ports.prp)",
    ],
    data = [
        "//inou/prp:tests/memory_multiclock_test.py",
        "//inou/prp:tests/pyrope/memory_multiclock_ports.prp",
        "//lhd",
    ],
)

# Expected-warning tests (`:type: warning`, in tests/warnings/): the program
# must COMPILE CLEANLY but emit a warning diagnostic whose message/hint match
# the header :warning:/:help: regexes (the lint counterpart of tests/errors/).
# A `locate_warning_here` comment pins the warning's line. Distinct `prp-warn-`
# prefix so the target names cannot collide with the `prp-` glob above.
[
    sh_test(
        name = "prp-warn-%s" % prp_file.split("/")[-1].split(".")[0],
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:%s)" % prp_file,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:%s" % prp_file,
            "//lhd",
        ],
    )
    for prp_file in glob(["tests/warnings/*.prp"])
]

# TBD-feature trackers (tests/fixme/): each `.prp` exercises a documented but
# NOT-yet-implemented Pyrope feature (marked "TBD" in the docs / 15-tbd.md). Most
# are `:type: compile` (the feature has no lowering, so the compile FAILS by
# design); `testbench_step` is `:type: simulation` (the instance/`step`/field-poke
# model runs, but the string-path `peek("a/b")`/`poke("a/b",v)` accessors are
# still unimplemented). Each flips green once the feature lands — at which point
# drop the `fixme` tag and move the file into the
# normal glob above. Tagged `fixme` so `.bazelrc` (`-fixme`) keeps `bazel test
# //...` green; run one with the tag filter cleared to see the failure:
#   bazel test //inou/prp:prp-tbd-temporal_past --test_tag_filters=
# Covered: temporal library (past[n]), standard library (import("prp")),
# verification extras (requires/ensures), testbench extras (step/peek/poke),
# and the finish-generics gaps from todo/livehd/3g.html (generic_* = body
# references of a generic name / declaration defaults <T, N=4> / named <T=…>
# bindings / generic-valued input default, input_default = input defaults with
# no generics involved; each header records today's exact diagnostic; the
# `T(a)` constructor form is tracked by the prp-equiv-gen_type_cast pair
# below instead, which also LEC-checks the lowering once it lands).
# (The `lg` attribute used to be on this list but is now implemented — its TBD
# markers were removed from the docs.) The `macro=` memory-compiler binding is
# also still TBD but is silently accepted-and-ignored — it compiles clean today
# (no diagnostic), so it needs an emitted-Verilog grep harness, not a compile
# test, and is tracked in todo/pyrope/3f-macro.html instead.
# One entry (wire_field_const_store) is a known-BUG tracker rather than a TBD
# feature: valid code that miscompile-errors today (see its fixture header).
[
    sh_test(
        name = "prp-tbd-%s" % base,
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:tests/fixme/%s.prp)" % base,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/prpsim.py",  # testbench_step is :type: simulation
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:tests/fixme/%s.prp" % base,
            "//inou/prp:sim_runtime_hdrs",
            "//lhd",
        ],
        # testbench_step + temporal_past now lower/run, as do the
        # 3g subtask-A body-binding trackers (generic_{body_type,const_value,
        # lambda_param} — a generic name is now visible+substituted in the body);
        # the rest are still TBD. (temporal_past: `past[N](x)` lowers to a depth-N
        # `stage` shift register — see prp2lnast function_call_expr_to_node.)
        # verif_requires is GONE: requires/ensures were removed from the language
        # (user ruling, 2026-07-25) — write assume for a pre, assert for a post.
        tags = [] if base in (
            # 2f-latch M8 (edge normalization): the two latch twins and the
            # posedge/negedge twin all carry REAL verdicts now — the encoder no
            # longer sees a Latch or a same-clock edge difference, because
            # pass.single_edge rewrote them away before it ran. Promoted out of
            # the fixme arm 2026-07-24.
            "latch_verify_hold",
            "latch_verify_low",
            "flop_verify_posneg",
            "testbench_step",
            "temporal_past",
            "generic_body_type",
            "generic_const_value",
            "generic_lambda_param",
            "generic_default_arg",
            "input_default",
            # B/C land via the prpparse grammar (generic decl-defaults + named
            # binds); they need the @prpparse local override / re-pinned commit.
            "generic_decl_default",
            "generic_named_bind",
        ) else ["fixme"],
    )
    for base in [
        "temporal_past",
        "stdlib_import",
        "testbench_step",
        "generic_body_type",
        "generic_const_value",
        "generic_lambda_param",
        "generic_decl_default",
        "generic_named_bind",
        "generic_default_arg",
        "input_default",
        # A VECTOR port (unnamed/positional tuple, or an array) has no lowering
        # in either direction. Counterpart of tests/sim/tuple_io_ports.prp,
        # which covers the NAMED tuple and passes: a named tuple's field order
        # is not guaranteed so its leaves may only be addressed by name, while
        # an unnamed entry is the one aggregate with a defined order and so the
        # only one that could be indexed/packed as a vector.
        "vector_io_ports",
        # Known BUG tracker (not TBD): const store to a wire-BUNDLE field is
        # folded+dropped (detupled dotted declare loses its wire mode), then
        # tolg errors "wire 'io.a' is never driven" on valid code. See the
        # fixture header for the root-cause chain and fix direction.
        "wire_field_const_store",
        # todo/livehd/2f-latch BMC twins (:type: verify) of the sim trackers
        # above — the SAME self-checking properties as design-body asserts for
        # `lhd formal verify` (stated twice because test-block asserts are
        # invisible to verify and body asserts are not executed by sim; see
        # each header for the dual-fixture conventions). GREEN since M8
        # (untagged above): pass.single_edge retypes the Latch into a
        # flop-with-enable and slots a negedge flop into its own sub-step, so
        # the ordinary Flop encoding decides all three. They stay in tests/fixme/
        # only because moving them would renumber every prp-tbd-* target; the
        # `fixme` TAG is what mattered and it is gone.
        "latch_verify_hold",
        "latch_verify_low",
        "flop_verify_posneg",
    ]
]

# Import tests (2f-lnimport): multi-file — an importer plus the unit(s) it
# imports. Each test names its inputs via the `:files:` header; every .prp in
# tests/imports/ is bundled into data so the header's repo-relative paths
# resolve at runtime. A `lib_*.prp` is an imported dependency only (no
# standalone target — it is meaningful only as another unit's import).
[
    sh_test(
        name = "prp-import-%s" % prp_file.split("/")[-1].split(".")[0],
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:%s)" % prp_file,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/pyrope_test.py",
            "//lhd",
        ] + ["//inou/prp:%s" % f for f in glob(["tests/imports/*.prp"])],
    )
    for prp_file in glob(["tests/imports/*.prp"])
    if not prp_file.split("/")[-1].startswith("lib_")
]

# 2f-lgimport: the lg: import (compiled lgraph) two-step flow + LEC, plus the
# not-found / port-mismatch diagnostics, exercised from a self-contained script
# (it generates its own .v / .prp fixtures and drives `lhd compile` + `lhd
# check`). Needs the yosys scripts (lgcheck) on top of the lhd binary.
sh_test(
    name = "prp-import-lg",
    srcs = ["tests/imports/lg_import_test.sh"],
    data = ["//lhd"],
    deps = ["//inou/yosys:scripts"],
)

# What a PLAIN `import("unit")` binds to when that same unit is ALSO supplied as
# a pre-built IR input — distinct from prp-import-lg above, which covers the
# EXPLICIT `import("lg:name")` form. The contract:
#   no ln:/lg: input -> re-parse the sibling .prp into a fresh LNAST
#   ln: input        -> reuse that LNAST, do NOT re-parse the sibling
#   lg: input        -> call the compiled LGraph directly (nothing to parse)
# The ln:/lg: artifacts are built from a DIFFERENT definition of the same unit,
# each with a distinct output width, so the width the importer instantiates says
# which definition actually won; `--result-json` inputs says whether the sibling
# was re-parsed. Split into two targets so the half lhd already honours is
# enforced on every run, while the lg: half stays visible and runnable on demand
# without breaking the build: today a lg: input does not resolve a plain import
# at all, and the compile silently falls back to re-parsing the sibling source.
sh_test(
    name = "prp-import-ir-precedence",
    srcs = ["tests/imports/ir_import_precedence_test.sh"],
    args = [
        "no_ir",
        "ln_only",
    ],
    data = ["//lhd"],
)

sh_test(
    name = "prp-import-ir-precedence-lg",
    srcs = ["tests/imports/ir_import_precedence_test.sh"],
    args = [
        "lg_only",
        "ln_and_lg",
    ],
    data = ["//lhd"],
    tags = ["fixme"],
)

# Simulation tests (tests/sim/*.prp, `:type: simulation`): a synthesizable DUT
# driven from `test` blocks that end-of-sim `assert` the result. The harness
# (`lhd sim --setup-only`) lowers each DUT to header-only Slop<N> C++ and emits
# a driver per test; prplib then compiles each driver with the HOST C++ compiler
# and runs it (a non-zero exit == an assert fired). The Slop/Blop runtime is
# header-only and, with -DNDEBUG, has no link deps — so this needs NO nested
# bazel, just hlop's + iassert's headers staged into runfiles (the
# `sim_runtime_hdrs` data dep below), discovered by name at runtime.
cc_direct_headers(
    name = "sim_runtime_hdrs",
    deps = [
        "@hlop//hlop",
        "@iassert",
    ],
    visibility = ["//visibility:private"],
)

# SILENT-SIM-BUG trackers (2026-07-25 latch/edge audit). Each fixture is a
# 4-tick run whose asserts capture the CORRECT end-of-tick values (taken from
# Icarus on the netlist LiveHD itself emits), and each fails TODAY on a shape
# `lhd sim` gets wrong with NO diagnostic -- reports 0 errors and hands back
# wrong numbers. Every one also carries CONTROL asserts that pass, so a failure
# is always attributable to the named bug rather than to a dead testbench.
#
# They are tagged `fixme` (the repo's known-broken convention; `.bazelrc`
# filters `-fixme`) so `bazel test //...` stays green. Run one with:
#   bazel test //inou/prp:prp-sim-latch_sim_clockgate_high --test_tag_filters=
# Drop the name from this list the moment its bug is fixed -- that promotion IS
# the regression test.
_SIM_FIXME = [
    # A transparent-HIGH latch gated by the design's CLOCK net never commits:
    # the gate cone reads the clock as ordinary DATA and cgen_sim never pokes
    # that In field. `lhd sim` and `lhd formal verify` disagree on this shape.
    "latch_sim_clockgate_high",
    # The US7624363 L1 case: a transparent-LOW latch on clk closes at the RISE
    # and a posedge flop on the same clk samples at that same rise, so it must
    # read THROUGH the latch. cgen_sim gives it the previous commit -- a full
    # period late. pass.single_edge refuses this shape for LEC; sim is silent.
    "latch_sim_l1_buffer",
    # A negedge flop in a SECOND clock domain never commits: the per-net
    # previous-level bit is updated BEFORE the phase-2 block, so the negedge
    # guard `(!cur && prev)` is a tautological false.
    "flop_sim_secondary_negedge",
    # A memory write is silently DROPPED when the design also holds a negedge
    # flop: the memory next-state section is emitted after phase 2 has erased
    # its operand cones AND after the negedge-operand-unresolved check, so the
    # write guard comes out as a literal `0 /*UNRESOLVED-CYCLE*/`.
    "mem_sim_negedge_write",
    # Negedge state INSIDE a submodule lands a full period late: the two-phase
    # tick builds its stale set from THIS graph's flops only, and the child's
    # cycle() runs during the parent's comb walk, before the parent's commits.
    "hier_sim_negedge_sub",
    # Reading a DUT output BEFORE `step` consumes a second-domain clock edge:
    # peek() runs a full cycle() and rolls back flops/memories/tick but NOT the
    # per-net previous-clock-level bit. Triggered by an ordinary debugging
    # habit, not by anything wrong with the design.
    "flop_sim_peek_secondary",
]

[
    sh_test(
        name = "prp-sim-%s" % prp_file.split("/")[-1].split(".")[0],
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:%s)" % prp_file,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/prpsim.py",
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:%s" % prp_file,
            "//inou/prp:sim_runtime_hdrs",
            "//lhd",
        ],
        tags = ["fixme"] if prp_file.split("/")[-1].split(".")[0] in _SIM_FIXME else [],
    )
    for prp_file in glob(["tests/sim/*.prp"])
]

# `:type: verify` fixtures (tests/verify/): the harness drives `lhd formal
# verify` on a single self-contained .prp (design + `formal` blocks) with a
# --workdir, then checks the header expectations against the machine artifacts:
# formal_report.json verdict counts, the simfail_<test>.prp counterexample
# testbench on an expected REFUTE, and the replay outcome (:verify_replay:
# fired = the driven trace re-fires the assert; no-refire = the loud
# free-initial-state warning). The replay compiles a sim driver, hence the
# sim_runtime_hdrs data dep. Distinct `prp-verify-` prefix.
[
    sh_test(
        name = "prp-verify-%s" % prp_file.split("/")[-1].split(".")[0],
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:%s)" % prp_file,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:%s" % prp_file,
            "//inou/prp:sim_runtime_hdrs",
            "//lhd",
        ],
    )
    for prp_file in glob(["tests/verify/*.prp"])
]

# Equivalence pairs (tests/equiv/<name>.prp + golden <name>.v): the harness
# lowers the .prp to Verilog (tolg path) and formally LECs it against the
# golden via inou/yosys/lgcheck, so these need the yosys scripts/binary on
# top of the plain-prp data. Distinct `prp-equiv-` prefix: equiv/trivial_if
# would otherwise collide with the comptime/trivial_if target name.
[
    sh_test(
        name = "prp-equiv-%s" % prp_file.split("/")[-1].split(".")[0],
        srcs = ["tests/pyrope_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:%s)" % prp_file,
        ],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:%s" % prp_file,
            "//inou/prp:%s" % (prp_file[:-len(".prp")] + ".v"),
            "//lhd",
        ],
        deps = ["//inou/yosys:scripts"],
    )
    for prp_file in glob(
        ["tests/equiv/*.prp"],
        # accum.prp is the aspirational call-site-phase file (stage[N]/@[N]):
        # no test header, no golden .v — it goes live with the call-site phase.
        exclude = [
            # `*_tb.prp` are the generated differential-sim testbenches (prp-simeq-*
            # below), NOT equiv pairs — never let the equiv glob pick them up.
            "tests/equiv/*_tb.prp",
            "tests/equiv/accum.prp",
            # signed_one_bit_cmp: known-failing miscompile tracker (1-bit signed
            # ordered compare reads unsigned). Run via the fixme target below.
            "tests/equiv/signed_one_bit_cmp.prp",
            # The 2026-07-29 corpus TRACKERS: one minimal repro per remaining
            # hard-failure category, each verified to fail TODAY. They carry no
            # marker in their NAME -- the `fixme` TAG is what marks them -- so
            # they are excluded here and re-emitted with `tags = ["fixme"]`
            # below. Only the six whose
            # FORWARD direction (.prp -> verilog -> LEC) actually breaks are
            # listed; blocking_ff_state and negative_array_index
            # PROVE forward (their .prp is the hardware the Verilog means) and
            # stay in this glob as ordinary, live coverage -- only their
            # Verilog-reading legs are fixme.
            # writer_undefined_mux was PROMOTED out of this exclude list
            # (2026-07-29): it never belonged here. Its `.prp` was the writer's
            # own BROKEN OUTPUT pasted in, reading an undeclared `_mux_1`, so the
            # forward direction failed as a COMPILE ERROR (rc=6) -- not as an
            # equivalence gap. The writer bug is fixed (see the pair's header),
            # the `.prp` is the real hand-written source form of the shape, and
            # all three directions prove; the parser half is pinned by
            # tests/errors/undefined_read_if_expr_arm.prp.
            # tolg_field_read_zero and writer_bad_char were PROMOTED out of this
            # exclude list (2026-07-29), for the same reason writer_undefined_mux
            # was: both `.prp` files held the writer's own BROKEN OUTPUT, so the
            # forward direction failed as a TOKENIZE/COMPILE error rather than as
            # an equivalence gap. Both writer bugs are fixed (see each pair's
            # header -- the nested-mut hoist now carries the declare's type AND
            # its comptime init; module names are backtick-escaped), the `.prp`
            # files are the real source form of each shape, and all three
            # directions prove. The writer half of both is pinned as a TEXT gate
            # by //pass/prp_writer:prp_writer_decl_emit_test.sh.
            "tests/equiv/latch_rule_a.prp",
            "tests/equiv/latch_rule_b.prp",
            "tests/equiv/dual_async_set_reset.prp",
            "tests/equiv/reg_clock_from_logic.prp",
            "tests/equiv/mem_write_during_reset.prp",
            "tests/equiv/async_mem_reset_demoted.prp",
            "tests/equiv/reg_decl_init_dropped.prp",
            "tests/equiv/selfref_slice_assign.prp",
            "tests/equiv/selfref_concat_assign.prp",
            # NOTE: the three phase_* pairs (phase_latch_flop_edges,
            # phase_hier_latch_neg, phase_clock_gate_chain) were PROMOTED out of
            # this exclude list (2f-lec / 2f-latch M10). They compare normalized
            # Pyrope state against Verilog that RETAINS latch-close or
            # Clock_cell timing, and lgcheck's bounded miter -- which steps one
            # posedge per step -- cannot represent either, so it reported a
            # mismatch between equivalent designs. Each now carries
            # `:equiv_engine: cvc5` in its header and is discharged by the
            # phase-aware `lhd lec` instead; the INDEPENDENT oracle for the pair
            # is the v2prp2v original-Verilog leg (still lgcheck) plus
            # lhd/tests/single_edge_four_classes_test.sh under Icarus.
            # ...and the five LEC-REFUTED trackers: these round-trip fine, so
            # the failure is a SILENT MISCOMPILE the miter catches.
            # divided_clock_decl_order has TWO clock domains (a register clocked
            # by another register's Q), which yosys' miter cannot decide -- the
            # bounded check does not run to completion and the pair is
            # INCONCLUSIVE, not refuted. What it regresses is the WRITER's
            # declaration ordering, so its coverage lives in the round-trip
            # combined prp-v2prp2v direction, which does run.
            "tests/equiv/divided_clock_decl_order.prp",
            # comb_array_const_index_read is the same shape of exclusion: it is
            # a MEMORY design, and lgcheck's bounded miter does not run to
            # completion on it (INCONCLUSIVE, not refuted). What it regresses --
            # a const-indexed read of a written comb-array entry folding to the
            # array's init -- is created by the WRITER's per-entry expansion of
            # a whole-array copy, so its coverage lives in the round-trip
            # combined prp-v2prp2v direction, which does run and does decide.
            "tests/equiv/comb_array_const_index_read.prp",
            # NOTE: the three comb-array store-loss trackers
            # (array_store_stale_rhs, arr_comb_mux_const, arr_comb_tmp) were
            # PROMOTED out of this exclude list (2026-07-27). Root cause was
            # upass.ssa copying a >=3-child (tuple_set) store VERBATIM, leaving
            # its VALUE operand on the stale base name after the def had been
            # renamed; see upass_ssa.cpp's is_store arm. All three prove now and
            # run as ordinary prp-equiv-* pairs.
            # NOTE: the six latch_* pairs (latch_enable_hold, latch_hold_basic,
            # latch_active_low, latch_master_slave, latch_icg, latch_mixed_flop)
            # used to be excluded here as todo/livehd/2f-latch trackers. They
            # were PROMOTED into the glob (2026-07-20): lgcheck's cascade ends
            # in a bounded miter that DECIDES latch designs (equiv_simple alone
            # cannot discharge $dlatch), so the forward equivalence proves
            # today. The still-broken latch surfaces (sim, native cvc5 LEC,
            # Verilog round-trip through slang) are tracked by the sim/verify
            # fixtures and lhd/tests scripts named in todo/livehd/2f-latch.html,
            # NOT by these equiv pairs.
            # NOTE: struct_init_whole_read / instance_conn_struct_ident /
            # struct_selfref_pattern / instance_out_struct_ident used to be
            # excluded here (their SystemVerilog '{...} / struct-typed-wire
            # goldens exceed yosys read_verilog, so the forward miter was
            # INCONCLUSIVE). They now run in the glob (2026-07-03): the header
            # `:gold_reader: slang` has lgcheck read the golden through the
            # yosys-slang plugin, and each golden's top is an UNDOTTED plain
            # identifier (dotted escaped `\a.b ` names break yosys-slang's
            # --top/RTLIL naming).
        ],
    )
]

# Known-failing equiv tracker (fixme): a MISCOMPILE that compiles clean but LECs
# wrong, so it cannot live in tests/fixme/ (compile-only). Excluded from the
# globs above; tagged `fixme` so `bazel test //... (-fixme)` stays green. Run it:
#   bazel test //inou/prp:prp-equiv-signed_one_bit_cmp --test_tag_filters=
sh_test(
    name = "prp-equiv-signed_one_bit_cmp",
    srcs = ["tests/pyrope_test.py"],
    args = ["-i", "$(location //inou/prp:tests/equiv/signed_one_bit_cmp.prp)"],
    data = [
        "//inou/prp:tests/prplib.py",
        "//inou/prp:tests/pyrope_test.py",
        "//inou/prp:tests/equiv/signed_one_bit_cmp.prp",
        "//inou/prp:tests/equiv/signed_one_bit_cmp.v",
        "//lhd",
    ],
    deps = ["//inou/yosys:scripts"],
)

[
    sh_test(
        name = "prp-equiv-%s" % base,
        srcs = ["tests/pyrope_test.py"],
        args = ["-i", "$(location //inou/prp:tests/equiv/%s.prp)" % base],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:tests/equiv/%s.prp" % base,
            "//inou/prp:tests/equiv/%s.v" % base,
            "//lhd",
        ],
        deps = ["//inou/yosys:scripts"],
        tags = ["fixme"],
    )
    for base in [
        # async_reset_enable was PROMOTED out of this list (2026-07-03): the
        # slang reader demotes an edge-triggered reset with no extractable
        # async rung to a synchronous one (async-reset-as-sync; sound because
        # LEC/sim observe state only after clock updates), and the pair runs
        # as :type: equiv_slang since yosys read_verilog hard-errors on the
        # compound `if (reset || arst)` guard (proc_dff multi-edge).
        # The six latch_* pairs were PROMOTED out of this list (2026-07-20):
        # rewritten to `pub mod` + the shipped `reg NAME:TYPE:[latch=true]`
        # spelling (latch_enable_hold included), and the lgcheck bounded-miter
        # stage proves every pair today — see the equiv-glob note above.
        # struct_init_whole_read / instance_conn_struct_ident /
        # struct_selfref_pattern / instance_out_struct_ident were PROMOTED out
        # of this list (2026-07-03): `:gold_reader: slang` reads their goldens
        # through the yosys-slang plugin (see the equiv-glob note above), so
        # the forward direction proves; they run as ordinary prp-equiv-* now.
        #
        # The three comb-array store-loss trackers (array_store_stale_rhs,
        # arr_comb_mux_const, arr_comb_tmp) were PROMOTED out of this list
        # (2026-07-27). One root cause covered all three: upass.ssa cleared
        # has_dest for a >=3-child (tuple_set) store — correct, its DEST must
        # not be versioned — but then let the node fall through to the
        # structural verbatim copy, so its INDEX and VALUE operands never
        # followed rename_map. A reassigned `mut` therefore reached the store
        # din on its stale base name, constprop folded that to the declaration
        # value and DCE deleted the real write. upass_ssa.cpp now routes such a
        # store through the same target-verbatim/operands-renamed arm as
        # tuple_get/tuple_add/attr_*. All three prove; arr_comb_tmp also left
        # _V2PRP2V_VERILOG_FIXME. Surfaced by lhdsuite //bench:minion_lec
        # (minion_dcache_tensor_load{,_p1}); see lhdsuite fixme.md 1h.
        #
        # ── corpus-mined category trackers (2026-07-29) ──────────────────────
        # One minimal repro per remaining hard-failure category found by the
        # ChiGen/ChiBench sweep. Each .v is <= 25 lines with 1-4 bit signals and
        # (where an array is needed) at most 4 entries of 4 bits, so the whole
        # set costs well under a second. Every one FAILS today -- run them with
        #   bazel test //inou/prp:prp-equiv-latch_rule_b --test_tag_filters=
        # The root cause is written at the top of each .v/.prp pair.
        "latch_rule_a",          # self-timed gate: enable reads its own Q
        "latch_rule_b",          # exhaustive `case` mis-inferred as a latch (21 corpus files)
        "dual_async_set_reset",  # a flop needs TWO async rungs; the cell has one slot
        # The three pure prp_writer trackers were all PROMOTED out (2026-07-29) --
        # writer_undefined_mux (undeclared `_mux_N`), tolg_field_read_zero (the
        # nested-mut hoist dropped an array's type AND its `initial` contents) and
        # writer_bad_char (module names were not backtick-escaped). Each proves in
        # all three directions now; the writer half of the last two is pinned as a
        # TEXT gate by //pass/prp_writer:prp_writer_decl_emit_test.sh.
        "reg_clock_from_logic",  # `clk ^ inv` clock rejected by tolg's whitelist
        # LEC-REFUTED categories (these COMPILE clean -- the miscompile is silent):
        "mem_write_during_reset",   # memory write not gated by the async reset
        "async_mem_reset_demoted",  # async reset OF a memory demoted to sync
        "reg_decl_init_dropped",    # `reg x = 1'b1;` power-on value dropped
        "selfref_slice_assign",     # concurrent slice assigns solved in source order
        "selfref_concat_assign",    # self-referential whole-vector assign never driven
        # The three formal phase-scheduler trackers (phase_latch_flop_edges,
        # phase_hier_latch_neg, phase_clock_gate_chain) were PROMOTED out of
        # this list (2f-lec / 2f-latch M10): the encoder now schedules latches
        # and negedge endpoints over four microsteps per source period, so all
        # three PROVE. See the equiv-glob note above for why they run under
        # `:equiv_engine: cvc5`.
    ]
]

# Native-Pyrope check within v2prp2v. The slang reader lowers each golden
# <name>.v to Pyrope (upass/prp_writer); `lhd lec` recompiles that Pyrope and
# proves it equivalent to the ORIGINAL hand-written <name>.prp
# (NOT the .v) via the in-process cvc5 BMC engine (the default since the
# ind->bmc flip). Proving against the .prp directly sidesteps yosys read_verilog
# gaps on the reference side (e.g. the `'{...}` assignment pattern that blocked
# packed_assign/bitrange_dyn under the old lgyosys/.v gate, both of which now
# pass). Per-side tops come from the reference's :verilog_top: (the emitted
# module) and :pyrope_top: (the original lambda) headers; a timeout is
# inconclusive (not a fail). Driven off the .v files (every pair ships one).
# 129/138 prove today.
#
# The remaining gaps still REFUTE for reasons OUTSIDE the prp_writer (the emitted
# Pyrope is correct — lgyosys proves every one of them equivalent). They are
# tagged `fixme` (the repo's known-broken convention; .bazelrc filters `-fixme`
# so `bazel test //...` stays green) with the specific root cause; flipping one
# green is the fix:
#   bazel test //inou/prp:prp-v2prp2v-mem_rom --test_tag_filters=
_V2PRP2V_NATIVE_FIXME = [
    # The SIX phase_* trackers and latch_always_transparent were PROMOTED out of
    # this list (2f-lec / 2f-latch M10 landed). Every refusal they existed to
    # record is gone: `pass.single_edge` is no longer MANDATORY (a decline hands
    # the design to the encoder's read-only four-microstep schedule instead of
    # ending the run), so a latch or negedge flop inside a def, two clock ports
    # tied to one net at every instantiation site, a memory that a rise batch
    # writes and a fall batch reads, a chained clock gate, an active-low
    # (`clk | ~en`) gate whose enable must be sampled before the FALL, and a
    # latch closing on the very edge its reader samples all prove now.
    # latch_enable_hold was PROMOTED out of this list (2026-07-20): the shipped
    # `reg NAME:TYPE:[latch=true]` spelling IS the Pyrope source equivalent of
    # an inferred latch, the fixture was rewritten to it, and the reverse
    # direction passes (the lec leg is honest-inconclusive under cvc5 until the
    # native encoder learns latches — 2f-latch M4).
    # async_reset_enable was PROMOTED out of this list (2026-07-03): the slang
    # reader now demotes an edge-triggered reset with no extractable async rung
    # to a synchronous one (async-reset-as-sync warning), so the .v round-trips.
    # instance_out_struct_ident (instance-output struct dual identity,
    # ExeUnitImp_4 / fp-convert family) was PROMOTED out of this list:
    # assign_to splits a whole value onto the bundle leaves, so the emitted
    # .prp text round-trips (2026-07-03).
    # instance_collapse_order (hierarchical-LEC box-instance pairing) was
    # PROMOTED out of this list: stateless collapsed leaves are now pairing-free
    # (per-def UF congruence), stateful ones pair name-first, and a collapsed-box
    # REFUTE is flat-confirmed before it can fail — see pass/lec/encode.hpp
    # (Comb_box / set_box_keys) and lec_hierarchical's flat confirmation.
    #
    # array_store_stale_rhs was PROMOTED out of this list (2026-07-27): the
    # reference .prp used to recompile through the comb-array store-loss bug, so
    # the (correct) impl side refuted against it. The bug was upass.ssa dropping
    # the rename on a tuple_set store's operands — not a tolg store-din gap, as
    # this entry previously guessed. See the equiv note above.
    # mem_ordering_none was PROMOTED out of this list (2026-07-29): the formal
    # `?` half of the ordering ruling landed. `ordering="none"` now drives a real
    # per-(read,write) `undef` matrix on the Memory cell (graph/cell.cpp pid 15)
    # instead of an all-zeros `fwd` that meant defined-OLD, so the REFERENCE side
    # carries an X bit-plane over the collision window and the existing
    # formal.lec.gold_x=ignore miter excludes it. (The old diagnosis here — "cvc5
    # grounds the x" — was wrong: the golden's `2'bxx` is on the IMPL side, and
    # gold_x masks ref-x only, so nothing was ever excluded.) The reader's
    # "no forwarding, but DEFINED" state moved to the new ordering="old".
    # latch_always_transparent was PROMOTED out of this list (2f-lec M10): an
    # ALWAYS-transparent latch (every path writes it, so tolg wires no `enable`)
    # stores nothing, and the phase schedule now classifies it as exactly that —
    # a COMBINATIONAL BUFFER, encoded as `q = din` rather than as a state cut.
    # pass.single_edge still declines it ("unresolved-commit-class"), but a
    # decline is no longer fatal: the schedule takes over and the pair proves.
    #
    # ── corpus-mined category trackers (2026-07-29) ──────────────────────────
    # One minimal repro per remaining hard-failure category from the
    # ChiGen/ChiBench sweep; the root cause is written at the top of each pair.
    # ALL NINE broke this direction (verilog -> Pyrope, then LEC against the
    # reference .prp) at check-in: three fail the READ itself, six emit Pyrope
    # that does not recompile. All THREE pure prp_writer trackers were PROMOTED
    # out (2026-07-29) -- the writer no longer drops the definition of a `_mux_N`
    # whose consuming store an enclosing mux collapse already folded
    # (writer_undefined_mux), no longer drops a hoisted nested `mut`'s type and
    # comptime init (tolg_field_read_zero), and backtick-escapes module names
    # (writer_bad_char). All three re-parse and this direction proves for each.
    "latch_rule_a",
    "latch_rule_b",
    "dual_async_set_reset",
    "reg_clock_from_logic",
    "negative_array_index",
    "blocking_ff_state",
]

# Two goldens cannot be checked against their original Verilog: both available
# yosys readers reject the compound async-reset edge form. Keep the native-only
# v2prp targets for those cases; every other former v2prp target is folded into
# the v2prp2v family below.
[
    sh_test(
        name = "prp-v2prp-%s" % base,
        srcs = ["tests/v2prp_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:tests/equiv/%s.v)" % base,
        ],
        data = [
            "//inou/prp:tests/v2prp_test.py",
            "//inou/prp:tests/equiv/%s.v" % base,
            "//inou/prp:tests/equiv/%s.prp" % base,
            "//lhd",
        ],
        deps = ["//inou/yosys:scripts"],
    )
    for base in [
        "async_negreset_compound",
        "async_reset_enable",
    ]
]

# v2prp2v: the Verilog ROUND TRIP over the same equiv corpus. Each golden <name>.v
# goes all the way THROUGH Pyrope and back --
#   v -> (slang) -> lg -> prp_writer -> .prp -> (inou.prp) -> tolg -> cgen -> v
# -- and inou/yosys/lgcheck proves the result equivalent to the ORIGINAL .v.
# The Pyrope legs were added 2026-07-29: the harness used to go slang -> lg ->
# cgen DIRECTLY, skipping upass/prp_writer and the Pyrope re-read, so a writer
# that emitted lossy or un-re-parseable Pyrope passed here while failing
# everywhere else (three such trackers had to be left untagged for exactly that
# reason; they refute now). One run therefore covers the whole path the corpus
# fuzzing exercises. This is the only harness whose reference side is read
# INDEPENDENTLY of LiveHD (yosys read_verilog, or yosys-slang via the sibling
# .prp's `:gold_reader: slang` header). The harness also runs the former
# v2prp native LEC check against the hand-written .prp, reusing the emitted
# Pyrope rather than repeating the conversion. lgcheck INCONCLUSIVE (exit 2)
# and timeouts pass with an honest note; only a counterexample or a hard yosys
# error fails.
#
# Census 2026-07-27 (lhdsuite array_problem.md): 169 of 179 pairs prove, 5 are
# honest-inconclusive (array_selfref, assign_pattern, io_bundle_nested,
# io_bundle_selfref, packed_assign), 4 are real round-trip miscompiles tracked
# below, 2 cannot be compared by any available reference reader (excluded).
_V2PRP2V_VERILOG_FIXME = [
    # bitrange_dyn was PROMOTED out of this list (2026-07-29). It tracked "z
    # reads 0 where the golden keeps bits (witness a=0x100 base=7 len=7 ->
    # gold z=2, gate z=0)", and the cause was NOT the range lowering: LiveHD's
    # IR was right (the shift-amount pin carries bits:5). cgen folded that
    # amount into the shift as text, and Verilog SELF-determines a shift's
    # amount operand, so the inlined `(len_u + 1)` evaluated at 3 bits and
    # wrapped 8 to 0. Cgen_verilog::shift_amount_expr now pads it. The new
    # bitrange_dyn_narrow pair gates the same hole on the NATIVE `#[lo..=hi]`
    # path, which bitrange_dyn's own widths happened to hide.
    # tup_in_port / tuple_in_port were PROMOTED out of this list (2026-07-29):
    # they tracked the DIRECT slang -> lg -> cgen path leaving `res` UNDRIVEN
    # (x) where the golden drives a value. Routing this harness through Pyrope
    # (v -> prp -> v) proves both — the writer/recompile legs reconstruct the
    # struct port binding the direct path dropped.
    # arr_comb_tmp was PROMOTED out of this list (2026-07-27): the `t = d` drop
    # was upass.ssa leaving a tuple_set store's VALUE operand on its stale base
    # name, not a slang front-end drop — see the equiv note above.
    #
    # ── corpus-mined category trackers (2026-07-29) ──────────────────────────
    # ALL NINE repros (see _V2PRP2V_NATIVE_FIXME). The three pure prp_writer bugs
    # (tolg_field_read_zero, writer_undefined_mux,
    # writer_bad_char) used to PASS here and were deliberately left
    # untagged, because this direction was the DIRECT slang -> lg -> cgen path
    # and never ran the writer. v2prp2v_test.py now routes through Pyrope
    # (v -> prp -> v), so the writer IS exercised and all three refuted --
    # exactly the point of the change, and all three are FIXED and PROMOTED out
    # (2026-07-29). writer_undefined_mux: the undeclared `_mux_N` read is fixed
    # (mux temporaries keep their own assignment; the writer no longer searches
    # every store for an inline consumer). tolg_field_read_zero: the nested-mut hoist rebuilt the
    # declaration from the NAME alone, so a `mut mem:[2]u4 = (1, 2)` declared
    # inside an `if` came back as `mut mem = 0` -- type and contents both gone.
    # writer_bad_char: module names now go through the same backtick escaper
    # signal names always did. Each pair's header states the root cause; the
    # writer half of the last two is pinned as a TEXT gate by
    # //pass/prp_writer:prp_writer_decl_emit_test.sh.
    "latch_rule_a",
    "latch_rule_b",
    "dual_async_set_reset",
    "reg_clock_from_logic",
    "negative_array_index",
    "blocking_ff_state",
    # The five LEC-REFUTED trackers. This is the only direction that catches
    # them: they COMPILE clean both ways, so the native Pyrope check trivially
    # passes, and prp-equiv-* only sees four of the five.
    "mem_write_during_reset",
    "async_mem_reset_demoted",
    "async_reset_pin_wire",
    "reg_decl_init_dropped",
    "selfref_slice_assign",
    "selfref_concat_assign",
]

[
    sh_test(
        name = "prp-v2prp2v-%s" % base,
        srcs = ["tests/v2prp2v_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:tests/equiv/%s.v)" % base,
        ],
        # The sibling .prp supplies the headers and is the native LEC reference.
        data = [
            "//inou/prp:tests/v2prp2v_test.py",
            "//inou/prp:tests/equiv/%s.v" % base,
            "//inou/prp:tests/equiv/%s.prp" % base,
            "//lhd",
        ],
        tags = ["fixme"] if (
            base in _V2PRP2V_NATIVE_FIXME or
            base in _V2PRP2V_VERILOG_FIXME
        ) else [],
        deps = ["//inou/yosys:scripts"],
    )
    for base in [
        f.split("/")[-1][:-2]
        for f in glob(
            ["tests/equiv/*.v"],
            exclude = [
                # generated differential-sim golden drivers, not equiv goldens
                "tests/equiv/*_tb.v",
                # No available reference reader can ingest these two goldens:
                # yosys read_verilog hard-errors on the compound async-reset
                # edge list ('Multiple edge sensitive events'), and yosys-slang
                # fails compilation on them too — the round trip cannot even
                # be compared. Their coverage stays with the equiv_slang /
                # v2prp directions.
                "tests/equiv/async_negreset_compound.v",
                "tests/equiv/async_reset_enable.v",
            ],
        )
    ]
]

# lhd-sim known-failing trackers. These are separate from `prp-equiv-*`: the
# paired .prp/.v files live in tests/equiv and LEC where possible, but these
# fixme targets exercise the actual SIM failure category.
[
    sh_test(
        name = "prp-simfail-%s" % name,
        srcs = ["tests/equiv_sim_fail_test.py"],
        args = [
            "--mode",
            mode,
            "--prp",
            "$(location //inou/prp:tests/equiv/%s.prp)" % name,
        ],
        data = [
            "//inou/prp:tests/equiv_sim_fail_test.py",
            "//inou/prp:tests/equiv/%s.prp" % name,
            "//inou/prp:tests/equiv/%s.v" % name,
            "//inou/prp:sim_runtime_hdrs",
            "//lhd",
        ],
        deps = ["//inou/yosys:scripts"],
        # sim_lg_atomic_feedback now compiles: cgen_sim flattens the false-loop
        # pure-comb sub-instance (flatten_false_loop_subs). sim_prp_packed_selfref
        # now compiles too: cgen_sim splits the false WORD-level packed-wire cycle
        # (split_packed_selfref_wires). The rest are still gaps.
        tags = [] if name in (
            "sim_lg_atomic_feedback",
            "sim_prp_packed_selfref",
            # 2026-07-03: the generalized slice-resolution engine in
            # split_packed_selfref_wires (footprint 1-bit/And-const/Mux rules +
            # recursive resolve: bitwise distribute, SHL/SRA re-basing, offset
            # Get_mask descent, Sum-as-disjoint-Or, And(SRA(w,k),m) readers)
            # fixed the whole packed-word false-loop family. Each pair is also
            # in _SIMEQ (iverilog-vs-lhd-sim value check).
            "sim_packed_selfref_1bit",
            "sim_loop_sticky_ripple",
            "sim_loop_pack_shiftand",
            "sim_loop_packed_bus_group",
            "sim_loop_mux1h_selfslice",
            "sim_loop_sbox_bit_selfref",
            "sim_loop_mem_prefetch",
            # class-B sub-feedback: nested pure-comb closures now FLATTEN
            # recursively, and a stateful MOORE callee (no comb in->out path)
            # gets peek()-pre-bound outputs + a deferred cycle(in) call.
            "sim_sub_stateful_feedback",
            "sim_sub_nested_comb_feedback",
            # 2026-07-03: instance-output dual identity fixed — assign_to
            # splits the bound value onto the struct's bundle leaves, so the
            # emitted .prp recompiles to the same sim (v2prp-sim mode green).
            "instance_out_struct_ident",
            # 2026-07-03: resolve() distributes a slice through Mux arms
            # (GM(mux(s,x,y),m) == mux(s, GM(x,m), GM(y,m)); an on-cycle
            # selector resolves as its own full-width slice).
            "sim_loop_mux_arm",
            # 2026-07-03: per-OUTPUT-cone deferral (Stage-2) — a stateful
            # MEALY callee whose FED-BACK outputs are all pure state reads
            # gets those outputs peek()-pre-bound; the atomic cycle(in) call
            # orders normally with its input cone emitted on demand.
            "sim_sub_mealy_feedback",
        ) else ["fixme"],
    )
    for name, mode in {
        "sim_prp_packed_selfref": "prp-emit",
        "sim_lg_atomic_feedback": "lg-emit",
        # SIM-side pin of the fp-convert fail family (FCVT/FPCVT/FP_INCVT/
        # CVT32ModuleS0/ByteMaskTailGen — instance-output struct dual
        # identity). FIXED 2026-07-03 (assign_to splits the bound value onto
        # the bundle leaves); green together with its prp-v2prp2v companion.
        "instance_out_struct_ident": "v2prp-sim",
        # ---- 2026-07-03 lhd-sim failure-class synthetics (verification.html
        # sim column, ~63 comb-loop errors + 3 sub-feedback + 2 segv). Each
        # pins ONE distinct gap; all pairs are bit-level acyclic and LEC-proven
        # (lgyosys) — the loop exists only at cgen.sim's word/instance
        # granularity. Promote into _SIMEQ (differential sim) when fixed.
        #
        # split_packed_selfref_wires gaps (inou/cgen/cgen_sim.cpp):
        # 1-bit fed-back packed field: footprint() bails on any single-bit Or
        # operand (signed -1/0 "unbounded", ~685; generic bound needs bits>1,
        # ~698) — one bail vetoes ALL rewires. BusyTable/BranchUnit/EnqEntry/
        # EnqPolicy/OthersEntry valid-bit families (~40 modules).
        "sim_packed_selfref_1bit": "prp-emit",
        # same footprint-1bit bail exercised as a bit k -> k+1 RIPPLE chain
        # (FloatFMA sticky vecs): every bit of the pack reads the previous
        # bit's slice, so the fix must redirect a CHAIN of slice-reads.
        "sim_loop_sticky_ripple": "prp-emit",
        # reader-form regression: readers spelled `(w>>k)&m` are SRA+And nodes;
        # the splitter now recognizes them alongside const-mask Get_mask reads
        # (CSRPermitModule/Debug/DecodeUnit family).
        "sim_loop_pack_shiftand": "prp-emit",
        # hierarchically GROUPED pack: the covering operand `(group)<<k` is
        # itself a still-cyclic sub-pack, and the one-shot redirect cannot
        # recurse — this is the family whose back-edge label is `shl_*`
        # (RegCacheDataModule_1/OldestArbiter/DataPath).
        "sim_loop_packed_bus_group": "prp-emit",
        # And-masked Mux1H arms: trace_to_or fails when the packed word's
        # driver is an And / Or-of-Ands with bit-replicated one-hot masks
        # (fpsqrt_r16_block nxt_rt speculation words, FDivSqrt/ExeUnitImp_10).
        "sim_loop_mux1h_selfslice": "prp-emit",
        # `wire` whole-vector direct self-read across TWO nets (riscv-crypto
        # sbox linear layer, BlockCipherModule o_36/t_36): mutual word-level
        # false loop, no Or-of-disjoint pack to trace at all.
        "sim_loop_sbox_bit_selfref": "prp-emit",
        # packed-wire false cycle feeding a comb-array READ ADDRESS: pins the
        # BusyTable_1/Dispatch class-C SIGSEGV (ensure_ready_impl unbounded
        # recursion — crash itself fixed by the prefetch_seen guard; the
        # residual loud comb-loop error keeps this fixme until the splitter
        # handles the Sum-packed word).
        "sim_loop_mem_prefetch": "prp-emit",
        # flatten_false_loop_subs gaps (comb-loop-through-instance survivors):
        # STATEFUL callee — the fed-back output is a pure register read, but
        # callee_is_pure_comb() rejects any callee holding a Flop
        # (DivUnit/SRT16DividerDataModule, ExeUnitImp_2).
        "sim_sub_stateful_feedback": "prp-emit",
        # NESTED pure-comb callee — zero flops anywhere, but the callee holds
        # a comb Sub and callee_is_pure_comb() rejects Ntype_op::Sub; the
        # flatten is single-level non-recursive (ExeUnitImp_4/Alu).
        "sim_sub_nested_comb_feedback": "prp-emit",
        # ---- 2026-07-03b: the last two NON-latch sim-engine gaps, BOTH FIXED
        # the same day (untagged above; each also in _SIMEQ):
        # Mux-arm slice distribution: a packed word driven by a MUX whose arms
        # pack a field computed from a slice of the same word (the
        # BlockCipherModule/round-chaining residual: Bku/CryptoModule/
        # ExeUnitImp_6/ExuBlock). resolve() distributes
        # GM(mux(s,x,y),m) == mux(s, GM(x,m), GM(y,m)).
        "sim_loop_mux_arm": "prp-emit",
        # per-OUTPUT-cone deferral (Stage-2): a stateful MEALY callee (one
        # reg-read output on the feedback, one comb in->out output) defeats
        # the whole-callee Moore check (CSR/NewCSR::MipModule, DivUnit/SRT16).
        # Pre-binding ONLY the input-independent outputs un-cycles the
        # schedule; the atomic call orders normally.
        "sim_sub_mealy_feedback": "prp-emit",
    }.items()
]

# prp-yvr: the `--reader yosys-verilog` direction. The slang/pyrope tests above
# CANNOT catch the XiangShan MISCOMPILE class whose root cause is LiveHD's
# yosys-verilog front end (slang reads it correctly — verified sl=OK yv=BAD by
# xs/attribute2.sh), so this drives each minimal golden through
# `--reader yosys-verilog` and LECs the result vs the source. clz_nest (a deep
# nested-ternary terminal constant, the CLZ_28/lzc/CMOUnit cluster) is the seed
# regression and now PROVES; add more yosys-verilog reader repros here.
[
    sh_test(
        name = "prp-yvr-%s" % base,
        srcs = ["tests/yvr_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:tests/equiv/%s.v)" % base,
        ],
        data = [
            "//inou/prp:tests/yvr_test.py",
            "//inou/prp:tests/equiv/%s.v" % base,
            "//lhd",
        ],
        deps = ["//inou/yosys:scripts"],
    )
    for base in [
        "clz_nest",  # deep nested-ternary terminal constant (now proven)
    ]
]

# prp->prp round-trip: a Pyrope source re-emitted through pass.prp_writer must
# recompile and LEC against its golden .v (the FORWARD companion of v2prp2v).
# This locks in the constructs the writer fully supports — the pipeline
# (`stage[N]` / `@[N]`), `type_spec`, `tuple_concat`.  Curated allow-list: the
# prp->prp writer path is still maturing (many equiv pairs emit clean Pyrope
# that is not yet logically equivalent, and a handful hit unimplemented nodes
# the safety net now hard-fails — both tracked in todo/livehd/2w-prp_writer).
# Add a name here once its prp->prp round-trip LEC-passes.
[
    sh_test(
        name = "prp-p2p-%s" % base,
        srcs = ["tests/p2p_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:tests/equiv/%s.prp)" % base,
        ],
        data = [
            "//inou/prp:tests/p2p_test.py",
            "//inou/prp:tests/equiv/%s.prp" % base,
            "//inou/prp:tests/equiv/%s.v" % base,
            "//lhd",
        ],
        deps = ["//inou/yosys:scripts"],
    )
    for base in [
        "mod_mul_add",         # top discovers its two emitted sibling pipes
        "mod_twopaths",        # registered-output timing regression (R8 A)
        "mod_varargs_add",     # monomorphized sibling uses a bare import alias
        "mod_varargs_csa",     # nested monomorphized sibling callees
        "pipe_varargs_add",    # pipe form of the monomorphized sibling path
        "mod_call_pipe",       # pipe inlined into a mod: stage[N] + @[N]
        "pipe1_pass",          # bare pipe passthrough
        "pipe2_two_out",       # multi-output pipe
        "pipe3_mul",           # multi-stage arithmetic pipe
        "pipe_range",          # pipe[A..=B] range depth
        "pipe_state_acc",      # pipe with feedback state
        "mod_delay3",          # @[N] landing-cycle on a mod output
        "reg_out_feedforward",  # feedforward reg output
    ]
]

# Differential simulation equivalence (prp-simeq-<name>): for each equiv pair a
# generated `<name>_tb.v` (Icarus) + `<name>_tb.prp` (`lhd sim`) drive the DUT
# and its golden Verilog with the SAME 64-bit-LCG constrained-random stimulus for
# 1000 cycles and fold every cycle's outputs into one 62-bit rolling signature.
# The `<name>_tb.prp` (a `:type: simulation` test that imports the `pub` DUT
# lambda) bakes the Icarus golden signature and asserts equality -- so this is the
# `lhd sim` C++ codegen (inou.cgen.sim) checked against an independent oracle.
#
# TWO PHASES (see tests/equiv/README_simeq.md): SETUP regenerates both `_tb`
# files + the baked golden via `inou/prp/tests/gen_equiv_tb.py --setup` (needs
# iverilog; only when a pair is added or its logic/interface changes). The
# regression below is phase RUN -- it recompiles the DUT to C++ and checks the
# baked golden, no Verilog needed. Same hermetic host-compile as prp-sim (the
# `sim_runtime_hdrs` headers; NO nested bazel). The `_tb.prp` imports its sibling
# `<name>.prp`, so BOTH are runfiles.
_SIMEQ = [
    "arr2d_dyn_sel", "array_selfref", "async_reset_enable", "bit_sel_sext", "bitrange_dyn", "bitrange_open",
    "bitsel_dyn", "bitsel_idx", "bitset_gap", "bitset_imm", "bitset_mut", "bitset_nil",
    "bitset_reg", "clz_nest", "codeblock_expr", "comb_single_out_op", "cond_modified_read",
    "condout_const_arms", "condout_mixed_arms", "condout_seed_uncertain", "dyn_tuple_const",
    "dyn_tuple_idxexpr", "dyn_tuple_mixed", "dyn_tuple_mut_ro", "dyn_tuple_pair",
    "dyn_tuple_sel", "flop_reset_matrix", "gen_type_cast", "generic_mod", "hier_value", "hotmux_match", "hotmux_match_partial",
    "hotmux_unique", "if_expr_rhs", "implies_cond", "implies_ops", "lead_or_chain", "lg_rename", "loop_accum",
    "match_const_else", "match_no_else", "mem_const_idx", "mem_init", "mem_init_scalar",
    "mem_init_tuple", "mem_pending_sync_init", "mem_rom", "mem_whole_cond_const",
    "mem_write_only", "mod_acc_state", "mod_call_mod", "mod_call_pipe", "mod_call_pipe2",
    "mod_comp", "mod_counter", "mod_delay3", "mod_lower", "mod_mixed_out", "mod_mul_add",
    "mod_mut_delay", "mod_mux_aligned", "mod_passthru", "mod_template", "mod_three",
    "mod_twopaths", "mod_varargs_add", "mod_varargs_csa", "multi_out_comb", "overload_dispatch",
    "overload_return_dispatch", "packed_assign", "pipe1_pass", "pipe2_two_out", "pipe3_mul",
    "pipe_bare", "pipe_range", "pipe_stage_reg", "pipe_state_acc", "pipe_varargs_add",
    "const_bit_select",
    "mem_comptime_init", "mem_comptime_init_inferred", "mem_multidim_init", "mem_multidim_rw",
    "mem_mut_array", "mem_pending_reset_restore", "mem_whole_coexist", "mem_whole_comb",
    "mem_whole_reg",
    "reduce_and", "reduce_or", "reduce_popcount", "reduce_xor", "reg_async_flag",
    "reg_attr_override", "reg_delay_init", "reg_hold_empty_elif", "reg_hold_match",
    "reg_hold_no_else", "reg_out_feedforward", "reg_tuple_reset", "rt_bool_cast",
    "rt_int_output", "rt_sat_s", "rt_sat_u", "rt_typecast", "rt_wrap_noop", "rt_wrap_reassign",
    "rt_wrap_s", "rt_wrap_sub", "rt_wrap_sum", "rt_wrap_u", "shift_combo_i8", "shift_combo_i8b",
    "sim_lg_atomic_feedback", "sim_prp_packed_selfref",
    "sim_packed_selfref_1bit", "sim_loop_sticky_ripple", "sim_loop_pack_shiftand",
    "sim_loop_packed_bus_group", "sim_loop_mux1h_selfslice", "sim_loop_sbox_bit_selfref",
    "sim_loop_mem_prefetch", "sim_sub_stateful_feedback", "sim_sub_nested_comb_feedback",
    "sim_loop_mux_arm", "sim_sub_mealy_feedback",
    "shift_nested_sra", "shift_sra_unsigned", "signed_one_bit_cmp", "splice_merge", "trivial_if",
    "varargs_add", "wire_comb", "wire_reg", "wire_reset", "wire_ring", "wire_typed",
]

# Known-failing differential-sim trackers (tagged `fixme` so `bazel test //...`
# (-fixme) stays green). Each is a real inou.cgen.sim gap the harness SURFACES --
# fixing the codegen (not the test) flips it green:
#   * sim_lg_atomic_feedback : a false combinational loop through an ATOMIC comb
#       Sub instance (its output feeds back into one of its own inputs via parent
#       logic). Needs Stage-1 selective flatten of the offending instance before
#       cgen.sim. Tracked in cgen_sim_false_comb_loop.
#   * sim_prp_packed_selfref (NOW FIXED, promoted to _SIMEQ): a false WORD-level
#       cycle -- a packed value whose read slice and written slice are independent,
#       but the sim scheduled the whole packed word as one object. cgen_sim now
#       redirects each disjoint Get_mask slice-read of an Or-of-disjoint-ranges net
#       to the single operand driving it (split_packed_selfref_wires).
#   * async_reset_enable (NOW FIXED, promoted to _SIMEQ): the golden's reset
#       is edge-triggered under a compound guard; the slang reader demotes it
#       to a synchronous reset (async-reset-as-sync), and the _tb.prp import
#       binding no longer collides with the `pipe` keyword (gen_equiv_tb.py
#       PRP_KEYWORDS guard -> `pipe_`).
#   * gen_type_cast (NOW FIXED, promoted to _SIMEQ): the generic `T(a)`
#       constructor cast now binds+lowers (todo 3g subtask A).
# Run one: bazel test //inou/prp:prp-simeq-sim_lg_atomic_feedback --test_tag_filters=
_SIMEQ_FIXME = []

[
    sh_test(
        name = "prp-simeq-%s" % base,
        srcs = ["tests/pyrope_test.py"],
        args = ["-i", "$(location //inou/prp:tests/equiv/%s_tb.prp)" % base],
        data = [
            "//inou/prp:tests/prplib.py",
            "//inou/prp:tests/prpsim.py",
            "//inou/prp:tests/pyrope_test.py",
            "//inou/prp:tests/equiv/%s_tb.prp" % base,
            "//inou/prp:tests/equiv/%s.prp" % base,
            "//inou/prp:sim_runtime_hdrs",
            "//lhd",
        ],
        tags = [] if base in _SIMEQ else ["fixme"],
    )
    for base in _SIMEQ + _SIMEQ_FIXME
]
