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
# --set lec.solver, and the retired `check` command's migration error.
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

# 2c-color: every coloring algorithm (acyclic|synth|path|mincut) + continuous +
# clear run through the lhd CLI on a real design.
sh_test(
    name = "lhd_color_test",
    size = "large",
    srcs = ["lhd_color_test.sh"],
    data = _FLOW_DATA + ["part_hier.v"],
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
