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

# pass.bitfuzz tripwires: strip the per-pin bits/sign annotations, re-infer,
# and require LEC to PROVE the fuzzed netlist equivalent to the baseline for
# one pinned design per defect class the first sweeps found. Guards the width
# contract ("bits are derived metadata; narrowing is an explicit cell") against
# every future pass that touches the annotations. See todo_bitfuzz.md.
sh_test(
    name = "bitfuzz_tripwires_test",
    size = "large",
    srcs = ["bitfuzz_tripwires_test.sh"],
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

# Incremental regeneration of the sim tree: a no-op re-setup and a comment-only
# Pyrope edit must rewrite NO generated C++, and a body-only edit must rewrite
# the module's .cpp but NOT its .hpp (else every parent's object is needlessly
# invalidated). Those mtimes are what the generated build.ninja keys on, so this
# is the test that keeps the host build incremental. Steps 1-3 need only
# --setup-only; the end-to-end warm-rebuild check self-skips without ninja.
sh_test(
    name = "lhd_sim_incremental_test",
    srcs = ["lhd_sim_incremental_test.sh"],
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

# The per-field WRITE CHAIN false combinational loop on a NESTED packed-struct
# VARIABLE: `c.f0=..; c.f1=..; c.f2=..` in one always_comb lowers to a Set_mask
# CHAIN on one packed net (a nested struct variable cannot be split into
# per-leaf nets), so a later read of one field binds to the LAST version and any
# field written after it that depends on that read closes a WORD-level cycle the
# bit-level design does not have. Pins BOTH legs of the pass/cprop fix
# separately -- the disjoint-lane skip and the contained-slice re-base onto
# `value` -- by reusing the two equiv-corpus goldens. The equiv harnesses cannot
# gate this (prp-equiv never reads the .v, while prp-v2prp2v's original-Verilog
# leg goes through yosys and its native leg deliberately scores an encoder
# refusal as inconclusive), so the strict "must come back PROVEN" assertion
# lives here.
sh_test(
    name = "struct_field_chain_cycle_test",
    srcs = ["struct_field_chain_cycle_test.sh"],
    data = [
        "//inou/prp:equiv_tests",
        "//lhd",
    ],
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

# `lhd sim` takes ln:/lg: IR inputs like `lhd compile` does, so a design compiled
# once simulates without re-reading its sources: the positionals route through the
# shared route_positional (an lg:DIR used to be swallowed as the TEST NAME), the
# absorbed lg: library joins the DESIGN so the DUT body is actually emitted, and a
# slang `--top X --emit-dir ln:` publishes the whole forest (it used to drop every
# module X instantiates, leaving an ln: dir that could not be linked). Structural
# checks are hermetic; with the sibling ../hlop + ../iassert headers it also runs
# both IR forms and the plain two-source baseline and requires the same verdict.
sh_test(
    name = "lhd_sim_ir_inputs_test",
    srcs = ["lhd_sim_ir_inputs_test.sh"],
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

# 2f-sim: the agent-oriented `lhd sim --query` API — cgen_sim's per-module
# iface.json manifest and lossless observe_signals/observe_mem surface, the
# per-test sim_catalog.json (now carrying outputs and memories, which the legacy
# describe_signals catalog never had), the five v1 operation families, and the
# unchanged legacy --probe/--break-when shapes. Structural checks run
# hermetically; the run checks need the sibling ../hlop/../iassert headers.
sh_test(
    name = "lhd_sim_query_test",
    srcs = ["lhd_sim_query_test.sh"],
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

# `lhd formal verify` test-unit face: a `formal` block is an INDEPENDENT test, so
# it enumerates and selects exactly like a sim `test`. `--list-tests` is a pure
# parse of the blocks -> JSON in sim's envelope (no design load, no solver — it
# works against a design that does not even compile) or a human listing in pretty
# mode; a lone non-path positional selects one block (anywhere among the
# positionals, glob accepted) through the same filter --formal drives; the two
# spellings together are a usage error; and a selector that matches nothing FAILS
# rather than silently proving only the design's own obligations.
sh_test(
    name = "lhd_formal_list_tests_test",
    size = "large",
    srcs = ["lhd_formal_list_tests_test.sh"],
    data = ["//lhd"],
)

# `past(x, n)` in a formal block: the first temporal operator. History is
# resolved by the ENGINE indexing the unroll (Monitor::Bind::delay), so the
# monitor stays combinational and the "property holds STATE" refusal must not
# fire; cycles without enough history behind them are skipped and DISCLOSED.
sh_test(
    name = "lhd_formal_past_test",
    size = "large",
    srcs = ["lhd_formal_past_test.sh"],
    data = ["//lhd"],
)

# SystemVerilog IMMEDIATE assert/assume become design obligations (the same
# LNAST cassert -> fproperty path a Pyrope `assert` takes). These used to be
# dropped silently, so a design whose properties are all immediate asserts
# verified as "no obligations found" -- proving nothing at exit 0.
sh_test(
    name = "lhd_sv_immediate_assert_test",
    size = "large",
    srcs = ["lhd_sv_immediate_assert_test.sh"],
    data = ["//lhd"],
)

# `break` in an unrolled for-loop = the priority-select idiom. Proven EQUIVALENT
# to the hand-written if/else-if chain by LEC, so a dropped guard or an
# off-by-one break point refutes rather than passing review.
sh_test(
    name = "lhd_sv_loop_break_test",
    size = "large",
    srcs = ["lhd_sv_loop_break_test.sh"],
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
# REFUTE, bounded bmc accepted only after pair-free reset/no-reset retry),
# entity-keyed pair hints replayed warm.
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

# The MEMORY half of the same machinery: `ordering="none"` (the Memory cell's
# `undef` matrix) must leave a same-cycle read-during-write window undefined for
# the miter, so BOTH the committed-value and the forwarding implementation of it
# prove -- while a bug OUTSIDE that window still refutes.
sh_test(
    name = "lec_mem_undef_test",
    size = "large",
    srcs = ["lec_mem_undef_test.sh"],
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

# Regression for the slang reader's STRUCT-net `wire` classification: a
# module-scope struct net the dataflow sort places after one of its readers must
# be wire-classified like the scalar in the same position. Pre-declaring struct
# leaves before `wire_syms_` existed locked them into `mut`, so the early read
# resolved to nil and the inter-module connection was silently severed (448
# warnings / 85 X-tied ports on lhdsuite //bench:minion). Also pins the
# deliberately-not-promoted RMW case and the wire-classified output port (just
# //lhd).
sh_test(
    name = "slang_struct_net_wire_test",
    size = "medium",
    srcs = ["slang_struct_net_wire_test.sh"],
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

# LIVE test for todo/livehd/2f-latch M4 (partially landed 2026-07-20) — clock
# awareness in `lhd lec`. pass/lec/encode.cpp never read a Flop's
# clock_pin/posclk, so it FALSELY PROVED three plainly-different pairs. Two are
# now closed and asserted REFUTED: gated-vs-ungated (the ICG cone folds to a
# commit guard) and two-clock-vs-one-clock (each flop commits on its own net's
# edge, with the previous level SHARED by both miter sides).
#
# The third — negedge vs posedge on the SAME clock — is still PROVEN, and the
# script says so rather than pretending: it needs SUB-CYCLE resolution (a rise
# phase then a fall phase) which a step-granular relational encoding cannot
# express. Promoted from `fixme` to live because it now guards real behavior;
# the open case is reported as a note, and flips to an assertion when the
# two-phase encoder lands.
sh_test(
    name = "lec_clock_blindness_test",
    size = "large",
    srcs = ["lec_clock_blindness_test.sh"],
    data = ["//lhd"],
)

# LIVE blast-radius test for todo/livehd/2f-latch M8 (steps 1h + 3a): edge
# normalization must stay OFF the synthesis path (lhd compile still emits a real
# always_latch / negedge -- the netlist handed to ABC has to contain the actual
# hardware), and must be SKIPPED rather than run-as-a-no-op on a design with no
# latch, no negedge state and one clock net. Both directions are asserted, so a
# trigger that under-fires OR over-fires fails here.
sh_test(
    name = "single_edge_scope_test",
    size = "large",
    srcs = ["single_edge_scope_test.sh"],
    data = ["//lhd"],
)

# LIVE test for todo/livehd/2f-latch M8 steps 3b + 3g: all FOUR commit classes
# (posedge, negedge, transparent-high, transparent-low) in ONE module. Discrimination
# is checked through `lhd lec` (collapsing any two classes must REFUTE, and a
# semantically-identical rewrite must still PROVE), and the transformation ITSELF is
# validated trace-level against Icarus Verilog -- the source netlist (real always_latch
# + negedge) vs the normalized one (posedge flops + phase divider), the normalized side
# clocked P times per source period and sampled under the observation-visibility rule.
# That iverilog leg is what the binding cross-model gating rule requires: our own
# encoder on both sides of a miter cannot see a transformation that is wrong but applied
# identically to both. It SKIPS with a note when iverilog is absent (it is a host tool,
# not a bazel dep -- same status as inou/cgen/tests/sim/diff_test.py).
# LIVE test for CLOCK GATING (todo/livehd/2f-latch): an ICG is recognized and
# rewritten into a FLOP ENABLE rather than modelled as a gated clock -- sound
# (the encoder has no clock-identity model, so gated and ungated otherwise look
# identical) and cheaper (no per-step commit term). Checks the rewrite through
# `lhd lec` in both directions AND against Icarus, with the L1 negative control
# that pins the enable-latch bypass: reading the latch's HELD Q instead of the
# value it passes through is a full cycle late and must fail.
# LIVE TRACKER for todo/livehd/2f-latch: a latch or negedge flop inside a DEF is
# refused (not lowered), which is exactly why lhdsuite's minion still needs its
# `lec_trust` list -- running minion_lec with an EMPTY list refuses naming
# `prim_rf_1r1w_preview_p2` and friends. This pins that the refusal is HONEST and
# NAMES the def (it used to come back falsely PROVEN), that `formal.lec.trust`
# still works as the escape hatch, and -- the part that keeps it a real test --
# that the refusal stays NARROW: a def with only posedge state, and an
# instantiated clock-gate cell, must both still pass.
sh_test(
    name = "single_edge_hier_latch_test",
    size = "large",
    srcs = ["single_edge_hier_latch_test.sh"],
    data = ["//lhd"],
)

# todo/livehd/2f-latch M9 — Clock_cell recognition at the Sub boundary: the
# cases INLINING structurally cannot cover (a TRUSTED gate, a MEMORY clock, and
# an enable that is a CONE rather than a bare port).
sh_test(
    name = "clock_cell_test",
    size = "large",
    srcs = ["clock_cell_test.sh"],
    data = ["//lhd"],
)

# Gate for the FORMAL PHASE SCHEDULE (2f-lec "phase-schedule" / 2f-latch M10):
# one source period encoded as ordered microsteps, so a latch closes and a
# negedge endpoint commits INSIDE the period the old encoding collapsed. Every
# proof is paired with a REFUTATION that must still fire -- a symmetric
# before/after check cannot see a transformation that is wrong but applied to
# both sides, so each mutant is built to be indistinguishable from the positive
# case unless the schedule is real (swapped latch windows; a clock guard sampled
# before the rise instead of before the fall). Case 3 pins that turning the
# schedule OFF still fails closed, by name.
sh_test(
    name = "phase_schedule_test",
    size = "large",
    srcs = ["phase_schedule_test.sh"],
    data = ["//lhd"],
)

# formal.lec.int_blast (cvc5 solve-bv-as-int): the translation to unbounded
# integers is equisatisfiable, so it must never change a verdict -- PROVEN stays
# PROVEN (comb and sequential), and a REFUTED must keep its CONCRETE
# counterexample (a witness degrading to UNKNOWN means the model round-trip
# through the translation broke). Also pins the int_blast=auto DEFAULT: a
# starved BV solve of a multiply miter must convert via the min_timeout
# int-blast retry, and int_blast=off must NOT retry. cones=false so cvc5, not
# abc, solves.
sh_test(
    name = "lec_int_blast_test",
    size = "medium",
    srcs = ["lec_int_blast_test.sh"],
    data = ["//lhd"],
)

# ONE EDGE PER MEMORY (user ruling 2026-08-01). The Memory cell has a SINGLE
# global `posclk` while yosys keeps a bit per port, and the reader took the
# WRITE vector whole and dropped RD_CLK_POLARITY — so a negedge synchronous read
# imported as posedge, and a clocked ROM (no write ports ⇒ empty write vector)
# imported as negedge. Both were silent. A memory whose ports commit on
# different edges is not a valid memory, so it is a NAMED refusal; the ROM cases
# pin that the surviving edge now comes from every clocked port, not just the
# write side.
# MEMORY READ FORWARDING under cvc5, MUTANT-GATED. A memory (Pyrope) against
# four named flops + muxes (Verilog), with a self-referential write and a
# forwarding read at a RUNTIME index -- the `needs_array` shape. Every proof is
# paired with a mutant that must still refute (forward dropped; forward not
# gated by the write enable; write-data self-read dropped), because a model that
# silently ignored forwarding would make the mutants equivalent too.
# REGRESSION: a COMBINATIONAL memory (type==2, no clock) must be evaluated on
# EVERY microstep. The M10 phase schedule enumerates CLOCKED endpoints only, so a
# comb array had no entry, fell to the Phase::Rise default, and was suppressed on
# the other three microsteps -- serving STALE reads to anything sampling at fall.
# Needs BOTH a negedge endpoint (else single_step() and the gate never fires) and
# a comb ARRAY (a scalar read is not a Memory cell), which is why it hid inside a
# register file. Reduced from minion's prim_rf_1r1w_diff_preview, sub-second.
sh_test(
    name = "mem_partsel_write_test",
    size = "small",
    srcs = ["mem_partsel_write_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],
)

sh_test(
    name = "mem_forward_test",
    size = "medium",
    srcs = ["mem_forward_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # runs the in-process cvc5 solver on compiled RTL
)

sh_test(
    name = "mem_clock_edge_test",
    size = "medium",
    srcs = ["mem_clock_edge_test.sh"],
    data = ["//lhd"],
)

# REGRESSION: a packed-struct field must lower to a LEAF net at every nesting
# depth. `struct_port_bundle_ok` used to disqualify a whole port as soon as one
# field was itself a struct, keeping it a packed WORD — so writing field X while
# reading a DISJOINT field Y became a self-reference on that word, i.e. a FALSE
# combinational cycle that `pass.lec` refused to encode. That cost three defs on
# lhdsuite //bench:minion_lec (vpu_tensorfma, plus vpu_ctrl and vpu_ml which
# instantiate it); see lhdsuite fixme.md issue 1b. Cases 2/3 are controls that
# pass either way — they prove the RTL really is acyclic, so a case-1 failure is
# a lowering defect. The structural check rejects any surviving packed-word
# masking, so the test cannot be satisfied by a smarter cycle breaker.
sh_test(
    name = "struct_nested_field_leaf_test",
    size = "medium",
    srcs = ["struct_nested_field_leaf_test.sh"],
    data = ["//lhd"],
)

# REGRESSION: the Pyrope writer must not hoist a clock gate's ENABLE CONE above
# the always_comb that cone reads. Verilog always_comb is order-independent,
# Pyrope assignment is sequential, so a relocated read landing above its writes
# silently captures the seed value. The writer therefore never reorders body
# statements for a `clock_pin`/`reset_pin` ref: the pin net is made
# POSITION-INDEPENDENT instead (already a `wire`/`reg`, a `wire X` pre-declare,
# or a minted `X__pinw` alias assigned the final value at the region end). The
# old cone-relocation machinery this replaces caused lhdsuite
# //bench:minion_lec's minion_dcache_cache_op_unit_l2 and
# minion_frontend_thread_buffer{,_p1} refutations; see lhdsuite fixme.md 1f
# and 1f-bis.
sh_test(
    name = "prp_writer_clock_cone_order_test",
    size = "medium",
    srcs = ["prp_writer_clock_cone_order_test.sh"],
    data = ["//lhd"],
)

# REGRESSION: a TRUSTED def's struct ports must pair across the two front ends.
# Box ports are keyed by NAME, so a flat bus on one side and per-leaf ports on
# the other never meet — the box outputs become unrelated free symbols and the
# parent goes Unknown (a false REFUTED at scale; symmetrically it can HIDE a
# real difference). Fixed by making a struct ALWAYS a bundle inside LiveHD
# (both front ends agree); `flat_top_io` packs only the top interface, where
# generated-vs-original Verilog equivalence needs it. This was the root cause
# of lhdsuite //bench:minion_lec's prim_mul_div / tensor_load refutations.
sh_test(
    name = "lec_trusted_box_struct_port_test",
    size = "medium",
    srcs = ["lec_trusted_box_struct_port_test.sh"],
    data = ["//lhd"],
)

sh_test(
    name = "mem_ordering_test",
    size = "large",
    srcs = ["mem_ordering_test.sh"],
    # _FLOW_DATA (lhd + the yosys lgcheck scripts) + the hermetic Liberty: the
    # abc write-priority case tech-maps and LECs, so it needs all three or it
    # degrades to a "skip" and stops covering anything. The shipped wrappers are
    # read directly (the UNDEF-parameter skew check).
    data = _FLOW_DATA + [
        "//inou/prp:abc_tests",
        "//ware:cgen_memory_rtl",
    ],
)

sh_test(
    name = "single_edge_icg_test",
    size = "large",
    srcs = ["single_edge_icg_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # invokes the host iverilog/vvp
)

sh_test(
    name = "single_edge_four_classes_test",
    size = "large",
    srcs = ["single_edge_four_classes_test.sh"],
    data = ["//lhd"],
    tags = ["no-sandbox"],  # invokes the host iverilog/vvp
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
# identifier — directly reproducible through the yosys importer; cprop now
# removes tolg's redundant Pyrope hold mux, so the Pyrope half below also pins
# the resulting fanout-1 enable and canonical RAW D + EN form; (2) emission was
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

# REGRESSION: a unit that converged in one round of the kernel's import_defer
# loop must not be re-walked by a later round (run for some OTHER blocked
# file). Re-walking is not idempotent: uPass_ssa demotes the unit's private
# `___ssa_N` names into the `__wN` namespace prp_writer emits into generated
# sources, aliasing two variables, after which constprop folds live logic to a
# constant and DCE deletes the real cone. This was the root cause of lhdsuite
# //bench:minion_lec's minion_dcache_miss_handler{,_p1} refutation (fixme.md
# issue 1i): the SAME .prp compiled to 1515 nodes alone and 1476 in-tree.
sh_test(
    name = "upass_converged_rewalk_test",
    size = "medium",
    srcs = ["upass_converged_rewalk_test.sh"],
    data = ["//lhd"],
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

# An --emit-dir may not name one of the run's own IR inputs. An emission
# replaces the directory (declaration file rewritten in full, stale bodies
# pruned), so aliasing an input destroyed it in place while still exiting 0.
sh_test(
    name = "emit_dir_alias_test",
    srcs = ["emit_dir_alias_test.sh"],
    data = ["//lhd"],
)

# Formal obligations across a module boundary: a submodule's `assume(a==3)` on an
# input is discharged (or refuted) by what the PARENT binds, not by the submodule
# in isolation. Case 3 (a parent that violates the assume must FAIL) is what
# keeps the test honest — without it, never checking anything would look correct.
# Tagged fixme: today the assume is only evaluated in module isolation, so the
# submodule cannot be compiled on its own and a violating parent still passes.
sh_test(
    name = "hier_property_test",
    srcs = ["hier_property_test.sh"],
    data = ["//lhd"],
    tags = ["fixme"],
)

# LIVE fail-closed test: a variable BLOCKING-assigned in an EDGE-sensitive
# process and read outside it is persistent flop state that `--reader slang`
# does not model. collect_state_vars documented the refusal, but it only ever
# fired for module OUTPUTS and unpacked arrays -- an internal scalar fell
# through, was declared a plain `mut`, and the REGISTER VANISHED: a
# `always @(posedge p) ms = ms + 1;` counter read by `assign o = ms` lowered to
# a stateless `comb` whose output folded to a constant, exit 0 with no warning,
# and lgcheck refuted it. Pins the diagnostic CODE (a crash also exits nonzero)
# and the acceptance half: a process-LOCAL blocking temp and an ordinary `<=`
# register must both still compile, with the register still real state.
sh_test(
    name = "blocking_ff_state_test",
    size = "small",
    srcs = ["blocking_ff_state_test.sh"],
    data = ["//lhd"],
)
