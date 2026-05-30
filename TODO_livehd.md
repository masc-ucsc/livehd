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

  **Depends on** [[1v]] (envelope — Phase A landed). Sibling of [[1d]] (producer
  bit lowering) and [[1i]] (comb inline, done). **Blast radius:**
  `lnast/lnast_nodes.def` + `lnast_writer`/`lnast_parser`, `inou/prp/prp2lnast.cpp`,
  all upasses (attributes biggest — deletes wrap/sat sticky logic + the 3-node
  declaration correlation), runner dispatch, `pass/lnastfmt`,
  `inou/slang/slang_tree.cpp`, `lnast_to_lgraph`, `inou/cgen`.

- **1d** Bit-range / bit-selection lowering bugs in
  `inou/prp/prp2lnast.cpp`. Pyrope's `#[…]` syntax (bit read,
  bit write, bit reductions) is partially broken on the LNAST
  producer side — multiple cases silently emit wrong code instead
  of either a proper `get_mask`/`set_mask` or a hard error.
  Sibling of [[1t]] (which adds the constprop/upass side); this
  entry covers the prp2lnast-only fixes that can land first.
  - **Symptoms** (observed via `inou.prp |> lnast.dump`):
    - `b#[1..<4] = 1` lowers to `assign ref('b#[1..<4]') 1` —
      bit-range syntax leaks into the ref text. Cause:
      `process_lvalue_for_assign` has no `bit_selection` arm and
      falls through to the text-fallback at `prp2lnast.cpp:827`.
      Expected: a `range` / `minus` setup matching the read path,
      then `set_mask new_word b mask 1`, then plain `assign b
      new_word`. Mirrors how `member_selection` lvalues already
      emit `tuple_set`.
    - `const a.b:u3 = 1` silently lowers to `tuple_set a b 1`,
      dropping the `const` decl and the `:u3` type cast. Cause:
      `process_lvalue_for_assign`'s `member_selection` arm ignores
      `decl_node` and `type_cast_node`. Pyrope has no syntax for
      "declare new tuple field via path" — this must be a hard
      compile error. Same fix applies if the LHS is a
      `bit_selection`.
    - `r = b#[1,4]` silently drops the `,4` in
      `bit_selection_to_node`. Cause: index walker treats the first
      child as the entire mask. Reject comma-lists inside `#[…]`
      (tuple indices are not supported; if we later want them, lower
      to a bitmask OR — but error first).
  - **Plan:**
    1. Lift `make_const_mask` / `make_range_mask` / `emit_range`
       out of `bit_selection_to_node` into a shared helper so both
       read and write paths reuse the same mask synthesis.
    2. Add a `bit_selection` arm to
       `process_lvalue_for_assign`. Shape: walk the bit_selection
       to recover `base` and the `select` subtree, compute
       `mask_ref` via the shared helper, emit
       `set_mask new_word base mask_ref rvalue` followed by
       `assign base new_word`. `set_mask` stays pure-functional
       (matches LGraph Dlop shape: `set_mask dest input mask
       value`), `assign` writes the result back.
    3. In both the `member_selection` and (new) `bit_selection`
       arms, hard-error if `decl_node` or `type_cast_node` is
       non-null: `const`/`mut`/`:type` are only legal on bare
       identifiers.
    4. In `bit_selection_to_node`, error on a comma-list inside
       the `#[…]` index node.
  - **Affects:** `prp-bitreverse`, `prp-bitset`, `prp-formux`,
    `prp-valid_unknown_bits` bit-range portion, and the
    `pp.prp` / `assert.prp` repros from the working tree. The
    write-side fix is a prerequisite for [[1t]] (the constprop
    bit-range work) — without it, set_mask never reaches
    constprop in the first place.

