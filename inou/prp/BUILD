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
        "tests/pyrope/abc_comb.prp",
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
        exclude = ["*test*.cpp"],
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
cc_test(
    name = "prpparse_stream_test",
    srcs = ["prpparse_stream_test.cpp"],
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
# verification extras (requires/ensures), and testbench extras (step/peek/poke).
# (The `lg` attribute used to be on this list but is now implemented — its TBD
# markers were removed from the docs.) The `macro=` memory-compiler binding is
# also still TBD but is silently accepted-and-ignored — it compiles clean today
# (no diagnostic), so it needs an emitted-Verilog grep harness, not a compile
# test, and is tracked in todo/pyrope/3f-macro.html instead.
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
        # verif_requires + testbench_step + temporal_past now lower/run; the rest
        # are still TBD. (temporal_past: `past[N](x)` lowers to a depth-N `stage`
        # shift register — see prp2lnast function_call_expr_to_node.)
        tags = [] if base in ("verif_requires", "testbench_step", "temporal_past") else ["fixme"],
    )
    for base in [
        "temporal_past",
        "stdlib_import",
        "verif_requires",
        "testbench_step",
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
    )
    for prp_file in glob(["tests/sim/*.prp"])
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
            # gen_type_cast: DELIBERATELY-broken generics repro — the `T(a)`
            # constructor on a generic param does not lower yet (see the file
            # header, tracked by 3g). It cannot compile, so the forward equiv
            # test can never pass; run it via the fixme target below. The reverse
            # direction is already fixme-tagged through _V2PRP_FIXME.
            "tests/equiv/gen_type_cast.prp",
            # Known-failing semantic trackers. Run via the fixme targets below.
            "tests/equiv/async_reset_enable.prp",
            "tests/equiv/latch_enable_hold.prp",
            # struct_init_whole_read: the golden's nested-struct '{...} net
            # initializers exceed yosys read_verilog, so the forward lgcheck
            # miter never completes (INCONCLUSIVE, not a refute). The reverse
            # prp-v2prp-struct_init_whole_read (cvc5, slang-read) is the real
            # guard for the slang net-initializer store-drop fix and passes.
            "tests/equiv/struct_init_whole_read.prp",
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

# Known-failing equiv tracker (fixme): gen_type_cast.prp is a DELIBERATELY-broken
# generics repro — the `T(a)` constructor on a generic parameter does not lower
# yet (the binding pass leaves it as a call to `inl1_T`; see the file header,
# tracked by 3g). The .prp cannot compile, so the forward equiv pair can never
# pass. Excluded from the glob above; tagged `fixme` so `bazel test //... (-fixme)`
# stays green. Run it: bazel test //inou/prp:prp-equiv-gen_type_cast --test_tag_filters=
sh_test(
    name = "prp-equiv-gen_type_cast",
    srcs = ["tests/pyrope_test.py"],
    args = ["-i", "$(location //inou/prp:tests/equiv/gen_type_cast.prp)"],
    data = [
        "//inou/prp:tests/prplib.py",
        "//inou/prp:tests/pyrope_test.py",
        "//inou/prp:tests/equiv/gen_type_cast.prp",
        "//inou/prp:tests/equiv/gen_type_cast.v",
        "//lhd",
    ],
    deps = ["//inou/yosys:scripts"],
    tags = ["fixme"],
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
        "async_reset_enable",
        "latch_enable_hold",
        # yosys-reader gap, not a semantic tracker: see the equiv-glob exclude
        # note; the cvc5 prp-v2prp direction is the live guard.
        "struct_init_whole_read",
    ]
]

# 2f-v2prp: the REVERSE direction over the same equiv corpus. The slang reader
# lowers each golden <name>.v to Pyrope (upass/prp_writer); `lhd lec` recompiles
# that Pyrope and proves it equivalent to the ORIGINAL hand-written <name>.prp
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
#   bazel test //inou/prp:prp-v2prp-mem_rom --test_tag_filters=
_V2PRP_FIXME = [
    # the original .prp is a DELIBERATELY-broken generics repro (the `T(a)`
    # constructor does not lower — see the file header); the emitted Pyrope is
    # correct, but the reference itself will not compile, so it cannot be the LEC
    # ref. Tracked by 3g (generics).
    "gen_type_cast",
    # Reverse v2prp proves against the concrete Pyrope source and does not mask
    # the Verilog X payload under valid=false. The forward lgyosys equiv test
    # passes and pins the intended don't-care semantics.
    "sim_dontcare_ctrl",
    "sim_dontcare_state",
    # Known-failing semantic trackers: latch inference and explicit data-path
    # async resets do not have a supported Pyrope source equivalent yet.
    "async_reset_enable",
    "latch_enable_hold",
]

