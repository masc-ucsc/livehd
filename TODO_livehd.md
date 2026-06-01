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

- **1n** Relocate `upass/bitwidth` from an iterative fold-pass to a standalone
  **read-only finalization pass**. **Goal 1.** **★ Authority = `upass/upass.md`
  §2 "bitwidth" + §1 finalization phase + §8 gates + §11 step 9 — read those
  first; this entry is the implementation checklist.** Today `bitwidth` is a
  `fold_ref`-capable plugin interleaved in the runner walk that flushes a
  name-keyed `lnast->bw_meta()` side map with **no consumer**, and reports
  out-of-range only via `Pass::warn`. Target: a single whole-tree pass that runs
  *after* the runner and *after* SSA rename/flatten, derives exact `Dlop`
  `max`/`min` per result, errors on **provable** type-envelope overflow, and
  publishes `max`/`min` as **per-node HHDS tree attrs** for `lnast_to_lgraph`.

  **Status (2026-05-31) — N1–N5 LANDED + overflow capture centralized (green,
  net-neutral; NOT git-committed).** Full `bazel build //...` green; `//upass/...`
  9/9. `//inou/prp` is **net-neutral** verified by stashing the change set and
  running uncached: my-change failure set ⊆ baseline failure set (zero new
  failures; the 4 overflow tests + wrap/reinterpret tests pass). NOTE: the prp
  suite is currently flaky — absolute counts bounce (8→16) because of a
  pre-existing latent uninitialized-memory issue on the comptime/string path
  (goal 1s) surfacing as spurious "undeclared variable" validator errors
  (`cputs_basic`, `crand_test1`, `named_tuple`, `range_force`,
  `string_interpolation`, `trivial_if2`, `attr_size`); the stable feature-gap
  fails are `enum_*`, `match_arms_mixed`, `setter_complex`,
  `tuple_decorator_complex`, `bitreverse`. Files: `pass/upass/pass_upass.{cpp,hpp}`,
  `upass/bitwidth/{upass_bitwidth.cpp,upass_bitwidth.hpp,lnast_range.hpp,
  upass_bitwidth_test.cpp,BUILD}`, `upass/attributes/upass_attributes.cpp`,
  `lnast/lnast.hpp`, + 3 new `inou/prp/tests/errors/overflow_{func,unroll,unsigned}.prp`.
  - **N1** ✓ `fold_ref`/`overrides_fold_ref` deleted (out of `fold_capable_passes`)
    — kills the stale-narrow-range bug class; constprop's own folding covers it.
  - **N2** ✓ **Reconsidered:** bitwidth runs as the **LAST opt pass in the runner
    walk** (read-only / votes keep), NOT a separate post-runner runner. Reason
    (discovered during overflow work): a typed declaration's init value is a
    SEPARATE `store` that DCE removes for unused vars, so a post-DCE pass can't
    see it — only an in-walk pass observes the store PRE-DCE, which is required to
    catch overflow on a dead comptime const (`const x:s4=200`). `fold_ref` removal
    still gives the decoupling. (The earlier separate-runner impl + Gate A/B were
    reverted.)
  - **N3** ✓ (no-inf) `neg_inf`/`pos_inf` collapsed to a single `unbounded` flag
    in `Lnast_range` + `BitwidthEntry`; no half-bounded state. **Residual:** int64
    bounds (exact-`Dlop` swap deferred — `lnast.hpp` stays `Dlop`-free, no >64-bit
    path) and name-keyed `bw_meta` (per-node HHDS-attr re-keying waits on the
    `lnast_to_lgraph` consumer).
  - **N4** ✓ **overflow capture centralized in bitwidth.** `process_declare`/
    `process_type_spec` record the `prim_type_int(max,min)` scalar envelope; an
    `end_run` range-vs-envelope check emits `core/diag` `bitwidth-overflow`
    ("does not fit", throws → non-zero exit). Signedness-aware (unsigned negatives
    reinterpret/force, only positive-beyond-max errors; signed = strict
    containment); deferred to end_run so a per-statement `wrap`/`sat` marker
    emitted AFTER its store still exempts the var (decl-mode wrap/sat also
    exempts). The two `uPass_attributes` overflow `upass::error`s were REMOVED
    (the negative→unsigned reinterpret, which rewrites the value, stays). **Scope:
    scalars only** — dotted tuple-field names (`ar.y`) are skipped (avoids the
    fcall2 false positive on an out-of-range param-field binding); per-field
    overflow is a future refinement (1t per-field types). Tests:
    `overflow_signed` (now via bitwidth), `overflow_unsigned` (u3=100),
    `overflow_func` (s4 acc=7+7 inside an inlined comb), `overflow_unroll`
    (u4 acc sums to 45).
  - **N5** ✓ `div`/`mod`/`sext` tightened (`|a/d| ≤ |a|`, `|a%d| < |d|`,
    `sext → [-2^p, 2^p-1]`); `set_mask` stays conservative ("where derivable").
  - SSA stays a whole pass before the runner (so bitwidth, last opt pass, is
    already after SSA); the harvest/rename split is moot under the in-walk design.

  **Model (locked 2026-05-31, confirmed with the user).**
  - **Read-only.** Never rewrites nodes / never alters declared
    `prim_type_int(max,min)`; only annotates. **No `fold_ref`** (drop
    `overrides_fold_ref`) — removes the stale-narrow-range-overrides-constprop
    bug class (mut-var widening); constprop already folds its own consts.
  - **Exact `max`/`min` only — no `+inf`/`-inf`.** `Dlop` is unlimited precision,
    so finite bounds never overflow; "no derivable bound" = **absent** (distinct
    from a concrete `[min,max]`). `bits`/`sign` derived on demand (`sign = min<0`),
    never stored. Aligns with [[1t]]'s range model + `lnast2lgraph.md §7-8`.
  - **Output = per-node HHDS `flat_storage` tree attr** keyed by the result node
    (a node→data map, NOT a Pyrope attr), replacing `lnast->bw_meta()`.
    Annotating the *final* (post-runner, post-SSA) tree is what makes per-node
    keying viable — node identity is stable because no rewrite follows.
  - **Overflow = compile error only when provable.** Typed target: error iff the
    result range *provably* exceeds the declared envelope AND the write carries no
    `wrap`/`sat` policy; unknown/unbounded into a typed target is **not** an error
    (the declared type is the width). Via `core/diag` (diagnostics foundation,
    landed): stable `code`,
    `category=source`, source span, collect-and-continue.
  - **Gated finalization** (`upass/upass.md` §8): **Gate A** — skip *all*
    finalization (SSA rename/flatten + bitwidth + readers + lower) for the whole
    program if any bundle carries `pending_import` (avoids rework +
    import-resolution-order non-determinism); **Gate B** — skip *this LNAST* if it
    is a generic (untyped-input) function body, realized only by inlining into
    callers; an un-inlined survivor at a real call site is a compile error.

  **Phases (each keeps `//upass/...` unit tests green).**
  - **N1** — decouple from the fold loop: delete `overrides_fold_ref`/`fold_ref`
    on `uPass_bitwidth`; remove `bitwidth` from the `pass_upass.cpp` runner order
    and the runner's `fold_capable_passes`; confirm the suite is net-neutral
    (constprop's own folding covers what bitwidth's `fold_ref` fell back to).
  - **N2** — run standalone in the finalization phase: invoke after the runner +
    after SSA rename/flatten over each `ln` in `pass_upass.cpp`, behind Gate A
    (pending_import — stub until poison lands, migration step 7) and Gate B
    (generic fn body, detected via `io_meta` `bits==0`).
  - **N3** — data model: drop `neg_inf`/`pos_inf` from
    `Lnast_range`/`BitwidthEntry`; store exact `Dlop` `max`/`min` (absent =
    unbounded). Delete `lnast->bw_meta()`; emit per-node HHDS tree attrs that
    `lnast_to_lgraph` reads at node creation to set bits/sign.
  - **N4** — overflow error: promote the unsatisfiable-constraint `Pass::warn`
    (`upass_bitwidth.cpp:592`) to a `core/diag` error fired only on *provable*
    envelope overflow (unless `wrap`/`sat`).
  - **N5** — lattice completeness: tighten `div`/`mod`/`sext`/`set_mask` (and any
    op returning `unbounded` today) to real bounds where derivable (`a/d ≤ a`,
    `a%d < d`, …) so the N4 check is sound without false positives.

  **SSA split (prereq, `upass/upass.md` §2 "SSA").** I/O-metadata harvest stays
  *before* the runner (the comb inliner reads `io_meta` mid-walk; also detects
  Gate B via `bits==0`); rename/flatten moves *after* the runner into the gated
  block, immediately before bitwidth. (`docs/upass_redesign.md` Step I's long-term
  goal folds rename/flatten into the runner emit; either satisfies "SSA naming
  complete before bitwidth, on the tree `lnast_to_lgraph` consumes".)

  **Depends on** [[1t]] (`prim_type_int(max,min)` envelope — structural done),
  the `core/diag` diagnostics surface (foundation landed), the comb inliner (1i — landed)
  for Gate B, and the pending-import poison (migration step 7) for Gate A.
  **Untouched:** `pass/bitwidth` (the LGraph-level MIT pass) stays as the
  Verilog-ingress fallback — different path, only shares the name (see [[1g]]
  build note: "leave `pass/bitwidth` alone"). **Blast radius:**
  `upass/bitwidth/upass_bitwidth.{hpp,cpp}` + `lnast_range.hpp`, `lnast/lnast.hpp`
  (`bw_meta`/`BitwidthEntry` removal), `pass/upass/pass_upass.cpp` (order +
  finalization sequencing + gates), `upass/runner` (drop `bitwidth` from
  `fold_capable_passes`), `upass/ssa` (split), and the future `lnast_to_lgraph`
  (consumes the tree attrs).

- **1s** Sanitizer pass — chase a nondeterministic memory bug in the
  comptime/string path. **Goal 1.** **Run on Linux (macOS can't do MSan).**
  - **Symptom.** `string_hello` (and likely other comptime/string tests)
    crash *intermittently* — `Bus error (SIGBUS)` or `Killed (SIGKILL/137)`,
    sometimes `exit 1` after a full dump — at a **few-percent rate with fixed
    input**. Fixed input + nondeterministic crash ⇒ an **uninitialized-memory
    read** (value depends on heap garbage), not a UAF.
  - **It is NOT in committed HEAD.** Pure `HEAD` ran **0/100**. The bug lives in
    the current **working-tree WIP**: the `inou/prp/prp2lnast.cpp` `declare`/
    `store` frontend work + the error-handling changes in `main/main.cpp`
    (catch→`has_errors()`→exit 1) and `parser/elab_scanner.cpp` (dropped the
    dbg-only `I(false)` abort). Crashes ~2–6% even with those two error-handling
    files reverted, so the root is most likely in the `prp2lnast` string /
    interpolation lowering (`"Hello a is {a}"`) — an uninitialized field on a
    node/span/string emitted there.
  - **Amplifier (why this blocks cleanup).** Removing the dead `changed` flag
    (`uPass::mark_changed`/`has_changed`; no production reader — see commit that
    dropped `max_iters`) is **blocked on this**. Editing `store_trivial`
    (`upass/constprop/upass_constprop.hpp`, a hot inlined helper) is provably
    behavior-identical (`Symbol_table::set` always returns `true`) yet perturbs
    codegen/heap layout enough to push the crash rate ~5% → ~20%. So the flag
    removal was reverted; redo it once this is fixed.
  - **ASan does NOT catch it** (built `--config=asan`, ran clean — its redzones
    change layout and it doesn't detect uninitialized reads). Needs **MSan**.
  - **Reproduce.**
    ```
    bazel test //inou/prp:prp-string_hello --runs_per_test=60   # expect a few FAILs
    # or, against a built binary, loop the comptime pipeline:
    for i in $(seq 1 60); do rm -rf /tmp/shh; mkdir -p /tmp/shh; \
      HOME=/tmp/shh bazel-bin/main/lgshell \
      "inou.prp files:$PWD/inou/prp/tests/comptime/string_hello.prp |> pass.lnastfmt \
       |> pass.upass constprop:1 verifier_pass:1 verifier_fail:0 |> pass.lnastfmt |> lnast.dump" \
      >/dev/null 2>&1 || echo "FAIL $i"; done
    ```
  - **MSan setup.** Add an `msan` config to `.bazelrc` mirroring the existing
    `asan` block (`-fsanitize=memory -fsanitize-memory-track-origins=2
    -fno-omit-frame-pointer`); build `--config=msan //main:lgshell` and run the
    loop above. Origins tracking should point straight at the uninitialized
    field. Relates to the `core/diag` diagnostics foundation (landed; the
    error-handling path is half the WIP) and the
    `prp2lnast` frontend.

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
  lgraph passes (+ tests for expected diagnostics). **Foundation landed** (the
  former 1z `core/diag` task: record schema + emitter + sink + JSONL + text
  renderer, spans `null`); this entry finishes full-fidelity `source_id` spans
  (via [[1f]]),
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
time lg "inou.prp files:xx.prp |> pass.upass verifier:0 ssa:0 bitwidth:1 constprop:0 attributes:0"
```

and have the pass framework:

- Enumerate the pass flag matrix (`verifier`, `ssa`, `bitwidth`,
  `constprop`, `attributes`, …) and run the user-selected
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