- **1i** Comb-call inliner as a **virtual splice into the runner's
  single linear traversal** — no deep copy. **Goal 1. ✅ DONE 2026-05-29.**

  **STATUS: COMPLETE.** `evaluate_callee_inline`, `try_eval_comb_call`, and
  `maybe_fire_setter_init` were DELETED from constprop (~1285 lines). The runner
  virtual-splice now handles every shape the evaluator did. Suite is back at the
  exact baseline (109 pass / 18 fail; the 18 are identical preexisting failures
  unrelated to inlining). The 13 evaluator-dependent tests that broke on deletion
  were all recovered via runner fixes (see the per-test list at the end of this
  entry / memory `task-1i-inliner-status`):
  - multi-output scalar gate (tuple-VALUED-output detection, not "any tuple op");
  - file-named combs (lookup prefers a real-signature body over the file module);
  - signed-output sext in the epilogue + output pre-declaration in the inlined
    top scope (branch-assigned outputs survive block-leave);
  - ref-param writeback (`__ref_arg` marker → positional);
  - void combs (cassert/cputs or ref-param side effect; guarded vs implicit-return);
  - bundle/tuple actuals (recursion gate accepts const bundles; bundle-literal
    expansion; tuple-param via bundle alias);
  - closures (function-name actuals) + method dispatch (typename bundle → fn,
    self = leading positional actual, bundle-aliased ref writeback);
  - nested tuple-valued multi-output: `is_tuple_field_key()` keeps tuple-literal
    KEYS raw under inlining + fold tuple-field VALUES in emit_op_with_fold.

  **Why.** Today `uPass_constprop::evaluate_callee_inline` is not an
  inliner: it's a *constant evaluator* — a second, partial
  reimplementation of the runner's statement semantics over its own
  side maps (`local_values`, `local_attrs`, `local_ranges`,
  `local_decl_widths`). It binds actuals→params, runs a fixed-point
  sweep, and returns the **output `Const`s**. It is **all-or-nothing**:
  if any statement can't fold to a constant it aborts and the `fcall`
  stays runtime. That design (a) duplicates the real `process_*`
  handlers — every construct must be hand-coded twice (the
  2026-05-28 session had to add `attr_get [bits]`, `type_spec`, and
  nested-`stmts` support just to inline `forunrollbits.prp`), and
  (b) can never emit *partially*-folded hardware. The recent fixes are
  a stopgap; 1i supersedes the mini-interpreter.

  **Decision: no deep copy.** The upass is a **single linear pass**
  (an optional 2nd pass runs only when the 1st couldn't because an
  `import` LNAST wasn't ready). Deep-copying the callee body into the
  caller tree would require re-traversing the generated code and
  re-running passes to stabilize — many passes until fixpoint. We do
  not want that. Instead the runner, on hitting an `fcall` whose comb
  LNAST is in `function_registry`, **descends its cursor into the
  callee's stmts as part of the same walk**, so the existing
  `process_*` handlers process the inlined body in-order, once.
  Whatever folds, folds; whatever stays runtime becomes real IR at the
  call site. No second implementation, no all-or-nothing.

  **Mechanism.**
  1. **Inline-frame stack.** Add a stack of frames to the runner;
     each frame = `{const Lnast* callee, rename_prefix,
     return_target(s), ref_writeback_map, depth}`. The tree cursor
     (`move_to_child/sibling/parent`) gains a thin redirection layer:
     when the walk would step past an inlinable `fcall`, push a frame
     and redirect subsequent stepping into `callee`'s `stmts` child;
     when that body is exhausted, run the epilogue and pop, resuming
     the caller at the `fcall`'s next sibling. This is a *virtual*
     splice — the callee tree is read in place, never copied or
     mutated.
  2. **Fake-assign prologue (arg binding).** Before descending, bind
     each actual to its formal as a synthesized `assign`
     (`<prefix>param = actual`), field-wise for tuple/bundle args and
     positional-or-named matching the existing `Call_actual` logic.
     These bindings feed the *same* symbol-table path `process_assign`
     uses — no special evaluator. A `ref` param records a writeback
     entry so the epilogue copies `<prefix>param` back to the caller
     variable.
  3. **α-rename / aliasing (the crux).** Because nothing is copied, the
     callee nodes keep their original names (`x`, `n`, `___1`…). Every
     name read/written while a frame is active must be namespaced by
     that frame's `rename_prefix` so it can't collide with caller
     temporaries or with other call sites of the same callee. Resolve
     names through the frame stack (innermost-first); the prologue's
     RHS actuals resolve in the *enclosing* frame's namespace, the
     LHS params in the *new* frame's. Pick a prefix scheme that's
     stable per call site (e.g. `<callee>$<callsite_id>$`) so a 2nd
     upass pass reproduces the same names.
  4. **Epilogue (output + ref writeback).** Map the callee's declared
     outputs back to the `fcall` destination(s): `dst = <prefix>out`
     (single), or field-wise (`dst.field = <prefix>out.field`) for
     tuple / multi-output, mirroring `try_eval_comb_call`'s current
     bundle splat. Apply `ref` writebacks (`caller_var =
     <prefix>param`). Then pop the frame.

  **Constraints / guards.**
  - **comb only.** Skip `pipe[N]` / `mod` / anything stateful — their
    state/timing is instance-specific and can't be naively spliced.
  - **Depth / cycle guard.** Reuse the `kInlineMaxDepth` seed; detect
    direct/indirect recursion via the frame stack and refuse (a
    recursive comb without a static bound can't inline).
  - **Import not ready.** If the callee LNAST isn't in
    `function_registry` yet (pending `import`), do **not** inline this
    pass — leave the `fcall`; the existing 2nd-pass-after-import
    mechanism picks it up. Ties into the `pending_import` poison work
    ([[2k]] / `docs/upass_redesign.md`).
  - **Idempotence across the optional 2nd pass.** Same call site must
    produce the same rename prefix and the same bindings so a re-run is
    a no-op where the 1st pass already inlined.

  **Retire / fold in.** Once 1i lands, `evaluate_callee_inline` and its
  duplicate handlers go away (or shrink to a thin pure-constant fast
  path for cassert discharge if profiling shows it's worth keeping).
  The verifier's cassert discharge then rides on the normal folded IR
  rather than the evaluator's returned `Const`s.

  **Relationship.** Unblocks the general case behind the
  bit-range/`set_mask` folding in [[1t]] and the `.[bits]`/`.[max]`/
  `.[min]` width reads that [[1v]] (typesystem) publishes — inlined
  bodies query the same type info as caller-scope code. Sibling of
  [[1d]] (producer-side bit lowering) only in that both feed the
  runner clean IR.

  **Affects.** `forunrollbits.prp`, `pp/pp2/pp3.prp` repros, and any
  comptime test that calls a comb with non-constant-foldable internals
  (those abort under the current evaluator and would instead inline to
  real IR). Validate that the inlined IR matches what a hand-flattened
  body would produce, and that multi-call-site renaming stays
  collision-free.

  ### Concrete design (grounded in `upass/runner/`, 2026-05-28)

  **Locked decisions** (this session): **(1) full runner replacement** —
  inlining moves out of constprop into the runner and
  `evaluate_callee_inline` is deleted; **(2) pure emit+fold** — always
  emit the inlined body and let constprop fold + `process_if`
  dead-branch-prune; recursion terminates when a folded-false cond
  prunes the recursive arm; a node-emit **fuel budget** is the safety
  valve (exceed → emit the `func_call` verbatim as runtime + warn).

  **Architecture facts that shape it.** The runner
  (`upass/runner/upass_runner.cpp`) is a *single linear walk* where all
  passes plug into `dispatch_to_passes`; it reads the source via an
  `Lnast_manager` cursor (`upass/core/lnast_manager.hpp`) and emits a
  staging tree with operand folding (`emit_op_with_fold`). It already
  does **dead-branch elimination** in `process_if` (lines 888-1095) —
  the recursion-termination primitive. Every main-walk pass reads names
  *only* via the `lm` cursor (verified: 0 raw-`lnast` name reads outside
  the to-be-deleted evaluator), so a rename applied inside
  `Lnast_manager` is seen uniformly by every pass's symbol table **and**
  the emit path — no per-pass edits. The runner already reserves an
  (empty) `function_registry` field for exactly this (`upass_runner.hpp:74`).

  **Core mechanism — source-swap + uniform rename (no deep copy).**
  The `Lnast_manager` gains a frame stack. At an inlinable `func_call`
  the runner: (a) reads actuals from the call node; (b) emits a
  *prologue* of binding assigns `<tag>param = actual` (synthesized in a
  scratch tree, processed through the normal walk so constprop folds
  them and records `<tag>param` in its ST); (c) `push_source(callee,
  tag)` — the manager swaps its active source tree to the callee body
  and activates a per-call-site rename tag; (d) walks the callee `stmts`
  via the ordinary `process_lnast`, so all passes fold/observe and the
  renamed, folded body is emitted into the caller's staging; (e)
  `pop_source`; (f) emits an *epilogue* mapping `dst = <tag>output` and
  `caller_var = <tag>refparam` (scratch, processed normally). The
  callee body is **read in place** — never copied.

  **Rename scheme.** Applies to **ref** nodes only (variables), never
  `const` (literals / attr-keys / the `comb` kind). Preserve tmp-ness:
  `x → <tag>x`, but `___N → ___<tag>N` so `Lnast::is_tmp` /
  `anchor_for`'s `___` special-case still hold. Per-call-site `tag` is
  derived from the call-site source nid (stable across the optional 2nd
  pass → idempotent). Renamed names are interned in a manager-owned pool
  so `current_text()`'s `string_view` stays valid.

  **Scope-key fix.** constprop keys `block_scope` by raw
  `lm->get_current_nid().get_class_index()` (`upass_constprop.cpp:788`);
  across the callee tree those indices collide with caller indices. Add
  `Lnast_manager::current_scope_uid()` = `(frame_salt<<40)|class_index`
  and have constprop (and any other nid-keyed pass) use it. Salt is 0
  with no active frame → no behavior change off the inline path.

  ### Phases (each keeps the suite green)

  - **A — manager primitive.** `Lnast_manager`: `lnast` const-ref →
    owned `shared_ptr`; add frame stack + `push_source/pop_source`,
    tag-rename in `current_text()/current_node()`, intern pool,
    `current_scope_uid()`. No caller pushes frames yet → no behavior
    change. Add a focused unit test.
  - **B — scope-key migration.** constprop `process_stmts` uses
    `current_scope_uid()`; audit/migrate other nid-keyed passes. Still
    inert (salt 0).
  - **C — runner func_call splice (non-recursive).** Populate runner
    `function_registry` (comb bodies). In `process_lnast` `func_call`
    case: if callee ∈ comb registry → splice (prologue / push_source /
    walk / pop / epilogue); else fall back to the existing `A_OP` path
    (typecasts, cell-ops `__sum`, markers stay in constprop). Land
    `pp3`/`forunrollbits` via the new path.
  - **D — recursion + fuel.** Frame-stack cycle/depth guard + node-emit
    fuel; exceed → verbatim runtime `func_call` + `log`. Lean on
    `process_if` pruning for comptime-bounded recursion.
  - **E — delete the evaluator.** Remove `try_eval_comb_call`,
    `evaluate_callee_inline`, and the 2026-05-28 stopgap handlers.
    `process_func_call` keeps only typecast/cell/marker folds.
  - **F — setter-init / method-dispatch / closures / verifier.**
    Route `maybe_fire_setter_init` (entry 1k) through the runner splice
    (synthesize a `func_call` or a runner hook). Resolve `obj.method()`
    to the qualified comb at the call site. Bind function-name actuals
    (closures). Ensure inlined `cassert`s count once via the normal walk
    (drop `mark_inlined_cassert_*` if redundant).
  - **G — green + new tests.** Multi-call-site collision, recursion,
    partial-fold (runtime body emits correct IR), parity vs hand-flatten.

  **Status (2026-05-28): Phases A–C landed, zero net regression**
  (`//inou/prp:all` = 109 pass / 18 fail, the same pre-existing baseline).
  Implemented: `Lnast_manager` source-frame stack + uniform rename
  (`push_source`/`pop_source`, user vars → `<tag>name`, tmps → fresh
  `___<N>`, intern pool, `current_scope_uid`); constprop/attributes
  block-scope keys on `current_scope_uid`; runner `try_inline_func_call`
  (prologue type_spec+bind → body walk → epilogue); coalescer
  `flush_deferred` before every source-swap (fixed a stale-nid crash);
  recursion detection + precomputed `inlinable_callees_` gate. The
  runner inlines the supported shape and **bails everything else to the
  still-present evaluator** — so this is the foundation, not yet the full
  replacement. Per-instantiation cassert counting was adopted (decision
  this session); `scope2`/`attr_comptime_query`/`assert_ifelse2`
  expectations bumped with a count-note.
    - **E reassessed (2026-05-29): deleting `evaluate_callee_inline` is a
      MAJOR architectural effort, not 6 small features.** Measured it by
      stubbing `try_eval_comb_call` to return false (forcing runner-only) and
      running the comptime suite: **16 tests still depend on the evaluator**
      beyond the 18 baseline — basic calls (`fcall1/2/3`, `simple`, `assert`),
      `does_test1`, `paths_if`, `phase5_const_check`, `ref_comb`, recursion
      (tree_sum), closures (`closure_capture`, `fcall6`), methods/setters
      (`fcall5/5b`, `fcall_rename_deep`, `setter_complex`, `setter_multi_field`,
      `tuple_decorator_complex`). The runner folds only a *subset* of casserts
      per test; the residual unknowns need: bit-range/`get_mask` folding in
      inlined bodies, multi-output **destructure** (`(b,c)=dox()` — the dst is
      multiple refs, not a single bundle tmp, so the current `emit_inline_tuple`
      path doesn't even fire), tuple actuals, method dispatch, closures,
      setter-init. **Several of these need the runner to read constprop's
      bundle/typename symbol-table state** (is a ref a const bundle? what's a
      var's typename?) — i.e. the shared symbol table (Step C of the upass
      redesign), a large prerequisite. **Key trap:** every step that makes the
      runner inline *more* (e.g. resolving `comb fcall1` in module `fcall1`)
      regresses tests, because the runner then *shadows* the working evaluator
      for shapes it can't fully fold. So the evaluator can't be removed
      incrementally without first making the runner fold each shape completely.
        - **Landed (net-safe, 109/18 deterministic):** `emit_inline_tuple`
          (single-bundle-tmp multi-output), positional-placeholder aliasing
          (`placeholder_callees_`), and — the campaign foundation —
          **shared-ST read access**: `uPass::provide_bundle_fields` /
          `provide_typename` (constprop overrides) + runner
          `try_bundle_fields` / `try_typename` (`shared_st_passes_`). Additive
          /inert; gives the runner the bundle/typename introspection the
          remaining feature ports need.
        - **Decision (2026-05-29): committed to the campaign** (delete the
          evaluator). Ordered next steps, each consuming the foundation and
          validated by stubbing `try_eval_comb_call`→false:
            1. tuple actuals (`try_bundle_fields` → bundle-param prologue via
               `emit_inline_tuple`; also fixes the recursion const-arg gate for
               bundle args → `tree_sum`).
            2. bit-range/`get_mask` folding in inlined bodies (`fcall1/2`).
            3. multi-output **destructure** `(b,c)=dox()` (dst is *multiple*
               refs, not one bundle tmp — needs a distinct epilogue path).
            4. method dispatch (`try_typename` → resolve `obj.method`, bind
               `self`).
            5. closures / function-name actuals.
            6. setter-init (entry 1k) — `try_typename` + a runner first-assign
               hook.
          Delete `evaluate_callee_inline` + `try_eval_comb_call` only once
          runner-only (stub) is fully green. **Trap to remember:** making the
          runner inline more shadows the evaluator, so each shape must fold
          *completely* before its bail is removed.
        - **NOTE:** the "second flaky heap bug" seen mid-turn was a FALSE ALARM
          (memory pressure from running sanitizer builds + tight loops
          concurrently). Quiet machine + clean build = 0/30, suite deterministic
          109/18. Don't judge flakiness under self-induced load.
    - **Phase D DONE (2026-05-28) — via a const-arg gate, NOT an iterative
      rewrite.** First diagnosis was wrong: the fib stack-overflow was not
      depth-driven. Root cause (found by tracing param bindings): the
      *standalone* function body (`is_function_body`, param is an unbound
      input) inlines its own recursive call with a non-const arg, so the
      base-case cond never folds → unbounded unroll → stack overflow. The
      call sites (const args) fold fine at natural shallow depth. Fix:
      **a recursive callee is only spliced when every actual folds to a
      comptime constant** (only then does `process_if` prune the base
      case); non-const recursive calls (standalone bodies, runtime args)
      stay a runtime func_call. recursion.prp now passes via the runner
      (fib≤10, fact≤6) — stable 4/4. Kept: fuel (`inline_budget_` 200000 +
      `kInlineMaxDepth` 256 backstop for pathological const-arg recursion
      like f(n)=f(n+1)) and the tuple-param exclusion (`tree_sum` → still
      evaluator, pending tuple-actual support in E). So the iterative
      rewrite is unnecessary; recursion no longer needs the evaluator.
    - **Heap UAF FIXED (2026-05-28).** The nondeterministic crashes
      (string_interpolation etc.) were a heap-use-after-free in
      `upass_attributes_migrate.cpp:201`: `type_info_map.emplace(path,
      ti_it->second)` where `ti_it` is an iterator into the *same* map — the
      emplace rehashes and invalidates it. Found via `--config=asan` (ASan
      reported it deterministically on `fcall6.prp`). Fixed by copying the
      `Type_info` out by value before the emplace. Suite is now
      **deterministic: `//inou/prp:all` = 109 pass / 18 fail** across
      repeated runs (was flaky 18↔20). This unblocks reliable Phase E
      validation.

  **Open risks to watch.** (1) `current_text()` lifetime under rename
  (intern pool); (2) other passes (attributes/bitwidth) that key state
  by nid need the scope-uid too; (3) DCE (`dead_code_eliminate_staging`)
  must still recognize renamed tmps (`___<tag>N`); (4) func_extract
  still spawns standalone callee bodies — verifier must not double-count
  casserts between the standalone body and the inlined copy
  (`verifier_include_funcs`); (5) the `runner_symbol_table` (Step C) is
  idle today — decide whether 1i finally activates it or stays on
  constprop's private `st`.

## Group 1-complex — foundation, larger scope

Tasks that are independent of other Group 1/2 work but are large enough
that they warrant their own bucket. Can be done in parallel with regular
Group 1 entries; downstream Groups treat them as Group 1 dependencies.

- **1b** New CLI: `setup/run/status/list/describe`, TOML config, JSONL
  results, error classes — `docs/contracts/future_cli.md`.
- **1f** Source-map indirection (LOC propagation: canonical map + per-cell
  index, alias multi-loc, partition-root fallback) — see "Source location
  (LOC) propagation strategy" below and `docs/contracts/sourcemap.md`.
## Group 2 — depends on Group 1

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
  lgraph passes (+ tests for expected diagnostics).
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