[
    sh_test(
        name = "prp-v2prp-%s" % base,
        srcs = ["tests/v2prp_test.py"],
        args = [
            "-i",
            "$(location //inou/prp:tests/equiv/%s.v)" % base,
        ],
        # The harness LECs the emitted Pyrope against the original .prp, so BOTH
        # the golden .v (the input) and the .prp (the reference) are runfiles.
        data = [
            "//inou/prp:tests/v2prp_test.py",
            "//inou/prp:tests/equiv/%s.v" % base,
            "//inou/prp:tests/equiv/%s.prp" % base,
            "//lhd",
        ],
        tags = ["fixme"] if base in _V2PRP_FIXME else [],
        deps = ["//inou/yosys:scripts"],
    )
    # Every golden ships a matching .prp except mclk_sram_2wclk (its own fixme
    # target below — multi-clock memory has no Pyrope reference yet).
    for base in [
        f.split("/")[-1][:-2]
        for f in glob(
            ["tests/equiv/*.v"],
            # `*_tb.v` are the generated differential-sim golden drivers
            # (prp-simeq-*), not equiv goldens — keep them out of the v2prp glob.
            exclude = [
                "tests/equiv/mclk_sram_2wclk.v",
                "tests/equiv/*_tb.v",
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
        tags = [] if name in ("sim_lg_atomic_feedback", "sim_prp_packed_selfref") else ["fixme"],
    )
    for name, mode in {
        "sim_dontcare_ctrl": "exact-diff",
        "sim_dontcare_state": "exact-diff",
        "sim_prp_packed_selfref": "prp-emit",
        "sim_lg_atomic_feedback": "lg-emit",
    }.items()
]

# 2w-prp_writer bucket C2 (DEFERRED to 2m-multiport_mem): a multi-clock SRAM
# whose two write ports use different clocks. This aborts in the FRONT END
# (upass.attributes: `clock_pin_refname` of the memory is add-only), never
# reaching the writer — it needs a per-port memory-clock representation +
# cgen's existing ware/rtl/cgen_memory_multiclock_*.v wrapper wiring. It is also
# the ONE golden with no <name>.prp reference, so the v2prp harness has nothing
# to LEC against either. Tagged `fixme` (repo known-broken convention; .bazelrc
# filters `-fixme` so `bazel test //...` stays green):
#   bazel test //inou/prp:prp-v2prp-mclk_sram_2wclk --test_tag_filters=
sh_test(
    name = "prp-v2prp-mclk_sram_2wclk",
    srcs = ["tests/v2prp_test.py"],
    args = [
        "-i",
        "$(location //inou/prp:tests/equiv/mclk_sram_2wclk.v)",
    ],
    data = [
        "//inou/prp:tests/v2prp_test.py",
        "//inou/prp:tests/equiv/mclk_sram_2wclk.v",
        "//lhd",
    ],
    tags = ["fixme"],
    deps = ["//inou/yosys:scripts"],
)

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
# recompile and LEC against its golden .v (the FORWARD companion of prp-v2prp).
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
    "arr2d_dyn_sel", "array_selfref", "bit_sel_sext", "bitrange_dyn", "bitrange_open",
    "bitsel_dyn", "bitsel_idx", "bitset_gap", "bitset_imm", "bitset_mut", "bitset_nil",
    "bitset_reg", "clz_nest", "codeblock_expr", "comb_single_out_op", "cond_modified_read",
    "condout_const_arms", "condout_mixed_arms", "condout_seed_uncertain", "dyn_tuple_const",
    "dyn_tuple_idxexpr", "dyn_tuple_mixed", "dyn_tuple_mut_ro", "dyn_tuple_pair",
    "dyn_tuple_sel", "flop_reset_matrix", "generic_mod", "hier_value", "hotmux_match", "hotmux_match_partial",
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
#   * async_reset_enable / gen_type_cast : pre-existing fixme DUTs (async
#       data-path reset; the deliberately-broken generics `T(a)` repro).
# Run one: bazel test //inou/prp:prp-simeq-sim_lg_atomic_feedback --test_tag_filters=
_SIMEQ_FIXME = [
    "async_reset_enable", "gen_type_cast",
]

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
