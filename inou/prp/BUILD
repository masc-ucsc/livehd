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
        # verif_requires + testbench_step + temporal_past now lower/run, as do the
        # 3g subtask-A body-binding trackers (generic_{body_type,const_value,
        # lambda_param} — a generic name is now visible+substituted in the body);
        # the rest are still TBD. (temporal_past: `past[N](x)` lowers to a depth-N
        # `stage` shift register — see prp2lnast function_call_expr_to_node.)
        tags = [] if base in (
            "verif_requires",
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
        "verif_requires",
        "testbench_step",
        "generic_body_type",
        "generic_const_value",
        "generic_lambda_param",
        "generic_decl_default",
        "generic_named_bind",
        "generic_default_arg",
        "input_default",
        # Known BUG tracker (not TBD): const store to a wire-BUNDLE field is
        # folded+dropped (detupled dotted declare loses its wire mode), then
        # tolg errors "wire 'io.a' is never driven" on valid code. See the
        # fixture header for the root-cause chain and fix direction.
        "wire_field_const_store",
        # todo/livehd/2f-latch BMC twins (:type: verify) of the sim trackers
        # above — the SAME self-checking properties as design-body asserts for
        # `lhd formal verify` (stated twice because test-block asserts are
        # invisible to verify and body asserts are not executed by sim; see
        # each header for the dual-fixture conventions). Promote at M4:
        #   latch_verify_hold / latch_verify_low -> today the shared encoder
        #     refuses the Latch cell (UNKNOWN/exit 0 vacuous pass; the harness
        #     fails on the missing verdicts)
        #   flop_verify_posneg -> today posclk is abstracted out of the
        #     encoding, so the half-cycle-transfer assert REFUTES at cycle 3
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

# `:type: verify` fixtures (tests/verify/): the harness drives `lhd formal
# verify` on a single self-contained .prp (design + `formal` blocks) with a
# --workdir, then checks the header expectations against the machine artifacts:
# formal_report.json verdict counts, the formalfail.prp counterexample
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
    for base in [
        f.split("/")[-1][:-2]
        for f in glob(
            ["tests/equiv/*.v"],
            # `*_tb.v` are the generated differential-sim golden drivers
            # (prp-simeq-*), not equiv goldens — keep them out of the v2prp glob.
            exclude = [
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
        # the bundle leaves); green together with its prp-v2prp companion.
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
