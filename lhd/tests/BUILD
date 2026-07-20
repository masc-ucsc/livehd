# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# lhd kernel tests (task 1y-bazel):
#  - trivial_net: the //tools:lhd.bzl ruleset driving `lhd compile verilog`
#    as a hermetic genrule action.
#  - lhd_flow-<in>-to-<out>: the full (input x output) kind matrix on the
#    inou/prp/tests/equiv trivial_if pyrope/verilog golden pair. Supported
#    cells run the flow and validate the artifact; the rest lock the
#    documented `unsupported` v0 contract. verilog/pyrope inputs emit
#    lnast/pyrope as --emit-dir (variable cardinality); lgraph is always the
#    lgdb directory blob.
#  - lhd_equiv_test: prp->ln->lg->v1, prp->lg->v2, verilog0->lg->v3, then
#    LEC of each against the golden verilog0.

load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools:lhd.bzl", "lhd_verilog")

lhd_verilog(
    name = "trivial_net",
    srcs = ["trivial.v"],
    out = "trivial.gen.v",
    recipe = "O1",
    top = "trivial",
)

_FLOW_DATA = [
    "//lhd",
    "//inou/prp:equiv_tests",
    "//inou/yosys:scripts",
]

# (input kind, output kind, expectation). ln: = Forest save dir (LNAST
# units), lg: = GraphLibrary save dir (LGraphs).
_FLOWS = [
    ("pyrope", "verilog", "pass"),
    ("pyrope", "pyrope", "pass"),
    ("pyrope", "ln", "pass"),
    ("pyrope", "lg", "pass"),
    ("verilog", "verilog", "pass"),
    ("verilog", "pyrope", "unsupported"),  # no LGraph -> LNAST decompiler
    ("verilog", "ln", "unsupported"),  # verilog elaborates to LGraphs
    ("verilog", "lg", "pass"),
    ("ln", "verilog", "pass"),
    ("ln", "pyrope", "pass"),
    ("ln", "ln", "pass"),
    ("ln", "lg", "pass"),
    ("lg", "verilog", "pass"),
    ("lg", "pyrope", "unsupported"),  # no LGraph -> LNAST decompiler
    ("lg", "ln", "unsupported"),  # no LGraph -> LNAST decompiler
    ("lg", "lg", "pass"),
]

[
    sh_test(
        name = "lhd_flow-{}-to-{}".format(in_kind, out_kind),
        srcs = ["lhd_flow_test.sh"],
        args = [
            in_kind,
            out_kind,
            expect,
        ],
        data = _FLOW_DATA,
    )
    for (in_kind, out_kind, expect) in _FLOWS
]

sh_test(
    name = "lhd_equiv_test",
    size = "large",
    srcs = ["lhd_equiv_test.sh"],
    data = _FLOW_DATA,
)

# `lhd lec` as the single equivalence command: verilog inputs (slang default +
# --reader yosys-* override), the cvc5 (default) and lgyosys backends via
# --set formal.solver, and the retired `check` command's migration error.
sh_test(
    name = "lhd_lec_verilog_test",
    size = "large",
    srcs = ["lhd_lec_verilog_test.sh"],
    data = _FLOW_DATA + glob(["merge_demo/*"]),
)

sh_test(
    name = "lhd_scan_test",
    srcs = ["lhd_scan_test.sh"],
    data = _FLOW_DATA + ["scan_imports.prp"],
)

# `lhd sim` runtime test-parameter flags: each `test name(params)` parameter is a
# `--<name>` flag on the generated driver (+ `--seed`/`--help`/validating
# parsers); unsafe/reserved parameter names are rejected with a clear message,
# and `--seed` must be numeric. Drives only `lhd sim --setup-only` (no nested
# bazel / host compiler), asserting on the generated driver source + diagnostics.
sh_test(
    name = "lhd_sim_args_test",
    srcs = ["lhd_sim_args_test.sh"],
    data = ["//lhd"],
)

# Stage 0 of the cgen_sim false-combinational-loop fix: a combinational
# sub-instance whose output feeds back through parent comb logic into one of its
# own inputs is a FALSE comb loop the single-pass sim schedule cannot order.
# inou.cgen.sim used to silently emit create_integer(0) for the fed-back input
# (wrong sim, exit 0); Stage 0 makes it a loud, located build failure. Asserts the
# bug designs fail with the diagnostic AND legitimate hierarchy still compiles
# clean (no false positive). See inou/cgen/tests/comb_loop/README.md.
sh_test(
    name = "cgen_sim_comb_loop_test",
    srcs = ["cgen_sim_comb_loop_test.sh"],
    data = ["//lhd"],
)

# `lhd sim` VCD clock/reset waveforms + the `tick clocks=(name=ratio)
# resets=(name=ticks)` clause: the generated DUT body traces a toggling clock + a
# reset window, drives the reset port functionally, and advances the period
# counter independent of VCD; the driver configures the ratio/window; >1 clock or
# reset is rejected. Drives only `lhd sim --setup-only` (no nested bazel / host
# compiler), asserting on the generated sim body + driver source + diagnostics.
sh_test(
    name = "lhd_sim_clock_reset_test",
    srcs = ["lhd_sim_clock_reset_test.sh"],
    data = ["//lhd"],
)

