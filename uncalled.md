# TODO 1c — uncalled LiveHD C++ functions (coverage triage)

Generated 2026-06-13 from a full `@scripts/getcov.sh` run
(`bazel coverage -c opt //...`). The numbers below come from
`cov/livehd.main-cpp.lcov`; the browsable per-line report is at
`cov/html/index.html`.

This file lists **LiveHD-owned C++ functions that still record zero calls
(`FNDA:0`) yet live in a file that has *other* functions hit** — i.e. dead
methods inside subsystems the suite otherwise exercises. Files with *no*
coverage at all are out of scope here (they are a different question than "this
method is uncalled but its neighbours run").

## Coverage snapshot

| metric | value |
|--------|-------|
| function coverage | **84.3 %** (1749 / 2075) |
| uncalled functions, all files | 339 (was 412 before this task) |
| uncalled functions in partially-covered files | 329 |
| partially-covered files with ≥1 uncalled fn | 44 |

## Tests added this task

Each is self-checking and tied to formerly-uncalled functions. All pass under
`bazel test`; coverage movement confirmed by the getcov re-run.

| test | newly-covered functions |
|------|------------------------|
| `inou/prp/tests/comptime/coalesce_ops.prp` | reduction (`red_and/or/xor`), `popcount`, `tuple_concat`, `while`, nominal `is` op dispatch in upass |
| `inou/prp/tests/pyrope/stmt_kinds.prp` | `Prp2lnast::process_loop_statement` / `process_impl_statement` / `process_spawn_statement` |
| `inou/yosys/tests/nocheck_slang_loops.v` (ladder `lec`) | `Slang_context::lower_for_loop`, `lower_while_loop` (repeat) |
| `inou/yosys/tests/nocheck_slang_foreach.v` (ladder `verilog`) | `Slang_context::lower_foreach` |
| `lhd/tests/lhd_bitwidth_o2_test.sh` | `pass.bitwidth` entry (`bw_pass`), memory sizing, cgen over O2 |
| `pass/bitwidth/bitwidth_unbounded_test.cpp` (5 new cases) | `Bitwidth::process_mult` / `process_bit_xor` / `process_bit_and` / `process_comparator` / `process_mux` + `adjust_bw`, `set_bw_1bit`, `set_wider_range`, `Bitwidth_range` ctors |
| `lhd/tests/lhd_prp_writer_test.sh` | `Lnast_prp_writer` re-emission + coalescer per-op hooks via `--emit-dir pyrope:` |
| `lhd/tests/lhd_lsp_test.py` | `lsp/livehd_lsp.cpp` 1/29 → 27/28 (full JSON-RPC session) |
| `lhd/tests/lhd_debug_obs_test.sh` | `lhd_kernel.cpp` `screen_dump_graphs`, `mirror_log_to_stderr`, `write_depfile` (the `--dump lg` / `--verbose` / `--depfile` paths) |
| `core/tests/vcd_sample_test.sh` | `core/vcd_reader.cpp` 0/14 → 12/14 (`open`/`process`/`parse*`) |
| `lhd/tests/lhd_ln_tools_test.sh` (case 7) | `ln_diff_command` unit-count-mismatch branch |

One source change supports the VCD test: `core/tests/vcd_sample.cpp` `main`
now accepts `argv[1]` so the binary can be pointed at a hermetic input. No
temporary probes remain in any source or test file.

---

## Remaining uncalled functions, by root cause (subtask E)

### 1. Dead dispatch — retired grammar rules → **delete**

`inou/prp/prp2lnast.cpp`
- `Prp2lnast::process_assert_statement(TSNode)`
- `Prp2lnast::process_function_call_statement(TSNode)`

The dispatch table (`prp2lnast.cpp:1091,1096`) keys these on tree-sitter node
types `assert_statement` and `function_call_statement`. The **pinned** grammar
(`tree-sitter-pyrope@279462b`, see `MODULE.bazel`) no longer defines those
rules — `assert(...)` and a bare `f(...)` now parse as
`function_call_expression` and route through the expression-statement path
(verified against the generated `src/node-types.json`). These two handlers and
their dispatch entries are unreachable and should be removed (or the grammar
restored to emit those nodes if the distinct handling is still wanted).

### 2. Unwired passes — registered but reachable from no `lhd` command → **wire or retire**

- `pass/isabelle/pass_isabelle.cpp` — **1/51 hit**. `pass.isabelle` is
  registered via `Pass_plugin` but no recipe (`O0/O1/O2`) or `lhd` command
  drives it, so the whole `Pass_isabelle::work` / `emit_for_graph` / cert
  emitter is dead from the CLI. Either expose it (an `lhd` step / emit-kind) or
  mark it a research artifact outside the shipped build.
- `pass/sample/pass_sample.cpp` — `Pass_sample::setup` / `wirecount` /
  `do_wirecount`. Example/demo pass with no CLI or test driver.
- `inou/yosys/inou_yosys_api.cpp` — `Inou_yosys_api::fromlg` (LGraph→Yosys
  writer; no `lhd` command emits through it) and `log_error_atexit` (a Yosys
  atexit hook only invoked on a Yosys fatal).
- `inou/yosys/lgyosys_tolg.cpp` — `Yosys2lg_Pass::help()` (EPRP help text;
  only via the old REPL help, which `lhd` does not call).

### 3. Bitwidth per-op inference — only on width-less nodes → **gtest-only**

`pass/bitwidth/bitwidth.cpp` (now **16/39**; the 23 below remain):
`process_const`, `process_flop`, `process_not`, `process_ror`, `process_sext`,
`process_shl`, `process_sra`, `process_bit_or`, `process_assignment_or`,
`process_hotmux`, `process_get_mask`, `process_set_mask`, `process_attr_set`,
`process_attr_set_bw`, `process_attr_set_dp_assign`, `insert_tposs_nodes`,
`try_delete_attr_node`, `set_subgraph_boundary_bw`, `get_key_attr`, `dump`, and
the anon-namespace `clear_sink` / `clear_all_sinks` / `setup_sink_by_name`.

Root cause: `bw_pass` (`bitwidth.cpp:1369`) early-`continue`s for any
non-multi-driver node whose driver pin **already** carries a width. Every CLI
frontend (yosys-verilog, slang, pyrope→tolg) emits fully width-annotated
graphs, so these inference branches only fire on a node whose width must be
*derived*. The new `bitwidth_unbounded_test` cases construct exactly that shape
and cover `mult`/`bit_xor`/`bit_and`/`comparator`/`mux`; the rest can be
covered by extending the same gtest (each needs a node with the right
sink-pin layout — `flop`, the masks, and `attr_set*` need attribute pins;
`shl`/`sra`/`sext` use named `a`/`n` sink pins).

### 4. Debug/dump-only entry points (NDEBUG-gated or developer dumps) → **keep, dbg-build coverage**

- `lnast/lnast_writer.cpp` — **1/79 hit**, the entire `Lnast_writer::write_*`
  family. It is invoked only from `Lnast_manager::write_node()`, which is
  `#ifndef NDEBUG` (`upass/core/lnast_manager.hpp`), so the opt build compiles
  every call out. This is developer instrumentation; coverage is only
  reachable in a `-c dbg` build. Keep, but it should not be counted against
  release-flow coverage.
- `lnast/lnast.cpp` — `Lnast::dump(string)`, `Lnast::print` (+2 lambdas),
  `Lnast_node::dump()`. (The `dump(ostream&)` overload that `ln.cat` uses *is*
  hit.)
- `inou/prp/prp2lnast.cpp` — `dump()`, `dump_tree_sitter` (2).
- `pass/bitwidth/bitwidth_range.cpp` — `Bitwidth_range::dump`.
- `upass/core/symbol_table.cpp` — `Symbol_table::dump`.

### 5. Diagnostic/error-emitter template instantiations → **keep (one source template each)**

These are per-argument-type instantiations of variadic diagnostic helpers;
each fires only when *that* error/warning is emitted with *those* operand
types. They are a single source template, so they carry low deprecation risk —
covering all instantiations would mean reproducing every diagnostic permutation.

- `core/diag.hpp` `Builder::msg<…>` — 12 instantiations
- `lnast/lnast.hpp` `Lnast::info<…>` — 4
- `upass/core/upass_utils.hpp` `upass::error<…>` — 3
- `pass/lnastfmt/pass_lnastfmt.cpp` `fmt_error<…>` — 10 (+ `node_loc`)
- `upass/timecheck/upass_timecheck.cpp` `Discharger::error_at<…>` — 3
- `upass/tolg/upass_tolg.cpp` `Tolg::error_here/error_at/warn_at` & `Time_checker::error_at_node<…>` — 31
- `pass/common/eprp.hpp` `parser_error<…>` — 2; `pass/common/eprp.cpp`
  `parser_info_int`, `parser_warn_int`
- `core/diag.cpp` `Builder::stage()`; `inou/slang/slang_context.cpp`
  `emit_error`; `upass/verifier/upass_verifier.cpp` `emit_cputs_diag`

### 6. Unused API overloads / alternate entry points → **review for deletion**

- `pass/common/eprp_var.cpp` — `Eprp_var::add` (4 overloads: `Eprp_var const&`,
  `flat_hash_map`, `unique_ptr<Lnast>`, `vector<shared_ptr<Lnast>>&`) and
  `Eprp_var::replace` (2). `lhd` only uses `add(shared_ptr<Lnast>)` and
  `add(shared_ptr<Graph>)`; these overloads are an unused surface.
- `lnast/lnast.cpp` — `Lnast::read` / `read_all` (2 each) and the anon
  `finish_read`, plus `Lnast::set_root`, `Lnast::spans_of`. The textual-dump
  read-back path; `lhd`'s `ln:` reload goes through `hhds::Tree::read_dump`
  elsewhere. Confirm whether any planned reload uses these before deleting.
- `lnast/lnast_builder.cpp` (12) — `create_red_and/or/xor_stmts`,
  `create_mod_stmts`, `create_mask_stmts`, `create_bitmask_stmts`,
  `create_declare_bits_stmts`, `create_func_call`, `create_tuple_get` (2),
  `get_lnast_name`, `get_lnast_lhs_name`. Builder helpers for ops the slang/cgen
  front-ends emit only for SV constructs the suite doesn't reach; testable with
  targeted SV but currently cold.
- `upass/runner/upass_runner.hpp` `take_new_lnasts`; `upass_runner.cpp`
  `try_scalar_kind`; `pass/cprop/cprop.cpp` `try_find_single_driver_pin`;
  `upass/core/bundle.cpp` `concat(Dlop)`, `get_bundle`;
  `upass/func_extract/upass_func_extract.cpp` `strip_io_prefix`;
  `upass/attributes/upass_attributes_wrap_sat.cpp`
  `Const_handler::on_attr_set`.

### 7. Graph color/serialize helpers — graphviz/attr-emit gated → **wire when those emits land**

`graph/node_util.hpp` `color_of`, `is_sink_connected`,
`set_type_const_serialized`; `graph/const_pin.cpp` `hydrate_const`. Consumed by
`inou/graphviz` and `inou/attr`, but the `graphviz:` emit kind is rejected as
*unsupported* in `lhd_kernel.cpp` and no attr/color pass is wired into a
recipe. They become reachable once graphviz/attr egress is implemented.

### 8. upass control-flow flush hooks & base no-ops → **need surviving control nodes**

- `upass/coalescer/upass_coalescer.hpp` — `process_for`, `process_uif`,
  `process_func_def`, `process_mod`. These `flush_all()` when a `for`/`uif`/
  `func_def`/`mod` node reaches a coalescer-active (`pyrope:`/`ln:` emit) walk.
  The coalesce/prp_writer tests use loops, but they unroll away before the toln
  emit; covering these needs a control construct that *survives* to the emit
  (e.g. a data-dependent, non-unrollable `for`/`uif`).
- `upass/core/upass_core.hpp` (15) — base-class virtual no-ops
  (`uPass::process_for/uif/top/assign/type_def/func_return/func_continue`,
  `classify_statement`) overridden by concrete passes, plus the
  `uPass_wrapper<Test_*>` registration helpers for in-test-only passes, and
  `~uPass`. Reached only if a pass leaves a hook un-overridden.

### 9. Destructors & small leaf utilities → **benign / low priority**

Destructors not attributed despite the objects being constructed and used:
`Vcd_reader::~Vcd_reader`, `Thread_pool::~Thread_pool`, `spmc256<int>::~spmc256`
(2), `uPass_assert::~uPass_assert`, `Lnast_manager::~Lnast_manager`,
`Attribute_handler::~Attribute_handler`. Plus `Vcd_reader::get_current_bucket`
(only on bucket query), `core/file_output.cpp` `strerror_threadsafe` (error
path), and assorted single-use lambdas
(`upass_attributes_tuple`, `upass_runner check_self_does`, `bundle.set`,
`prp2lnast` binary-expr / lambda-named internals, `lsp uri_to_path` char
lambda). No action needed beyond noting them.

---

## Non-C++ appendix (subtask F)

The coverage filter (`scripts/getcov.sh`) already excludes non-C++ and
`tests/` sources, so the audit surfaced **no** unused LiveHD-owned Python /
Bash / Pyrope / Verilog files to flag. The only non-test data added by this
task is `core/tests/vcd_sample_signals.vcd`, `lhd/tests/bw_mix.v`,
`lhd/tests/bw_mem.v`, `lhd/tests/writer_rich.prp`, and the two
`inou/yosys/tests/nocheck_slang_*.v` inputs — all consumed by the new tests.

## Known pre-existing failure (not caused by this task)

`//lhd/tests:lhd_usage_merge_test` fails identically in plain `bazel test`
(fastbuild) and under coverage: a crash in the yosys-verilog elaboration of
its `inv.v` input (`run_method_now` backtrace), independent of every file
touched here. `//pass/common:eprp_test` only **times out** under coverage
instrumentation (>600 s); it passes in a normal build. Both are tracked
separately from 1c.
