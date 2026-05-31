# TODO — LiveHD internal refactor

Pending work on LiveHD internals: CLI, upass infrastructure, source-map
machinery, LGraph cleanup, simulation/debug substrate, test reorg,
benchmarks, and HHDS-side optimizations.

Items use the same Group N letters as the master plan in [TODO.md](TODO.md).
Items in the same group can be done in parallel; all letters in group N must
complete before group N+1 starts. Group letters are shared across
[TODO_prp.md](TODO_prp.md), [TODO_verilog.md](TODO_verilog.md),
[TODO_livehd.md](TODO_livehd.md), and [TODO_hhds.md](TODO_hhds.md), so
cross-file dependencies stay visible.

## Group 1 — foundation

- **1t** Clean type / declaration / write model for LNAST. **Goal 1.**
  **★ Full spec + all locked decisions + worked examples live in
  [`docs/contracts/typesystem_clean_plan.md`](docs/contracts/typesystem_clean_plan.md)
  — read it first; this entry is the implementation checklist.** Supersedes the
  original `1t` declare/store sketch and folds in [[1v]]'s envelope, the wrap/sat
  `2m`, `1w` (`attr_set`/`tuple_set` unify), `1c` §11.5 (validator), TODO.md
  **Cluster F**, and the type-coupled remainder of `2q`. Removes three
  conflations: (a) `assign` vs `tuple_set` → **one `store`**; (b) the
  `attr_set`+`type_spec`+`assign` declaration cluster → **one `declare`**;
  (c) types-as-attributes → a clean **TYPE / MODE / OVERFLOW / ATTRIBUTE** split.
  `lnast_spec.md §11.3` (no-assign-collapse) is **superseded** by the `store`
  unification (decision 2026-05-29).

  **Model (locked — see the spec for detail).**
  - Scalar kinds are only `prim_type_int`(+`(max,min)` range) / `prim_type_bool`
    / `prim_type_string` / `prim_type_range` / `prim_type_none` (last unifies
    `none_type`+`unknown_type`). `bits`/`sign` **derived** from the range, never
    stored (`sign = min < 0`); `ubits`/`sbits`/stored-`bits` cease to exist.
    Aligns with `lnast2lgraph.md §7-8` + the `Dlop` value model.
  - Type sizing only via the **type-call** `int(max=,min=,bits=)` + `uN`/`sN`
    sugar (params refine sugar bounds; `bits=` reconciles to the range); **no
    `:[…]` size attributes**.
  - `declare( var, <type>, const(mode), [value] )` — universal bind node
    (statement, typed `tuple_add` field payload, `comp_type_tuple` field).
    `<type>` ∈ `prim_type_*` / `comp_type_*` / **`ref(NamedType)`** (named type =
    `ref` to its symbol-table bundle — no `expr_type`). `mode` = **`Lnast_mode`
    enum** `{mut, const, reg, mut_comptime, const_comptime, type, ref}` (one
    canonical token + `from_sv`/`to_sv`, mirrors `Lnast_ntype`; `mode==type`
    replaces `type_def`; `ref` = param modifier, encoding TBD; NO `await`/`stage`).
  - `store( var, level0..levelN, value )` — replaces `assign`+`tuple_set`
    (0 levels = scalar→wire; N = path→flatten pre-LGraph). One `process_store`
    branches on arity.
  - Derived reads (`bits`/`ubits`/`sbits`/`max`/`min`/`size`/`sign`/`typename`/
    `key`/`comptime`) computed from the type/bundle; `attr_set` of any = compile
    error. `attr_set`/`attr_get` survive only for hardware/synthesis/user attrs.
  - `stage`/`await` are NOT typesystem (await deprecated): `stage[a..=b] foo:T =…`
    = a normal `declare` + a separate timing check
    (`range`/`get_time`/`in`/`cassert`; needs a future `get_time` op).
  - Memory/flop config (`rdport`/`wensize`/`clock_pin`/`reset_pin`/…) folds into
    new `prim_type_memory`/`prim_type_register` nodes mirroring `graph/cell.hpp`
    `Memory`/`Flop`/`Latch`/`Fflop` pins (later phase).

  **Status (2026-05-30) — T1-T5 STRUCTURAL DONE; green (suite 244 pass / 10 fail
  across //inou //upass //lnast //pass = baseline +1; full `bazel build //...`
  green; NOT git-committed).** `declare`/`store`/`prim_type_int` are live in the
  producer + all upasses; the type-call + uN/sN sizing, store arity dispatch, the
  decl-cluster→`declare` merge, wrap/sat-as-declare-mode + the Cluster-F unsigned
  coercion all landed. **10 legacy nodes DELETED** (`prim_type_uint`/`sint`/
  `type`/`ref`, `comp_type_mixin`, `unknown_type`, `type_def`, `expr_type`,
  `tuple_set`, and **`assign`**) + `prim_type_boolean`→`prim_type_bool`. Every
  write/bind is now the single `store` node (statement scalar, field-path,
  tuple-literal field payload, func/io signature param, typed `name=value:type`),
  disambiguated by parent context. **4 lnastfmt validators**: store/declare
  shape, type-only-on-declare, no-`attr_set`-of-derived, and **declare-once**
  (const-redeclare per scope, `mut` reset legal, nested-scope shadowing).
  `valid_unknown_bits` GREEN (the `ones == 0sb1111_1111` cassert was a
  value-vs-bitpattern test bug → corrected to `0ub1111_1111`). Full detail in
  `docs/contracts/typesystem_clean_plan.md`.
  **Only non-green 1t item:** `typesystem` test's 5 unfoldable casserts = task-1v
  named-type *semantics* (default borrowing, typename propagation, comptime
  non-stickiness, recursive typenames, complex-field `[bits]`), NOT 1t structural
  work — high regression risk to constprop bundle machinery; left for a 1v pass.

  **Phases (each keeps `//inou/prp:all` green).**
  - **T1** — producer recognizes the type-call `int/uint/uN/sN(max=,min=,bits=)`
    (`function_call_type` callee = type keyword; today `int(max=3)` mis-lowers to
    a tuple) → `prim_type_int(max,min)`; extend
    `uPass_attributes::Type_info` to carry the `(max,min)` range (today
    `kind`+`bits`); teach `process_type_spec` + `derive_bits/max/min` to consume
    `prim_type_int`; `bits`/`sign` purely range-derived; switch `uN`/`sN`
    emission to `prim_type_int`. **Migrate the 6 size-attr tests** off `:[…]`/
    `::[…]`: `wrap_complex` (`::[sbits=8]`→`:s8`, `::[ubits=8]`→`:u8`),
    `wrap_trivial` (`::[ubits=12]`→`:u12`), `tuple_simple_attr`
    (`::[bits=5]`→`:uint(bits=5)`), `phase2_attr_max_min` +
    `phase8_size_attrs_partial` (`:int:[max=N,min=M]`→`:int(max=N,min=M)`).
  - **T2** — `store`: producer statement `assign`/`tuple_set`→`store`; runner
    `process_store` (arity branch); update the ~15 structural sites (DCE
    `dce_is_def_producing`, ssa `stmt_has_dest`, coalescer barriers,
    `upass/core/lnast_manager.hpp::is_tuple_field_key`, `pass/lnastfmt`
    `parent_writes_pos0`, `upass/prp_writer`, runner `set_function_registry`);
    tuple-literal field payloads → `store`.
  - **T3** — `declare`: producer decl-cluster→`declare`; typed tuple fields→
    nested `declare`; named types→`comp_type_tuple` of field declares; `type
    Foo`→`declare(mode==type)`. `process_declare` in `upass/attributes`
    (type_info+mode+comptime), `upass/constprop` (value + shape-merge over
    declare/store payloads), `upass/bitwidth` (range). Stop emitting
    `attr_set(type/comptime/ubits/sbits)` + `tuple_get`-for-field-type; update
    `is_tuple_field_key`/shape-merge to read `declare`/`store`; `declare` always
    live in DCE; `inou/slang/slang_tree.cpp` (`create_declare_bits_stmts`) emits
    the type node, not `__ubits`.
  - **T4** — wrap/sat-as-qualifier + Cluster-F: narrowing reads the range at the
    write; out-of-range comptime write without wrap/sat = hard error. Targets
    `prp-valid_unknown_bits`, `prp-typesystem`. (The earlier surgical constprop
    pin was reverted — it conflicts with `sat`/unknown-width because the
    qualifier wasn't on the node; this phase puts it on the write.)
  - **T5** — validator + cleanup: `pass.lnastfmt` enforces store/declare shape,
    type-only-on-declare, declare-once (post-constprop, dead-branch aware),
    no-`attr_set`-of-derived-reads. Delete `assign`/`tuple_set`/`prim_type_uint`/
    `sint`/`type_def`/`prim_type_type`/`prim_type_ref`/`expr_type`/
    `comp_type_mixin`/`unknown_type`; rename `prim_type_boolean`→`prim_type_bool`.
  - **T6** (deferred) — LGraph lowering: derive sign/bits from the range at cell
    creation (`lnast2lgraph.md §7`); `wrap`→`get_mask`, `sat`→`mux+get_mask`;
    flatten N-level `store`; `prim_type_memory`/`register` + `ref`/`get_time`
    follow-ups.

  **Depends on** [[1v]] (envelope — Phase A landed). Sibling of the now-landed
  producer bit lowering (1d) and comb inliner (1i). **Blast radius:**
  `lnast/lnast_nodes.def` + `lnast_writer`/`lnast_parser`, `inou/prp/prp2lnast.cpp`,
  all upasses (attributes biggest — deletes wrap/sat sticky logic + the 3-node
  declaration correlation), runner dispatch, `pass/lnastfmt`,
  `inou/slang/slang_tree.cpp`, `lnast_to_lgraph`, `inou/cgen`.

- **1g** Stop converting `Const`→`int` (`to_i`/`is_i`) in the **uPass passes** —
  do all numeric/bit work in `Const`, on `hlop` top-of-tree where the `int`
  shift/sext overloads are now **protected**. **PARTIAL, 2026-05-30. Goal 1.**
  Broader value-layer cleanup (legacy LGraph path + symbol deletion) is [[2t]].
  - **Design (confirmed with the user 2026-05-30).** `to_i`/`is_i` collapse a
    `Const` to one `int64_t`, so they break on >64-bit values and the
    present-but-nil sentinel ([[1b]] hit the nil case → abort in
    `blop.hpp::negn`). Rules:
    1. **Op arguments stay `Const`.** hlop made `shl_op(int)`/`sra_op(int)`/
       `sext_op(int)` protected; call the `const Dlop&` overloads — pass the
       value through (`args[0].sext_op(args[1])`). For a genuinely computed
       width (e.g. `n-1` in `wrap_to_signed`), wrap with `Dlop::create_integer`.
    2. **Bit ranges → `Const` mask arithmetic.** `foo#[start..=end]` builds the
       mask `((1<<(end-start+1))-1)<<start` from the `start`/`end` `Const`s, then
       `value.get_mask_op(mask)`; open `start..` → `value.sra_op(start)`. No
       `to_i`, no `lo<0`/`hi<lo` guards (a nonsense range yields a degenerate
       mask). (`apply_range_mask`, constprop `process_set_mask`, prp2lnast.)
    3. **Unsigned coercion → store the declared max `Const`, mask with it.** For
       `uN`, `max == 2^N−1 ==` the N-bit mask, so the first-write reinterpret is
       `v.is_negative() ? v.and_op(max) : v` — no width/`bit_test(N)`/`to_i`.
       (constprop `process_declare`→`decl_unsigned_max_`, `process_assign`.)
    4. **Ranges/derivation carry max/min `Const`s** (never bits): upass/bitwidth
       computes the possible `max`/`min` value per op (`a+b` ⇒ `a.max+b.max`,
       `a.min+b.min`; const ⇒ `max==min`); `.[bits]` derives from the `Const`
       (`get_bits`), not from `to_i`+`bit_width`. (This is the model — unlike
       pass/bitwidth which is intentionally max/min-**bits**-based and keeps
       `to_i`; **leave pass/bitwidth alone**.)
    5. **External-table indexing is a fair `to_i` exception.** Indexing a C++
       container (string `substr`, bundle/tuple key index, building a bounded
       range bundle, LNAST node int payload) genuinely needs an `int` — and an
       index ≥ 2^64 is physically impossible, so `to_i` is fine there. Sites:
       constprop string-slice (`:2327`), tuple-key (`:1435`), key-string
       (`:2379`), range→bundle build (`:2285`).
  - **Landed (all green; `upass_constprop_test` passes):**
    `lgraph_manager.hpp` (div/sub folds → `div_op`/`sub_op` = the real overflow
    fix; `==1` → `same_repr`; guards → `is_integer`); constprop `sext` (pass
    `Const`), `apply_range_mask` + `process_set_mask` (`Const` mask),
    `process_declare`/`process_assign` coercion (`decl_unsigned_max_`);
    `wrap_sat.hpp` `sext_op(create_integer(n-1))`; `attributes_read:53`
    (`same_repr(-1)`).
  - **Remaining uPass numeric sites:** `attributes_read` derive-bits + range
    guards (×~7), `attributes.cpp` [[1b]] range guards (`is_i`→`is_integer`) +
    bits-attr read, `ssa` derive-bits (×4), `prp2lnast` mask positions +
    `bits_to_bounds`, `lnast_manager:150` (value→const-node ⇒ `to_pyrope`).
    `stringify_one` (`string()` cast, `:53`) needs an arbitrary-precision
    **decimal** formatter in hlop (`to_pyrope` is hex >63) — FLAGGED.
  - **Build note:** the new hlop pin also breaks the non-uPass `int`-overload
    callers — `pass/cprop` (`:379`), `inou/yosys` (×3), `inou/cgen` — which must
    move to `Const` args too ("other passes"); `pass/bitwidth` is intentionally
    left on `to_i`. Until those compile, the full `lgshell`/comptime build is
    blocked (verify uPass via `//upass/...` unit tests meanwhile).

## Group 1-complex — foundation, larger scope

Tasks that are independent of other Group 1/2 work but are large enough
that they warrant their own bucket. Can be done in parallel with regular
Group 1 entries; downstream Groups treat them as Group 1 dependencies.

- **1y** New CLI: `setup/run/status/list/describe`, TOML config, JSONL
  results, error classes — `docs/contracts/future_cli.md`. (Renamed from
  `1b` — that letter is the Pyrope range-fit task in TODO_prp.md.)
- **1f** Source-map indirection (LOC propagation: canonical map + per-cell
  index, alias multi-loc, partition-root fallback) — see "Source location
  (LOC) propagation strategy" below and `docs/contracts/sourcemap.md`.
- **1z** Error-notification foundation: a unified diagnostic surface emitting
  one JSONL record per compile error/warning, plus a `text` renderer/filter.
  **Goal 1.** Full design in
  [`docs/contracts/diagnostics.md`](docs/contracts/diagnostics.md). Pulled
  forward from **3f** because it is decoupled from sourcemap — diagnostics ship
  now with `span: null` (or best-effort tree-sitter byte ranges in `inou/prp`)
  and get full-fidelity spans when [[1f]] lands (that's what **3f** finishes).
  - **Why now.** Errors today are throw-and-die with a bare string
    (`Pass::error`→`Eprp::parser_error` `pass/common/pass.hpp:39`;
    `upass::error` `[[noreturn]]` throw, ~24 sites; `inou/prp` `Pass::error` ×7)
    — no severity, no class, no location, no collect-and-continue, nothing an
    agent can parse. The only piece blocked on [[1f]] is the `span` field.
  - **Scope (this entry).**
    1. `livehd::diag` — `Severity`/`Span`/`Diagnostic`/`Sink` + JSONL serializer
       + active-sink plumbing (mirrors `Pass::eprp`).
    2. Route `Pass::error`/`Pass::warn`/`upass::error` through the sink (keep
       throw-on-fatal); all ~30 sites keep working, spans `null`.
    3. `inou/prp` best-effort raw `TSNode` byte spans.
    4. The JSONL→text renderer (clang/rust-style; degrades to one line when
       `span` is null) + `--format text` wiring stub.
    5. Stable `code`/`category` on the highest-value diagnostics first
       (range-fit, declare-once, unknown-node, unsupported); long tail defaults
       to `category:internal`, migrated incrementally.
  - **Relationship.** Foundation for **3f** (full-fidelity spans via [[1f]] +
    lgraph-pass coverage + CLI roll-up) and **4h** (golden-error harness keys on
    the stable `code`/`category`); the CLI [[1y]] consumes the JSONL.
## Group 2 — depends on Group 1

- **2t** Finish removing `Dlop`/`Slop` `is_i()`/`to_i()` from the value layer
  (and delete the symbols) — **not started.** Depends on [[1g]] (the uPass passes
  + `inou/prp` already migrated to `to_index()`, and `to_index` landed in hlop).
  - Migrate the **legacy LGraph path** to `to_index()` / `Const` ops:
    `pass/bitwidth/bitwidth.cpp` (13), `pass/bitwidth/bitwidth_range.cpp` (6),
    `pass/cprop/cprop.cpp` (11), `inou/cgen/cgen_verilog.cpp` (11),
    `core/pin_tracker.hpp` (3), `graph/const_pin.cpp` (2).
  - Migrate the **hlop internals**: `eval.hpp` (shl/sra/mux/mem),
    `dlop.cpp` (shl/sra/mux/lut/mem). The `to_pyrope`/`to_string`/`to_verilog`
    formatting fast-paths read `base()` directly behind an internal
    `get_bits()<=62 && !has_unknowns()` check (no public accessor needed). Plus
    the 5 hlop test files (`dlop_test`, `slop_test`, `eval_test`,
    `slop_dlop_diff_test`, `slop_sint_diff_test`).
  - Then **delete** `Dlop::is_i`/`to_i` (`dlop.hpp`/`dlop.cpp`) and
    `Slop::is_i`/`to_i` (`slop.hpp`). Push + pin-bump. Keep both repos green.

- **2h** Demand-driven incremental upass cache keyed on
  `(tree_body_hash, deps.interface_hash)` —
  `docs/contracts/architecture.md §4`.
- **2j** Hot-reload tier reporting in JSONL (`hot-debug` / `hot-approx` /
  `cold`) — `docs/contracts/architecture.md §8`.
- **2k** Verifier `pending` counter + `:type: top` semantics. Once the
  upass `pending_import` poison mechanism (see `docs/upass_redesign.md`)
  is functional, the verifier needs a third disposition: a cassert whose
  cond is still poisoned at end-of-walk is `pending` (not pass, not fail).
  Add a `verifier_pending:N` counter alongside `verifier_pass` /
  `verifier_fail`. For tests tagged `:type: top` (whole-program runs),
  pending casserts are rolled into the pass count — the contract is "at
  the top level all imports must resolve, so any surviving pending is a
  bug." For non-top tests (libraries with deferred imports), `pending`
  is a legitimate disposition. Complications worth working out:
    - A cassert that started pending on invocation N and then proves
      true on invocation N+1 should count once as pass, not once as
      pending then again as pass. Tally lifetime is per-program, not
      per-invocation.
    - `:type: top` aggregation crosses LNASTs in `var.lnasts` (including
      func_extract-spawned bodies) and the `verifier_include_funcs` knob
      already in `pass.upass`. The pending→pass roll-up should respect
      that scope.
    - Reporting: an unresolved pending at `:type: top` end-of-run needs
      a diagnostic naming the blocking `import`(s); requires keeping
      enough info on the `__pending_import` marker to identify the
      blocker (today the redesign uses a presence-only flag).
    - A cassert that started pending and later proves false is a hard
      error, same as any known-false cassert.

## Group 3 — depends on Group 2

- **3c** `Partition` descriptor as a tree-level HHDS attribute (`kind`,
  `latency_range`, ports, `ext`, `interface_hash`, `state_shape_hash`) —
  `docs/contracts/architecture.md §3`.
- **3d** New upass `lnast_to_slop` (parallel to `lnast_to_lgraph`) producing
  executable slop.
- **3f** Unified compile error/warning surface from `inou/prp`, upass,
  lgraph passes (+ tests for expected diagnostics). **Foundation landed in
  [[1z]]** (record schema + emitter + sink + JSONL + text renderer, spans
  `null`); this entry finishes full-fidelity `source_id` spans (via [[1f]]),
  lgraph-pass coverage, line/col resolution, and the CLI roll-up. Design:
  `docs/contracts/diagnostics.md`.
- **3h** `Bitwidth_range` → `Const min/max` (drop `int+overflow`) once Dlop
  migration is stable — see "Bitwidth_range" section below.

## Group 4 — depends on Group 3

- **4c** Hot-probe injection at checkpoint replay (no recompile) —
  `docs/contracts/simulation.md`.
- **4d** What-if `poke(signal, value)` engine with safety semantics.
- **4e** Queryable indexed trace store (slice, transition, distribution
  queries).
- **4f** Invariant-detector codegen → emits `debug.prp` the agent can edit.
- **4g** Checkpoint format + reload validator (after slop codegen lands in
  [TODO_livehd.md](TODO_livehd.md) **3d**); partition-keyed via
  `state_shape_hash`, covers memories and registers for large simulation —
  `docs/contracts/architecture.md §9`.
## Group 5 — polish / final

- **5a** Rewrite `simlib/{uint,sint}.hpp` as part of the HLOP runtime;
  retire `firrtl-sig` attribution — see "simlib fixed-width int types"
  below.
- **5b** Attribute hot-path benchmark (flat_storage vs lhtree regression
  check) — `docs/contracts/hhds_migration.md §8`.
- **5c** Agent-legible globally-unique, optimization-stable signal names —
  `docs/contracts/simulation.md`.
- **5d** Test reorg per "Test Organization" below (`tests/` layout,
  `contracts/` split, `*_test.cpp` rules).
- **5e** IDAP perf-sweep pass: hold correctness flags steady, drive each
  optional pass on/off to measure marginal cost and find regressions —
  see "IDAP perf-sweep pass" below.
- **5f** Benchmark contracts (extend `benchmark/`): codegen throughput,
  simulation speed, agent-loop iteration latency — regression gates so the
  "seconds matter" promise of `docs/contracts/architecture.md §1` stays
  measurable.

## Test Organization

Tests are scattered across the codebase. Some subsystems place tests in
dedicated `tests/` directories (core, lgraph, lnast, inou/pyrope) while others
have test files alongside source code (upass, pass/label, pass/mockturtle,
main, inou/lefdef, inou/liveparse).

Rules to enforce:

- All main C++ tests must live in a `tests/` subdirectory under each component.

- A file should only use the `_test.cpp` suffix (e.g., `foo_test.cpp`) when
  there is a corresponding `foo.cpp` in the parent directory. Otherwise it is
  just a standalone test and should not carry the `_test` suffix.

- When the `_test` convention is used, it must be a suffix, never a prefix
  (`foo_test.cpp`, not `test_foo.cpp`).

- Create a separate `contracts/` directory (per component or top-level) for
  higher-level integration and API tests. Contracts test only public/high-level
  APIs and overall integration. Agents should not edit contract tests directly;
  they are maintained by hand to guarantee stable interface behavior.

- Treat compile-driver scripts as contracts, not ordinary tests. Move
  `yosys_compile.sh`, `slang_compile.sh`, and the other `*_compile.sh` scripts
  out of `tests/` directories and place them under a consistent `contracts/`
  structure.

## Source location (LOC) propagation strategy

Decide how LOC anchors (`file:line:col`, module, pipeline-stage) flow through
LNAST and LGraph so that `sigref("alu.flag_z")`, hot-probe injection
(`docs/contracts/simulation.md`), waveform-to-source mapping, and the
invariant-detector code generator can all map an internal signal back to
agent-editable Pyrope without best-effort heuristics.

### Hard commitments (not in scope for this decision)

- Every `Partition` (`comb` / `pipe[N]` / `mod`) keeps its declaration LOC.
- Every port (input/output of a partition) keeps its declaration LOC.
- Every register and memory declaration keeps its LOC.

These ride on the partition descriptor / port struct and are mandatory
through every pass.

### Open: how to handle everything else (internal wires, SSA temps, synthesized cells)

The naive "anchor on every value" approach is expensive and brittle in practice.
The favored direction is a **source-map indirection**: build one canonical
source map from the original Pyrope, and let internal names (SSA / temp /
synthesized) point at one or more locations in that map (so an alias
`a = b` can legitimately reference both `a`'s and `b`'s sites).

#### Issues to resolve

- Alias chains (`a = b; c = a`): does `c` map to `{a, b}`, `{a, b, c}`, or
  follow transitive closure? Closure could explode on deeply aliased SSA.
- Synthesized cells with no surface name (e.g., a `Mux` born from an
  `if/elif` lowering, a `Sum` materialized during slice/concat lowering):
  which source location is the "right" one — the condition, the join, the
  containing statement, or the partition root as a fallback?
- Optimization passes that drop a name entirely (constprop folding,
  cell-removal): the indirection handle disappears; what does `sigref` on
  that name return after optimization?
- Round-trip through external Verilog + LEC must not lose the source map.
  Emit `// src: file:line:col` comments at egress? A sidecar `.srcmap`
  file? Both?
- Pass discipline: with per-cell LOC, every pass must remember to set
  anchors and CI catches misses. With a source map, pass authors mostly
  don't think about LOC, but a pass that *renames* an SSA value must
  update its indirection — quiet failure mode if forgotten.

#### Pros of source-map indirection

- One canonical store; cells carry a small index, not 8+ bytes of anchor.
- Multiple-location attribution is natural (alias case).
- Source-derived SSA names (`foo_l42_b` per `lnast_spec.md §13`) already
  encode source by construction — the source map is a formal version of
  that scheme.
- Verilog egress emits source-map references rather than duplicating LOC
  per wire.
- Pass surface is smaller; LOC-propagation logic is centralized.

#### Cons of source-map indirection

- Indirection adds a lookup at every debug query (`sigref`, waveform
  click-through, LEC failure attribution). Hot-probe injection needs fast
  resolution.
- Synthesized cells still need *some* attribution rule — falling back to
  the partition root is honest but coarse.
- Tools outside LiveHD (waveform viewers, external LEC) must learn the
  source-map format or rely on the Verilog-egress sidecar.
- Optimization that removes the last reference to a name loses the link;
  recovering it requires the optimization to log a "removed: <name> ->
  <loc>" entry into the source map, which is a discipline issue too.

#### Alternatives considered

1. **Per-cell LOC (full anchor on every value).** Matches the aspirational
   wording in `docs/contracts/simulation.md`. Maximum fidelity but every
   pass becomes responsible for LOC propagation; quiet failure mode when a
   pass forgets; memory cost on every cell.
2. **Boundary-only LOC.** Only the hard-commitment list above carries LOC.
   Internal signals return "no source mapping" when probed. Simplest
   contract; weakest UX for `sigref` on internal wires.
3. **Source-map indirection with multi-location names** *(favored)*. One
   source store; names index in; 1-to-many supported for aliases.
   Synthesized cells without a surface name fall back to the enclosing
   partition's anchor.
4. **LOC-on-LNAST-only, drop at LGraph.** Let LGraph be LOC-free; resolve
   through `LGraph cell -> LNAST node -> LOC` indirection at debug time.
   Requires LNAST to be retained alongside LGraph for the lifetime of a
   debug session; works only if the upass cache keeps LNAST around.
5. **Hybrid (the working assumption).** Hard commitments above + source
   map for the rest, with partition-root fallback. Verilog egress emits
   `// src:` comments derived from the source map.

#### To decide before implementation

- Source-map storage: in-memory only, on-disk sidecar, or both? On-disk is
  needed for the `runs/<test>/vN/` workflow in `simulation.md`.
- Lookup API surface: what does `sigref(name).source()` return when the
  name has 0, 1, or N locations?
- Lifetime: when an upass invalidates a tree's cache, does the
  corresponding source-map slab get invalidated atomically?
- Egress comment format: pick one (`// src: file:line:col`) and commit, so
  external LEC tooling can parse it reliably.

## Bitwidth_range: replace int+overflow with Const min/max

`pass/bitwidth/bitwidth_range.{hpp,cpp}` currently stores `int min, max`
plus an `overflow` flag, then materializes a `Const` from those when the
value escapes the range representable in `int`. This was a workaround for
when the constant value type was expensive to allocate.

With `Const = Dlop` (inline storage for size <= 1, pool storage above), the
internal `int+overflow` representation is largely redundant — `Dlop`
already handles arbitrary widths efficiently. Refactor to store `Const
min; Const max;` directly and drop the `overflow` bookkeeping. Expected
benefits:

- Fewer materialize/round-trip conversions per query (`get_max`/`get_min`
  currently rebuilds a `Const` every call; storing it eliminates the
  reconstruction).
- Cleaner arithmetic in callers — no need to branch on `is_overflow()` or
  to special-case the int-fits path; the same `Dlop` ops cover both.
- One representation for the value, simpler invariants
  (`min <= max` becomes a single `Dlop` comparison).

Deferred until after the lconst-to-Dlop migration stabilizes so that the
churn lands in one focused commit.

## HHDS PinEntry/NodeEntry: native bits + sign storage

LiveHD currently stores per-pin `bits` (Bits_t / 23-bit) and `sign` (1 bit)
as HHDS attributes (`livehd::attrs::bits` / `livehd::attrs::sign`) — see
`docs/contracts/hhds_graph_migration_plan.md §G3`. Attributes are
flat_storage hashmaps; fine for correctness, but every read does a hash
lookup.

Native fields on `PinEntry` / `NodeEntry` would be one indexed load. The
proposed layout (keeps both classes at 32 bytes by dropping `ledge1` and
widening `Nid_bits` from 42 to 48, and port_id:23):

- **PinEntry** — `master_nid:48 + next_pin_id:48 + ledge0:48
  + port_id:23 + use_overflow:1 + sign:1 + bits:23 + sedges_/overflow_idx:64`
- **NodeEntry** — `type:16 + next_pin_id:48 + ledge0:48 + use_overflow:1
  + alive:1 + pad:6 + sign:1 + bits:23 + sedges_extra:48 + sedges_:64`

Touches every overflow/edge code path in `hhds/graph.cpp` (single-ledge
fast path, EdgeRange `kInlineMax` constants, NodeEntry edge accounting).
Worth its own focused multi-commit campaign in `../hhds`. Once landed,
LiveHD drops `livehd::attrs::bits` / `livehd::attrs::sign` and the
`mirror_set_pin_bits_hhds` / `mirror_set_pin_sign_hhds` helpers, reading
`bits()` / `is_unsign()` directly off the HHDS pin/node handle.

The `GraphIO::DeclaredIoPin` graph-level bits + sign extension lands
sooner (it's the immediate Sub_node-deletion unblocker, smaller scope,
no PinEntry/NodeEntry surgery required).


## IDAP perf-sweep pass

A correctness-preserving optimization/IDAP pass that toggles individual
upass stages on and off while keeping correctness invariants in place, so
we can attribute perf regressions to a specific pass and confirm each pass
earns its runtime cost. Concretely, we want to drive runs like:

```
time lg "inou.prp files:xx.prp |> pass.upass verifier:0 max_iters:1 ssa:0 bitwidth:1 constprop:0 attributes:0"
```

and have the pass framework:

- Enumerate the pass flag matrix (`verifier`, `ssa`, `bitwidth`,
  `constprop`, `attributes`, `max_iters`, …) and run the user-selected
  combinations.
- Diff results against a reference run so disabling a pass that *must* run
  for correctness fails loudly rather than silently miscompiling.
- Emit per-pass wall-clock + allocation deltas into the JSONL results
  stream (consumed by **5f** benchmark contracts).

While building this pass, audit the hot upass plumbing for the
performance anti-patterns we keep hitting. These are the same axes the
sweep should report on, so callers can see which axis regressed:

- **Repeated allocations.** No `new` / `make_unique` / `std::vector{}` in
  inner loops. Hoist scratch buffers out of per-node code paths; reuse
  per-pass arenas where lifetime allows. Look for `clear()` + refill
  patterns and convert them into stable, reusable buffers.
- **`std::vector` returns.** Replace "build vector, return vector" APIs
  with iterators / ranges / output iterators. Where a concrete container
  is unavoidable, take it by `&` from the caller so allocation lives at
  the call site, not inside the helper. This is the broader scope of
  **1j**; **5e** finishes the sweep across non-upass code.
- **Small-vector hotspots.** When a container is known to be tiny in the
  common case (≤8 elements: per-node fanin/fanout, per-cell port list,
  pass-local work stack), use `absl::InlinedVector<T, N>` (or
  `absl::FixedArray` when the size is known at the call) instead of
  `std::vector` to keep the storage on the stack.
- **String churn.** Hunt `std::string` temporaries on hot paths: prefer
  `std::string_view`, `absl::string_view`, or pre-interned symbol handles
  (we already have `mmap_lib::str` / interned IDs — use them). No
  `to_string()` in inner loops; no `+` concatenation for log lines that
  are then discarded; no `absl::StrCat` materialization when a
  `string_view` would do.
- **Tree / graph iteration without copying.** Iterate HHDS trees and
  LGraph in place via the existing iterator APIs. Do not snapshot
  `children`, `descendants`, `fanin`, or `fanout` into a `std::vector`
  just to walk them. If a pass must mutate during traversal, use the
  iterator invalidation-safe variants HHDS exposes rather than
  materializing a copy. Forbid the "collect into vector, then iterate"
  pattern in pass code; CI should grep for the usual offenders
  (`get_children`, `get_fanin_nodes`, …) returning vectors and replace
  them with range-returning siblings.

Deliverables:

- The sweep harness itself (driver + diff-vs-reference checker).
- A short report contract under `docs/contracts/` describing the flag
  matrix, what "correctness-preserving" means per flag, and the JSONL
  schema for the per-pass timing/allocation rows.
- One pass of cleanup across upass / lgraph passes hitting the
  allocation, vector-return, small-vector, string, and copy-to-iterate
  anti-patterns above — landed *with* the sweep so the regression gates
  have something to lock in.

Sequenced as **5e** so it lands after the upass cache (**2h**) and
partition descriptor (**3c**) have stabilized — otherwise we'd be
optimizing APIs that are still moving.

## simlib fixed-width int types

`simlib/uint.hpp` and `simlib/sint.hpp` (extracted from the `firrtl-sig`
upstream project, kept under `simlib/LICENSE.firrtl-sig`) are still used by
`core/tests/lconst_test.cpp` and the simlib examples. Once HLOP lands, these
types will be replaced/rewritten as part of the HLOP simulation runtime —
revisit the simlib int surface and the `firrtl-sig` attribution at that point.