# `lhd sim` single fast run path + `--list-tests`: `--list-tests` emits the dotted
# test names + parameters as JSON (a pure parse, no build); `--setup-only`
# generates ONE driver (drv.cpp) holding every `test` block with the registry +
# embedded JSON + `--test`/`--seed` flags; `--set sim.vcd=true` resets the VCD
# timeline per test. The structural checks run hermetically; when the sibling
# ../hlop + ../iassert headers are present it also host-compiles + runs the binary
# (binary `--list-tests` == `lhd sim --list-tests`, `--test` selection, two-test
# VCD run produces both .vcd without crashing).
sh_test(
    name = "lhd_sim_list_tests_test",
    srcs = ["lhd_sim_list_tests_test.sh"],
    data = ["//lhd"],
)

# `lhd sim` structured + LOCATED assert failures: a failed `assert` prints the
# source location (prp:line), the cycle (the `clock` value, which survives the
# tick loop), and both operand VALUES of a top-level comparison (a literal operand
# prints only its source); `lhd sim --result-json` enriches the result envelope
# with a per-test {test,status,cycle,failing_assert,prp_file,line,msg} array. The
# structural checks run hermetically; when the sibling ../hlop + ../iassert headers
# are present it also host-compiles + runs and checks the located stdout + array.
sh_test(
    name = "lhd_sim_located_assert_test",
    srcs = ["lhd_sim_located_assert_test.sh"],
    data = ["//lhd"],
)

# `lhd sim` CHECKPOINT creation (sim_checkpoint_debug_plan, Stage B): cgen emits
# dump_state/load_state/design_hash per module; the driver runs a periodic
# fork-checkpoint cadence in the tick loop writing <workdir>/ckpt/<test>/ckp<N>/
# {regs.json,*.hex,tb.json,meta.json}, pruned to `sim.checkpoint_max` and evenly
# spaced; `sim.checkpoint=false` disables it. Structural checks run hermetically;
# the run checks need the sibling ../hlop + ../iassert headers.
sh_test(
    name = "lhd_sim_checkpoint_test",
    srcs = ["lhd_sim_checkpoint_test.sh"],
    data = ["//lhd"],
)

# `lhd sim` RESTART + windowed VCD (sim_checkpoint_debug_plan, Stage C):
# `--restart-at N` loads the nearest checkpoint <= N (DUT + testbench frame) and
# resumes there, so an accumulating tb local matches a full run bit-exactly and
# only cycles >= the checkpoint re-run; a target before any checkpoint replays
# from 0; `--vcd-from Y --vcd-to Z` traces a VCD over just [Y, Z]. Structural
# checks run hermetically; the run checks need the sibling ../hlop/../iassert.
sh_test(
    name = "lhd_sim_restart_test",
    srcs = ["lhd_sim_restart_test.sh"],
    data = ["//lhd"],
)

# `lhd sim --vcd-on-fail` (sim_checkpoint_debug_plan, Stage D): on an assert fire,
# the driver re-runs the failed test from the nearest checkpoint with a VCD window
# around the failing cycle (reusing Stage C), producing `<test>.vcd` of the failure
# region with its stdout suppressed (the verdict is unchanged). Structural checks
# run hermetically; the run checks need the sibling ../hlop/../iassert headers.
sh_test(
    name = "lhd_sim_vcd_on_fail_test",
    srcs = ["lhd_sim_vcd_on_fail_test.sh"],
    data = ["//lhd"],
)

# Hierarchical VCD (one writer shared down the sub-instance tree, nested $scope
# per instance, sub io+flops traced) + the `sim.vcdfakedelay` style knob (default:
# X settle window after each edge; false: plain edge-aligned updates) + the real
# `clock` label through a flopless pass-through wrapper (the lecfail dut-pair
# shape). Structural checks run hermetically; the run checks need the sibling
# ../hlop/../iassert headers.
sh_test(
    name = "lhd_sim_vcd_hier_test",
    srcs = ["lhd_sim_vcd_hier_test.sh"],
    data = ["//lhd"],
)

# `lhd sim` OBSERVABILITY (sim_checkpoint_debug_plan): query signal values without
# re-instrumenting. cgen emits describe_signals/probe_signals per module; the
# result envelope's "debug" member carries --list-signals (the observable signal
# set), --probe (a per-cycle JSON trajectory), and --break-when (the first cycle a
# SIG OP V condition holds). Structural checks run hermetically; the run checks
# need the sibling ../hlop/../iassert headers.
sh_test(
    name = "lhd_sim_observe_test",
    srcs = ["lhd_sim_observe_test.sh"],
    data = ["//lhd"],
)

# 2c-color / 2p-partition: `lhd pass color <alg>` colors a flat top, `lhd pass
# partition` rebuilds it as one-module-per-region + a new top, and the
# partitioned Verilog is LEC-equivalent to the original (acyclic + synth).
sh_test(
    name = "lhd_partition_test",
    size = "large",
    srcs = ["lhd_partition_test.sh"],
    data = _FLOW_DATA + ["part_flat.v"],
)

# 2f-semdiff: `lhd pass semdiff` structurally matches two lg: libraries (marks the
# `match` attribute), the diff is greppable (tool grep match=0 / -v) and
# visualizable (tool diff --match); covers commutative full-match, an isolated
# difference, the matching_names flop anchor, and id_granularity=region.
sh_test(
    name = "lhd_semdiff_test",
    size = "large",
    srcs = ["lhd_semdiff_test.sh"],
    data = ["//lhd"],
)

# 2f-formal: a refuted assert/assume/Hotmux one-hotness is recorded as a build
# error (exit != 0) but the compile CONTINUES so cgen still emits the design
# with the failing check kept as a runtime check (never elided / optimized).
sh_test(
    name = "lhd_formal_fail_test",
    size = "large",
    srcs = ["lhd_formal_fail_test.sh"],
    data = ["//lhd"],
)

# 2f-verify V1: `lhd formal verify` — single-design assert/assume BMC on the
# pass/lec engine: per-assert bounded-proven / refuted-at-cycle-with-trace
# verdict table, assume-as-disclosed-constraint, per-obligation timeout
# isolation (+ formal.strict), formal.*/lec.* knob aliases, `formal lec` alias.
sh_test(
    name = "lhd_formal_verify_test",
    size = "large",
    srcs = ["lhd_formal_verify_test.sh"],
    data = ["//lhd"],
)

# F3 (2f-fcore): `lhd formal verify` under the engine=auto portfolio — the
# induction-first strategy promotes a deep-state invariant to UNBOUNDED, the
# per-obligation merge takes bmc-first's deep reachable refute, and auto never
# regresses a bmc verdict.
sh_test(
    name = "lhd_formal_portfolio_test",
    size = "large",
    srcs = ["lhd_formal_portfolio_test.sh"],
    data = ["//lhd"],
)

# 2f-formal: pass.formal mode=normal rebased onto the shared BMC-from-reset +
# 1-induction engine — the stateful verdict ladder (inductive->elide, reachable
# refute->hard error, out-of-budget->deferred) and the deterministic rlimit budget.
sh_test(
    name = "lhd_formal_rebase_test",
    size = "large",
    srcs = ["lhd_formal_rebase_test.sh"],
    data = ["//lhd"],
)

# post-V1 verify knobs: the TOTAL solver-time budget (budget_mode=wall), the
# minetimeout timeout-core diagnosis (names the toxic obligation subset), and the
# per-flop induction reset guard (unbounded proof for a true twin invariant, still
# REFUTED for an unequal-reset twin — the guard never manufactures a proof).
sh_test(
    name = "lhd_formal_budget_mine_test",
    size = "large",
    srcs = ["lhd_formal_budget_mine_test.sh"],
    data = ["//lhd"],
)

# P3 invariant mining: a stuck run mines the tight inductive range bound of the
# counter in the hard obligation's cone, emits it as a paste-ready formal block
# (formal_mined.prp) + report mined[] records, the round-trip re-proves it
# (P1 prove-then-use), and a design edit that falsifies it REFUTES loudly.
sh_test(
    name = "lhd_formal_mine_test",
    size = "large",
    srcs = ["lhd_formal_mine_test.sh"],
    data = ["//lhd"],
)

sh_test(
    name = "lhd_formal_cache_split_test",
    srcs = ["lhd_formal_cache_split_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],
)

sh_test(
    name = "lec_formal_helpers_test",
    size = "medium",
    srcs = ["lec_formal_helpers_test.sh"],
    data = ["//lhd:lhd"],
)

# 2f-lec tier-2 uncertain state correspondence: semdiff full-match pairs
# injected as uncertain (self-certifying ind PASS, drop-all+retry-once on
# REFUTE, bounded bmc PASS suppressed), entity-keyed pair hints replayed warm.
sh_test(
    name = "lec_state_pairing_test",
    size = "medium",
    srcs = ["lec_state_pairing_test.sh"],
    data = ["//lhd:lhd"],
)

# run_id slice scoping: a lec lg: side hashes only its --top slice, so edits
# to modules outside the proven subtree keep the run_id; edits to a Sub child
# move it; an unresolvable top falls back to whole-directory hashing.
sh_test(
    name = "lhd_runid_slice_test",
    srcs = ["lhd_runid_slice_test.sh"],
    data = ["//lhd"],
)

# unset-unused-field on detupled wire-bundle leaves: a driven+consumed u64
# field must not false-positive (its reads/writes ride plain dotted refs and
# its width defeats the 62-bit bw-range proxy), and a never-touched leaf must
# still be reported (it materializes no bundle entry — the declare-side
# enumeration catches it).
sh_test(
    name = "unset_unused_field_warning_test",
    srcs = ["unset_unused_field_warning_test.sh"],
    data = ["//lhd"],
)

# Regression: a kind-change ("a variable's type cannot change") diagnostic must
# report its source span (file + line), not "span":null. Guards both the
# constprop array/field store path (cursor left invalid by the sibling walk) and
# the typecheck scalar store path (emit helper's empty default span).
sh_test(
    name = "lhd_diag_span_test",
    size = "large",
    srcs = ["lhd_diag_span_test.sh"],
    data = ["//lhd"],
)

# Regression for the cvc5 LEC/formal encoder's `#sext` sign propagation: a
# partial sign-extend slice must sign-extend (not zero-extend) when widened,
# arithmetic-shifted, or signed-compared. cvc5-only (in-process), so just //lhd.
sh_test(
    name = "lec_sext_sign_test",
    size = "large",
    srcs = ["lec_sext_sign_test.sh"],
    data = ["//lhd"],
)

# Regression for the cvc5 LEC "phantom headroom bit" false REFUTE: a flop
# declared wider on one side (cgen's signed spare-bit `reg [1:0]` for a u1
# value) must share its power-on state at the MIN corresponding width, extended
# on the wide side — a free headroom bit spuriously refutes an equivalent pair
# (the DelayNWithValid shape). Also guards soundness: a genuinely-2-bit counter
# vs a u1 twin must still REFUTE. Uses the equiv pair as data; cvc5-only.
sh_test(
    name = "lec_flop_init_headroom_test",
    size = "large",
    srcs = ["lec_flop_init_headroom_test.sh"],
    data = [
        "//inou/prp:equiv_tests",
        "//lhd",
    ],
)

# Type-C whole-net false comb loop on the SIM-emission path: a struct net
# whole-'{...}'-assigned with later elements reading earlier FIELDS of the same
# net (acyclic per field, cyclic per whole net) must sim-emit — the slang
# reader keeps it a per-field bundle (whole_copied_selfref_pattern). Guards the
# dominant XiangShan `lhd sim` error family (DretEventModule et al.).
sh_test(
    name = "sim_selfref_struct_test",
    size = "large",
    srcs = ["sim_selfref_struct_test.sh"],
    data = [
        "//inou/prp:equiv_tests",
        "//lhd",
    ],
)

# lec.gold_x=ignore (reference-side X = don't-care, the yosys -ignore_gold_x
# analogue): an impl that concretizes a ref X arm to ANY value must prove; a
# real bug on non-X bits must still refute; gold_x=zero keeps legacy behavior.
sh_test(
    name = "lec_gold_x_test",
    size = "large",
    srcs = ["lec_gold_x_test.sh"],
    data = ["//lhd"],
)

# Regression for the slang reader lowering a scalar packed-struct variable as a
# per-field BUNDLE (independent nets) instead of one flat packed bus: a struct
# whose '{...} write computes a field from reads of its OTHER fields (the
# dual-issue ALU `io`) becomes a false combinational self-loop as one node, which
# the cvc5 LEC/formal encoder cannot encode. Asserts the bundle representation
# AND cvc5-proves equivalence to the struct-free twin (just //lhd).
sh_test(
    name = "lec_packed_struct_test",
    size = "large",
    srcs = ["lec_packed_struct_test.sh"],
    data = ["//lhd"],
)

# Regressions for the slang reader's `wire` classification (back-edge-only, not
# blanket-wiring a false SCC) and the multi-write `wire` split (mut accumulator +
# single bridge). Emitted Pyrope must recompile AND cvc5-prove equivalent to the
# source (just //lhd).
sh_test(
    name = "slang_wire_classification_test",
    size = "large",
    srcs = ["slang_wire_classification_test.sh"],
    data = ["//lhd"],
)

# Regression for SCALAR named-type aliases (`type W = u10; wire x:W`, local or
# imported `pkg.W`) carrying their width to the declaration, plus the `pub type`
# grammar needed to export a scalar alias from a package unit (just //lhd).
sh_test(
    name = "scalar_named_type_test",
    size = "medium",
    srcs = ["scalar_named_type_test.sh"],
    data = ["//lhd"],
)

# Regression for named-constant provenance in slang→Pyrope emission
# (compile.slang.preserve_param_provenance): symbolic `pkg.PARAM` refs + a
# `pub comptime const` package unit with REAL values under constprop=0 and =1,
# a value-preserving-only conversion peel, recompile of the emitted Pyrope, and
# cvc5 LEC vs the source SV (just //lhd).
sh_test(
    name = "slang_param_provenance_test",
    size = "large",
    srcs = ["slang_param_provenance_test.sh"],
    data = ["//lhd"],
)

# Regressions for the slang-reader/tolg features the minion IP needed: multi-dim
# unpacked struct arrays (linearized tuple memory, LEC'd), latch-based ICG state
# + generate-for reg arrays on a derived clock, HierarchicalValue assignment
# targets, widening-cast declared-range aliasing, and structs first written
# inside a unique-case arm (just //lhd).
sh_test(
    name = "slang_minion_features_test",
    size = "large",
    srcs = ["slang_minion_features_test.sh"],
    data = ["//lhd"],
)

# FIXME tracker for todo/livehd/2f-latch (M4) — a LIVE SOUNDNESS BUG that is NOT
# about latches: pass/lec/encode.cpp never reads a Flop's clock_pin/posclk, so
# `lhd lec` FALSELY PROVES gated-vs-ungated clocks, negedge-vs-posedge flops, and
# two-clock-vs-one-clock pipelines equivalent. Reproduced 2026-07-20; all three
# must REFUTE once the encoder is clock-aware. Tagged `fixme` so `bazel test
# //...` stays green; run it with --test_tag_filters=
sh_test(
    name = "lec_clock_blindness_test",
    size = "large",
    srcs = ["lec_clock_blindness_test.sh"],
    data = ["//lhd"],
    tags = ["fixme"],
)

# LIVE oracle-pinning test for todo/livehd/2f-latch (M0 "pin the lgyosys
# latch-LEC verdict" — resolved 2026-07-20): lgcheck's bounded-miter stage
# DECIDES latch designs (equiv_simple alone cannot discharge $dlatch) and
# genuinely DISCRIMINATES latch enable polarity (a port-matched
# transparent-high vs transparent-low flip REFUTES with a counterexample).
# This is the independent latch oracle the 2f-latch cross-model gating rule
# uses. Note the power is latch-polarity only: for FLOP clock/edge differences
# lgcheck is as blind as the native encoder (lec_clock_blindness_test above).
sh_test(
    name = "lec_latch_polarity_test",
    size = "large",
    srcs = ["lec_latch_polarity_test.sh"],
    data = [
        "//inou/yosys:scripts",
        "//lhd",
    ],
)

# LIVE test for todo/livehd/2f-latch M2 (landed 2026-07-20) — the latch's cell
# identity and the consumers that had to learn about it. Its headline is a
# NEGATIVE case: implementing Pyrope-authored active-low enable as a bare
# polarity-pin flip is a SILENT MISCOMPILE (tolg bakes `din = cond ? d : q` from
# the same condition, so a flipped enable makes the latch write ITSELF and never
# capture din), and a symmetric before/after gate cannot see it — it was caught
# only by LEC-ing against an independent golden. The attribute is therefore
# REFUSED, and the test pins both the refusal and the correctness of the
# spelling it recommends (`if !g`), including a vacuity guard that the oracle
# really does REFUTE the opposite polarity. Also covers pass/abc treating a
# Latch as a boundary (it used to reject the whole region) and a
# color+partition+compile flow emitting elaborable Verilog.
sh_test(
    name = "latch_clock_identity_test",
    size = "large",
    srcs = ["latch_clock_identity_test.sh"],
    data = [
        "//inou/prp:abc_tests",  # the toy liberty library
        "//inou/yosys:scripts",  # lgcheck: the polarity-discriminating oracle
        "//lhd",
    ],
)

# LIVE test for todo/livehd/2f-latch M1 (landed 2026-07-20) — latch Verilog
# emission + round-trip. Both bugs M1 fixed were invisible to a yosys-only gate,
# which is why the six live prp-equiv-latch_* pairs kept PROVING while the
# emitter was broken: (1) `process_latch` read din/enable through
# get_wire_or_const, so a fanout-1 enable came out as a bare UNDECLARED
# identifier — reproducible ONLY through the yosys importer, since tolg's hold
# mux keeps a Pyrope latch's enable at fanout >= 2; (2) the emission was
# `always @*` + blocking `=`, which our own slang reader reads back as plain
# comb, so Verilog -> slang silently LOST the Latch cell. Both halves were
# verified to fail on the pre-M1 emitter before landing.
sh_test(
    name = "latch_verilog_emission_test",
    size = "large",
    srcs = ["latch_verilog_emission_test.sh"],
    data = [
        "//inou/yosys:scripts",  # lgcheck, for the round-trip LEC leg
        "//lhd",
    ],
)

# LIVE fail-closed test for todo/livehd/2f-latch M0 (landed 2026-07-20). Every
# shape it covers used to report SUCCESS while silently doing the wrong thing,
# which makes every downstream gate vacuous: a latch design under `lhd formal
# verify` exited 0 on an ENCODER REFUSAL (distinct from a solver give-up — no
# budget can help, so `formal.strict` must not be needed to see it); `lhd sim`
# on a latch died with a misleading `combinational-loop` error naming an
# internal node; and a gated-clock or two-clock design simulated to a
# plausible-looking WRONG waveform at exit 0. Asserts the diagnostic CODE, not
# just the nonzero exit (a crash also exits nonzero), and pins the negative side
# too: an ordinary single-clock design must still simulate.
sh_test(
    name = "latch_fail_closed_test",
    size = "large",
    srcs = ["latch_fail_closed_test.sh"],
    data = ["//lhd"],
)

# LIVE test for todo/livehd/2f-latch M3 (landed 2026-07-20) — the latch CONTRACT
# CHECK. The flop-with-enable/commit-class latch model is an abstraction with a
# PRECONDITION (no time borrowing), so the precondition is checked, never
# assumed: graph/latch_contract.cpp fails closed with a distinct diagnostic on
# each of the three violation shapes (self-timed gate, Q->own-D comb path, comb
# path between simultaneously-transparent latches).
#
# The ACCEPTANCE half is what makes it a real test — a checker that rejects
# everything passes all three rejections. It pins that a master/slave pair
# (opposite phases of one gate, with a comb path between them) is ACCEPTED, that
# every shipped latch fixture still compiles, and — the sharpest discriminator —
# that a same-phase twin of that master/slave design differing by exactly ONE
# `!` flips the verdict. Both-accept or both-reject means rule C has gone blind.
sh_test(
    name = "latch_contract_check_test",
    size = "large",
    srcs = ["latch_contract_check_test.sh"],
    data = [
        "//inou/prp:equiv_tests",  # the shipped latch fixtures must keep compiling
        "//lhd",
    ],
)

# Regression for the lec leaf<->flat-bus port correspondence: a design whose
# tuple-typed ports compile to per-leaf dotted io (`req.a`, `req.b`) must
# PROVE against the same design compiled with ONE flat packed bus port
# (first-declared leaf = MSB, SV packed layout), REFUTE on real diffs, and
# stay Unknown (not crash/prove) on width-sum mismatches (just //lhd).
sh_test(
    name = "lec_tuple_port_flat_test",
    size = "large",
    srcs = ["lec_tuple_port_flat_test.sh"],
    data = ["//lhd"],
)

# Regression for tuple-typed-port CALL SITES: named tuple actuals on mod and
# comb Sub calls (runner re-emits the leaf expansion as dotted named actuals),
# dot-form multi-output instance reads (`r.rsp.sum`), and escaped dotted
# instance-connection names in emitted Verilog (just //lhd).
sh_test(
    name = "tuple_port_callsite_test",
    size = "large",
    srcs = ["tuple_port_callsite_test.sh"],
    data = ["//lhd"],
)

# Regression for the "conditional reg write with no covering else does NOT
# auto-hold" trap: an unwritten conditional path must HOLD the flop q, not lower
# to a don't-care. Covers the empty-`elif` (PC stall), pure-write (register
# file), and `unique if`/match forms. Asserts no don't-care in the next-state
# AND cvc5-proves equivalence to an explicit-hold Verilog reference (just //lhd).
sh_test(
    name = "reg_cond_hold_test",
    size = "large",
    srcs = ["reg_cond_hold_test.sh"],
    data = ["//lhd"],
)

# Hierarchical-LEC box-instance correspondence + verdict safety: stateless
# collapsed leaves prove reversed interchangeable instances PAIRING-FREE
# (per-def UF congruence), stateful ones pair name-first (sorted cross-design
# UF input layout), an occurrence-mispaired anonymous pair is rescued by the
# driver's flat confirmation, and a REAL bug in the same shapes still exits
# REFUTED (the fallback never launders a genuine fail).
sh_test(
    name = "lec_box_pairing_test",
    size = "large",
    srcs = ["lec_box_pairing_test.sh"],
    data = ["//lhd"],
)

# Call-site `name=` (`f::[name=inst](args)`) sets the Sub instance name and
# `reg x:[name="..."]` sets the local flop name, so a `mod` instantiated twice
# yields the hierarchical flop names (pipeA_ex_mem.reg_mem_writedata, …) LEC
# uses to correspond state with a hierarchical Verilog reference (cvc5-proven).
sh_test(
    name = "callsite_name_test",
    size = "large",
    srcs = ["callsite_name_test.sh"],
    data = ["//lhd"],
)

# compile.upass.inline=false leaves a fully-defined `comb` as a Sub module
# INSTANCE (tolg lowers it) instead of inlining it into the caller — preserving
# the comb boundary for debug/optimization. Asserts the flatten-vs-instance
# structural split, that the two builds are LEC-equivalent, and that
# overload/closure dispatch (no Sub form) and the comb-can't-instantiate-mod
# rule are unaffected.
sh_test(
    name = "inline_disable_test",
    size = "large",
    srcs = ["inline_disable_test.sh"],
    data = ["//lhd"],
)

# 2c-color / 2p hierarchical-input (prerequisite for 2a-abc): color + partition
# descend a multi-module Pyrope hierarchy (combinational + sequential fixtures),
# re-link Sub instances, and the partitioned design stays LEC-equivalent.
sh_test(
    name = "lhd_partition_hier_test",
    size = "large",
    srcs = ["lhd_partition_hier_test.sh"],
    data = _FLOW_DATA + ["//inou/prp:hier_tests"],
)

# 2c-color-size subtask D: `pass.color synth` ABSORBS every def below the size
# window's `min` (structural inline, so its logic can share an ABC region with
# its neighbours -- a Sub is a blackbox to ABC). Absorb rewrites the netlist, so
# the test that matters is the one only an end-to-end run gives: the absorbed
# design is still LEC-equivalent to the original.
sh_test(
    name = "lhd_color_absorb_test",
    size = "large",
    srcs = ["lhd_color_absorb_test.sh"],
    data = _FLOW_DATA + ["//inou/prp:hier_tests"],
)

# `pass.color reduce`: repeated combinational cones become ONE shared def
# instantiated at every site (in-place rewrite, fewer total nodes). The test
# that matters is end-to-end: a pat_* module appears, instantiated at every
# occurrence, and the reduced library is LEC-equivalent to the original; plus
# idempotence, the min_count gate, and the --emit-dir refusal.
sh_test(
    name = "lhd_color_reduce_test",
    size = "large",
    srcs = ["lhd_color_reduce_test.sh"],
    data = _FLOW_DATA,
)

# 2a-abc: `lhd pass abc` technology-maps a colored combinational design to a
# standard-cell netlist (blackbox Sub cells), `pass liberty gensim` models each
# cell, and the netlist is LEC-equivalent to the original logic (+ a negative
# control). Hermetic via a small vendored Liberty.
sh_test(
    name = "lhd_abc_test",
    size = "large",
    srcs = ["lhd_abc_test.sh"],
    data = _FLOW_DATA + ["//inou/prp:abc_tests"],
)

# 2opt-incr A+C: incremental pass.abc. A persistent region cache keyed by a
# canonical digest: a NoChange run is all hits and zero ABC; editing one module
# re-synthesizes only the regions it touched; every assembled netlist is
# LEC-gated against the original logic.
sh_test(
    name = "lhd_abc_incr_test",
    size = "large",
    srcs = ["lhd_abc_incr_test.sh"],
    data = _FLOW_DATA + [
        "//inou/prp:abc_tests",
        "//inou/prp:hier_tests",
    ],
)

# 2opt-incr subtask 0: pass.abc memory admission. A whole-design ABC region can
# exhaust the machine (flat XSCore reached 221 GB before the OS killed it), so
# pass.abc samples its own RSS while bit-blasting and refuses a region that will
# not fit. Pinned with memory_budget_mb so the refusal is deterministic on any
# host: refuse + emit no partial netlist + allow_oversize override + no false
# positive on a design that fits.
sh_test(
    name = "lhd_abc_memory_test",
    size = "large",
    srcs = ["lhd_abc_memory_test.sh"],
    data = _FLOW_DATA + ["//inou/prp:abc_tests"],
)

# 2opt-incr: design-size gate (node-count memory admission). pass.color warns and
# pass.abc/lec refuse (with an override) when a flattened design is over the
# threshold, BEFORE any translation. Pinned tiny with LIVEHD_LARGE_DESIGN_NODES so
# the refusal is deterministic and host-independent: color warns, abc/lec refuse +
# override, per-def and the default threshold do not fire.
sh_test(
    name = "lhd_size_gate_test",
    size = "large",
    srcs = ["lhd_size_gate_test.sh"],
    data = _FLOW_DATA + ["//inou/prp:abc_tests"],
)

# 2opt-freq D: `lhd pass opentimer` STA on pass.abc-mapped modules — comb
# region + flat sequential (flop-boundary), timing.json + envelope qor member,
# and the non-Liberty-Sub / bogus-top negative controls.
sh_test(
    name = "lhd_opentimer_test",
    size = "large",
    srcs = ["lhd_opentimer_test.sh"],
    data = _FLOW_DATA + ["//inou/prp:abc_tests"],
)

# 2opt-freq B: `{ ::[abc='…', color=N] … }` block-scoped synthesis attributes —
# LEC-invariant annotation, own region + per-region flow override (with and
# without a pass.color run: seeded precedence), netlist LEC vs the partition
# twin, and the unknown-attr / interpolation-trap negative controls.
sh_test(
    name = "lhd_block_attr_test",
    size = "large",
    srcs = ["lhd_block_attr_test.sh"],
    data = _FLOW_DATA + ["//inou/prp:abc_tests"],
)

# 2i-abc_arith: every selectable adder architecture (rca/cska/cla + block_size)
# tech-maps the arithmetic fixture and is LEC-equivalent to the original logic.
sh_test(
    name = "lhd_abc_arith_test",
    size = "large",
    srcs = ["lhd_abc_arith_test.sh"],
    data = _FLOW_DATA + ["//inou/prp:abc_tests"],
)

# 2a-abc follow-on: the multiplier (array) + right-shift (sra) bit-blast and the
# division blackbox. Maps mult/sra and keeps div native (with a div-blackbox
# warning), proving every region LEC-equivalent to the original logic over signed
# and unsigned operands, plus a non-default adder and a no-lib negative control.
sh_test(
    name = "lhd_abc_math_test",
    size = "large",
    srcs = ["lhd_abc_math_test.sh"],
    data = _FLOW_DATA + ["//inou/prp:abc_tests"],
)

# 2a-abc subtask 5: sequential tech-mapping (seq=true). Flops cross into ABC as
# latches and return as native flops (name + reset preserved); memories and Sub
# instances are blackbox boundaries. Each region is sequentially LEC-equivalent
# (yosys BMC/induction) to its original-logic twin, plus a negative control.
sh_test(
    name = "lhd_abc_seq_test",
    size = "large",
    srcs = ["lhd_abc_seq_test.sh"],
    data = _FLOW_DATA + ["//inou/prp:abc_tests"],
)

# Whole-design flatten: `pass.color flat` + pass.abc/pass.partition inline the
# hierarchy into ONE output module (flatten=auto), LEC-proven vs the original;
# flatten=false keeps the classic per-def decomposition.
sh_test(
    name = "lhd_abc_flat_test",
    size = "large",
    srcs = ["lhd_abc_flat_test.sh"],
    data = _FLOW_DATA + ["//inou/prp:abc_tests"],
)

# 2c-color: every coloring algorithm (acyclic|cgen|synth|path|mincut|flat) +
# continuous + clear run through the lhd CLI on a real design.
sh_test(
    name = "lhd_color_test",
    size = "large",
    srcs = ["lhd_color_test.sh"],
    data = _FLOW_DATA + ["part_hier.v"],
)

# The --top XXX -> XXX.XXX (file.entity) fallback: commands resolving a top
# against internal graph/unit names accept the bare entity with a
# top-entity-fallback warning; an unresolvable --top still errors (tool diff
# must not read as "identical").
sh_test(
    name = "lhd_top_fallback_test",
    srcs = ["lhd_top_fallback_test.sh"],
    data = _FLOW_DATA + glob(["merge_demo/*"]),
)

# The `lhd tool` lg-side inspector (cat/grep/diff/tree, the filter grammar,
# jsonl, --max) on a real colored design.
sh_test(
    name = "lhd_tool_test",
    size = "large",
    srcs = ["lhd_tool_test.sh"],
    data = _FLOW_DATA + [
        "part_hier.v",
        "tree_hier.prp",
    ],
)

# 2h-set_path: --set namespace = command path + context-relative key
# abbreviation (all five forms of pass.color.continuous behave identically).
sh_test(
    name = "lhd_set_path_test",
    size = "large",
    srcs = ["lhd_set_path_test.sh"],
    data = _FLOW_DATA + ["part_hier.v"],
)

# Task 1m — pub/import machinery (the LiveHD docs): pub index
# + __pub wrapper publication, two-invocation and iterate-until-converged
# same-invocation imports, and the failure-mode diagnostics.
sh_test(
    name = "lhd_import_test",
    srcs = ["lhd_import_test.sh"],
    data = _FLOW_DATA,
)

# Task 1m-C — graph-library linker: merge lg: + ln: inputs into a new lg:
# library (one defines `foo`, another imports it as a black box).
sh_test(
    name = "lhd_lg_merge_test",
    srcs = ["lhd_lg_merge_test.sh"],
    data = _FLOW_DATA,
)

# Doc-test for the "Linking libraries (Pyrope + a Verilog black box)" section of
# the usage guide (../docs/docs/livehd/02-usage.md): yosys leaf + Pyrope leaf +
# Pyrope top → linked lg → Verilog.
sh_test(
    name = "lhd_usage_merge_test",
    srcs = ["lhd_usage_merge_test.sh"],
    data = _FLOW_DATA + glob(["merge_demo/*"]),
)

# --reader yosys-verilog|yosys-slang|slang trichotomy (slang = direct
# inou.slang SV->LNAST front-end: ln:/pyrope: only until tolg can lower it).
sh_test(
    name = "lhd_reader_test",
    srcs = ["lhd_reader_test.sh"],
    data = _FLOW_DATA + ["trivial.v"],
)

# --config lhd.toml pass-flag defaults (strict TOML subset; CLI flags win).
sh_test(
    name = "lhd_config_test",
    srcs = ["lhd_config_test.sh"],
    data = _FLOW_DATA,
)

# ECMA-426 source-map egress ([[1f]]-G): .v.map sidecar with sourcesContent,
# in-process and lg:-reload (hash-validated disk recovery + drift rejection).
sh_test(
    name = "lhd_srcmap_test",
    srcs = ["lhd_srcmap_test.sh"],
    data = _FLOW_DATA,
)

# --dump parse|lnast|lg screen debug observables (stderr; stage-forcing).
sh_test(
    name = "lhd_dump_test",
    srcs = ["lhd_dump_test.sh"],
    data = _FLOW_DATA,
)

# --diag-fmt auto|jsonl|pretty: stdout envelope + stderr diagnostic rendering
# (auto = isatty(stdout): pretty on a terminal, jsonl when piped/captured).
sh_test(
    name = "lhd_diag_fmt_test",
    srcs = ["lhd_diag_fmt_test.sh"],
    data = _FLOW_DATA,
)

# ln.cat / ln.diff LNAST debug tools (stdout payload; hhds tree edit distance).
sh_test(
    name = "lhd_ln_tools_test",
    srcs = ["lhd_ln_tools_test.sh"],
    data = _FLOW_DATA,
)

# `lhd list options [REGEX]` / `lhd describe pass.flag` (the --set/--config
# vocabulary from the EPRP label registry) + strict --set/--config validation
# (typo'd pass or flag = usage error, never a silent no-op).
sh_test(
    name = "lhd_list_options_test",
    srcs = ["lhd_list_options_test.sh"],
    data = _FLOW_DATA,
)

# `lhd help X` == `lhd X --help` parity, sub-command-specific pages, and the
# --diag-fmt pretty|jsonl rendering of every help page.
sh_test(
    name = "lhd_help_test",
    srcs = ["lhd_help_test.sh"],
    data = ["//lhd"],
)

# --recipe O2 (pass.cprop + pass.bitwidth): cell-type mix LEC'd against the
# source, recipe provenance in --result-json, and the memory sizing path.
sh_test(
    name = "lhd_bitwidth_o2_test",
    size = "large",
    srcs = ["lhd_bitwidth_o2_test.sh"],
    data = _FLOW_DATA + [
        "bw_mem.v",
        "bw_mix.v",
    ],
)

# --recipe O1 (pass.cprop): a runtime Set_mask write chain plus single-bit
# reads must fold through Cprop::try_find_single_driver_pin (the recursive
# single-driver resolver) so y0->a and y2->c in the generated Verilog.
sh_test(
    name = "lhd_setmask_bitread_test",
    srcs = ["lhd_setmask_bitread_test.sh"],
    data = _FLOW_DATA + ["setmask_bitread.prp"],
)

# `--emit-dir pyrope:DIR` re-emission of a construct-rich source: the
# coalescer upass per-op hooks (toln consumer present) + pass.prp_writer
# unit emission, with the verifier discharging every cassert.
sh_test(
    name = "lhd_prp_writer_test",
    srcs = ["lhd_prp_writer_test.sh"],
    data = _FLOW_DATA + ["writer_rich.prp"],
)

# `lhd pyrope lsp` scripted JSON-RPC session over stdio: initialize negotiation,
# didOpen/didChange/didSave/didClose, pull diagnostics on a broken buffer,
# MethodNotFound, and clean shutdown/exit.
sh_test(
    name = "lhd_lsp_test",
    srcs = ["lhd_lsp_test.py"],
    data = ["//lhd"],
)

# `lhd pyrope fmt` — the Pyrope source formatter (prpfmt). Stdout default,
# idempotence, -i in-place (and its no-op on formatted input), --indent, a
# clean parse-error exit, the -i/-o guard, and the `pyrope lsp` alias.
sh_test(
    name = "lhd_fmt_test",
    srcs = ["lhd_fmt_test.sh"],
    data = ["//lhd"],
)

# Kernel debug observables: --dump lg (multi-graph stderr dump), --verbose
# (step-log mirroring), --depfile (make-style prerequisites).
sh_test(
    name = "lhd_debug_obs_test",
    size = "large",
    srcs = ["lhd_debug_obs_test.sh"],
    data = _FLOW_DATA + ["bw_mix.v"],
)

# `compile --unused-inputs FILE` (Bazel unused_inputs_list): declared source
# positionals absent from the compiled closure, one cwd-relative path per
# line. Positive case = a .sv outside the --top hierarchy; pyrope roots are
# always compiled (empty list); the file is created even when empty.
sh_test(
    name = "lhd_unused_inputs_test",
    srcs = ["lhd_unused_inputs_test.sh"],
    data = ["//lhd"],
)
