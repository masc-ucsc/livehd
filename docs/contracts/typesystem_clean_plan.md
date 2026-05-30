# Clean type / declaration / write model for LNAST

Single source of truth for the type-system redesign (supersedes the relevant
parts of `lnast_spec.md` §3/§4/§11 and folds in task 1t). All decisions below
are LOCKED unless a line says "(open)". Reconciled against the grammar, the
`Dlop` value model, `lnast2lgraph.md`, the comptime tests, and the memory notes
(2026-05-29; via the `stress-test-typesystem-plan` and
`reconcile-typesystem-sources` workflows).

## The three conflations being removed

1. **Two write nodes for one operation.** `assign(x,v)` and
   `tuple_set(x,a,b,v)` are the same write at depth 0 vs depth N. → one `store`.
2. **A declaration is a scattered cluster.** `mut x:u8 = 255` emits
   `attr_set(x,"type","mut")` + `type_spec(x,…)` + `assign(x,255)`; per-field
   tuple types add `tuple_get` + `attr_set(__ubits,N)`. → one `declare`.
3. **Types and attributes are conflated.** `uint`/`sint` as *basic* types;
   width smuggled through `attr_set("ubits"/"sbits",N)`; `bits`/`sign`
   sometimes stored, sometimes derived. → basic types are only
   **integer/bool/string** (+complex); for an integer the only refinement is
   the value range `(max,min)`; **bits and sign are always derived**.

`Dlop`/`Const` already gets the value layer right (`Type {Integer, Boolean,
String, Nil}` — no uint/sint, no stored sign/bits), and `lnast2lgraph.md` §7-8
already specifies the backend wants exactly this: *"bits and signed are derived
on demand from max/min"* and *"booleans are 1-bit signed integers (0=false,
-1=true)"*. The LNAST layer is being brought in line.

## Status (2026-05-29)

**Landed (foundation, all green; suite at 117 pass / 11 fail, the fails are
pre-existing + concurrent cell work, not this task):**
- `declare`, `store`, `prim_type_int` node types added to `lnast_nodes.def`
  (inert — nothing emits `declare`/`store` yet; `prim_type_int` emitted by no
  producer yet). Writer + dump/read round-trip test
  (`lnast/tests/parser_writer_test.cpp::declare_store_dump_read_roundtrip`).
- `wrap_to_signed` sext bug fixed in `upass/bitwidth/wrap_sat.hpp` →
  `prp-wrap_checks` + `prp-wrap_complex` pass (`wrap_checks` header count
  corrected 2→4).
- `.[sign]` derived read (`min < 0`) in `upass/attributes` + `is_builtin_attr`;
  new `inou/prp/tests/comptime/sign_attr.prp`.

**T1 — LANDED 2026-05-29 (green, suite 117/11 = baseline, 0 regressions):**
- `Type_info` (upass/attributes) extended with `range_max`/`range_min`
  (`std::optional<Const>`) — the single source for max/min/bits/sign once a
  `prim_type_int` is consumed.
- `process_type_spec` consumes `prim_type_int(max,min)` (two const children;
  `"nil"` ⇒ unbounded), recovering the legacy `kind`+`bits` view from the range
  so wrap/sat narrowing keeps working. `derive_max`/`derive_min` read the range
  first; `derive_bits` reads the recovered `bits`.
- Producer (`inou/prp/prp2lnast.cpp::emit_int_type_call`) lowers the integer
  type-call `int(max=,min=,bits=)` / `uint(bits=)` / `uN(min=…)` to
  `type_spec(target, prim_type_int(max,min))`. **GRAMMAR REALITY (key finding):**
  `int(max=3)` does NOT parse as `function_call_type` — `int`/`uint`/`uN` are
  keyword tokens, so the parenthesized form surfaces as an `ERROR` node wrapping
  the keyword plus a bare `(…)` tuple. The build pins tree-sitter-pyrope to a
  GitHub archive (NOT a local override; it is not a bzlmod module), so a clean
  grammar fix is out of scope here — the producer reconstructs the intent from
  the ERROR shape. (`typesystem.prp` case 9 `int(max=3)` now resolves: max=3,
  min=nil, bits=nil.)
- **Deferred to T5** (folded into the `prim_type_uint`/`sint` deletion, which is
  the natural place and lower-risk once declare/store exist): switching uN/sN
  *emission* to `prim_type_int`, and migrating the 6 size-attr tests off
  `:[…]`/`::[…]` (kept passing on the legacy path meanwhile). The `:[…]` size
  attrs still type-pin until then.
- `typesystem.prp` stays red (needs the 1v typename/structural/comptime-sticky
  cases — out of 1t scope); `int(max=3)` is the only 1t-owned case in it.

**T2 — `store` LANDED 2026-05-29 (green, suite 117/11 = baseline, 0 net regressions; full `bazel build //...` green):**
- Producer rewrites statement-level writes to `store` via ONE centralized rule in
  `prp2lnast.cpp::rewrite_statement_writes_to_store` (post-build walk): every
  `tuple_set`→`store`; every `assign` whose parent is a `stmts` block→`store`.
  Field-payload `assign(key,val)` (parent tuple_add/concat) and func_def/io
  signature `assign` (parent tuple_add under func_def/io) stay `assign`. This
  avoids classifying the ~20 emission sites by hand.
- Runner dispatch (`upass_runner.cpp::process_lnast`) routes `store` by arity:
  ≤2 children → `process_drop_candidate(&process_assign)` (scalar/wire);
  ≥3 children → `process_verbatim(&process_tuple_set)` (tuple field write). The
  pass methods walk children positionally, so they handle a `store` node
  unchanged — most passes needed NO edit (routed via dispatch).
- Node-type predicates taught about `store` (the non-dispatch sites):
  `dce_is_def_producing` (+store), lnastfmt `parent_writes_pos0` (+is_store),
  attributes `scan_op` is_alias (+store — THE bug that broke bundle-shape
  propagation: `foo=___1` alias only fired for literal `assign`), the 1i inliner
  `ref_aliases` scan (+0-level store), and `lnast_manager::current_num_children`
  (new helper for the arity branch). `lnast_prp_writer` got `write_store`.
- **ssa fix (subtle):** `store` falls inside `stmt_has_dest`'s `[assign,ge]` enum
  range (assign<store<ge), so a ≥3-child store-as-tuple_set was wrongly
  SSA-versioned — splitting `self.x=…; self.y=…` into different bundle versions
  (broke ref-self multi-field setters: fcall6, setter_multi_field). Fixed by
  excluding ≥3-child stores from versioning (they're in-place field writes like
  tuple_set). 2-child stores still version like assign.
- **Pre-existing flaky test note:** `prp-string_interpolation` flakes under the
  bazel harness (exit-code race during teardown) — confirmed at BASELINE too
  (1/6 with changes stashed); lgshell is deterministic (25/25 direct + 8/8 under
  ASan, no UAF reported). The larger `Type_info` (T1) may slightly raise the
  flake rate via heap pressure. Not a logic regression. `string_hello` /
  `tuple_simple4` also flake under parallel load; all pass in isolation.

**T3 — `declare` LANDED 2026-05-29 (green, suite 117/11 = baseline, 0 net regressions; full `bazel build //...` green, 18/18 upass/lnast/lnastfmt unit tests):**
- Producer merges the declaration cluster
  (`attr_set(t,"type",K)` + `[attr_set(t,"comptime")]` + `[type_spec(t,TYPE)]`)
  into one `declare(ref(t), TYPE|none_type, const(mode))` via a **copy-merge
  rebuild** (`prp2lnast.cpp::rewrite_decls_to_declare`): builds a fresh body
  tree merging contiguous clusters, then `replace_body`. (In-place
  delete_subtree is avoided on the LNAST tree — `upass_runner.cpp:1272`.) The
  value stays a separate `store`; the `typename` attr_set for named types stays
  separate (copied verbatim after the declare). mode = storage token (`mut`/
  `const`(empty)/`reg`/`await`) + optional ` comptime`.
- `attributes::process_declare` (the only meaningful consumer) reads
  `declare(var, TYPE, mode)` → populates `type_info` (kind/bits/range via the
  extracted `read_scalar_type_at_cursor` helper, shared with process_type_spec)
  + `decl_kind` + `is_comptime`. constprop/bitwidth need no process_declare (the
  value store carries value/range). Base `PROCESS_NODE(declare)` is a no-op
  default.
- Runner dispatch: `C_OP(declare)` (verbatim, never dropped — like type_spec).
  DCE keeps declare (not in `dce_is_def_producing` ⇒ never a drop candidate).
  lnastfmt `parent_writes_pos0` (+is_declare).
- **ssa fix (subtle, mirrors the store one):** `declare` falls inside
  `stmt_has_dest`'s `[assign,ge]` enum range, so ssa marked the declared var as
  "seen" — then the first value `store` became a spurious reassignment
  (`acc`→`acc___ssa_1`), diverging the declaration from its value (broke
  `forunrollbits`; `paths_if` flaked). Fixed by excluding `declare` from ssa
  versioning (it introduces a name+type, not a value write — copied verbatim).
- has_type_spec gating preserved: only a concrete numeric/string TYPE sets it
  (an un-annotated `const y = 5` → `declare(y, none_type, "")`, has_type_spec
  stays false ⇒ derived reads nil, matching the legacy attr_set-only behavior).

**T4 — Cluster-F ENFORCEMENT landed 2026-05-29/30 (green, suite 117/11 =
baseline, 0 regressions, full build). wrap/sat-qualifier-on-node deferred.**
- Implicit unsigned coercion (below) AND the **Cluster-F hard error**: a KNOWN
  positive comptime value whose magnitude exceeds the declared unsigned width
  (bit `bits` set ⇒ value ≥ 2^N > max) written WITHOUT `wrap`/`sat` is now a
  hard compile error (`upass_attributes.cpp` on_assign_like). The known-negative
  case is a legal reinterpretation (not overflow); values that fit never reach
  it. Verified: `u8 = 300` errors, `u8 = 255` / `wrap z = <wide>` don't; no
  passing test does an over-width non-wrap write (checked), so 0 regressions.
- **wrap/sat-qualifier-ON-NODE — decl-site LANDED 2026-05-30 (green):** a
  declaration-site `var:u4:[wrap]` / `:[sat]` is now folded into the `declare`
  MODE (`"mut wrap"`); `process_declare` sets the sticky `wrap`/`sat` policy
  directly from the declare node (replacing the old
  attr_set(wrap)-before-first-store + `was_assigned` sticky path). The merge
  consumes the contiguous decl-site `attr_set(wrap/sat)`. Per-statement
  `wrap x = v` stays an after-store `attr_set(wrap)` (one-shot) — putting it
  *literally* on the `store` node needs a metadata slot (`store`'s "value is the
  last child" invariant blocks a qualifier child), which is invasive with no
  functional difference (the after-store attr_set IS the per-write qualifier).
  `phase5_wrap_sticky`/`wrap_complex`/`wrap_checks`/`wrap_trivial` all green.
- **declare-once validator — ATTEMPTED, needs scope-aware analysis (reverted):**
  a syntactic same-`stmts`-block declare-once check FALSE-POSITIVES on `matrix`
  (flattened loop-unroll re-declares of a body-local var) and `scope_simple`
  (scoped re-declares). Confirmed empirically — it genuinely needs the
  post-constprop, dead-branch/iteration-aware analysis the plan specifies, not a
  syntactic check.
- **Landed:** implicit unsigned coercion at a declaration's first write
  (`upass/attributes/upass_attributes.cpp::on_assign_like`). A comptime value
  that sign-extends a KNOWN 1 past the declared width — i.e. a known-negative
  bit pattern, possibly with interior unknowns — stored to a uN-typed var reads
  as its unsigned N-bit pattern: `v:u8 = 0sb1001_0111` ⇒ 151,
  `v:u8 = 0sb1?01_?000` ⇒ `0ub1?01?000`. Gates that make it safe:
  - `bit_test(bits) && !unknown_bit_test(bits)` — the sign-extension past N is
    a KNOWN 1. An UNKNOWN sign bit (`0sb?` → `v1:u32`) is skipped, so a
    deliberate 1-bit unknown keeps its natural width (no `valid_simple`
    regression). Uses `and_op` (NOT `wrap_to_unsigned`, which bails on unknowns).
  - `!was_assigned(lhs)` — ONLY the declaration's first write. Per-statement
    `wrap x=…`/`sat x=…` are reassignments whose wrap/sat attr_set lands AFTER
    the store; coercing there would pre-mask the value out from under `sat`
    (`sat z=<neg>` must clamp to 0, not mask) — the documented wrap/sat
    conflict. (Confirmed: without this gate `wrap_complex`/`attr_sticky`
    regress; with it they stay green.)
  - `need_rhs_value` extended to materialize the RHS for unsigned-typed LHS.
- **Deferred (needs architecture):** `valid_unknown_bits`'s last assert
  (`ones==0xff` where `ones = v|0xff`) stays at baseline 9/1. The coercion fires
  (v's first write IS coerced) and the OR folds correctly in isolation, but in
  the full statement sequence **constprop folds `v|0xff` from its OWN symbol-
  table value of v (the raw signed literal), not from the coerced value in
  attributes' `tmp_fold`** — the constprop/attributes value-coherence gap (the
  shared-symbol-table, Step C of the upass redesign). Robust fix: constprop must
  read/apply the declared-type coercion at the store, OR the coercion must land
  in the shared value. The full wrap/sat-as-qualifier-on-node + Cluster-F
  hard-error remain (analysis below).
- **T4 (full wrap/sat-as-qualifier + Cluster-F hard-error):** the wrap/sat path already works
  (`wrap_checks`/`wrap_complex`/`wrap_trivial` green) via the existing
  `attr_set(x,"wrap")`-after-`store` + the `was_assigned` heuristic + the T1
  `wrap_to_signed` fix. Moving it onto a node qualifier is a *refactor of
  working code* (store has no qualifier slot; per-statement `wrap x=v` would
  need one — invasive) with regression risk and **no test win**. The Cluster-F
  hard-error has **no test** (`typesystem.prp`'s `int(max=3)=4` negative is
  commented out — the harness has no `:expect_fail:`). Its named targets are
  blocked by ORTHOGONAL, non-1t bugs:
  - `valid_unknown_bits` fails on `cassert(ones==0xff)` where `ones = v|0xff`
    (v has unknown bits) — a **Dlop OR-with-unknowns bug in `../hlop`** (`?|1`
    not resolving to `1`); `v.[bits]==8` already works.
  - `typesystem` needs the 1v typename/structural/comptime-sticky cases; only
    its `int(max=3)` case is 1t (and now resolves in isolation).
  - A blanket Cluster-F mask regresses `valid_simple` (masks a deliberate 1-bit
    `0sb?` to the full u32 width) and `phase5_wrap_sticky` (conflicts with sat)
    — the documented prior-revert reason.
- **T5 — PARTIAL landed 2026-05-29 (green, 117/11 baseline, full build):**
  - **uN/sN → `prim_type_int` emission switch** (the T1-deferred part):
    `emit_type_expr` now emits `prim_type_int(max,min)` for `uN`/`sN`/`iN`
    (computed bounds) and `(nil,nil)` for unbounded `uint`/`int`. The two
    PROGRAMMATIC synthesizers were migrated too — the 1i inliner
    `emit_inline_typespec` (`upass_runner.cpp`) and ssa's io re-emit
    (`upass_ssa.cpp`) now build `prim_type_int`. ssa `type_info_from` +
    constprop `process_declare` derive bits/signed from the
    `prim_type_int(max,min)` range (fixed `fcall1`'s signed-output sext). So
    **`prim_type_uint`/`sint` are no longer emitted at runtime** by the
    producer, inliner, or any upass — only the legacy `.lnast` TEXT parser
    (`lnast_parser.cpp`, `#u`/`#s` tokens) still constructs them.
  - **lnastfmt store/declare shape validators** added (`pass_lnastfmt.cpp`):
    store child0=ref + ≥2 children + no type slot; declare child0=ref,
    child1=type node, child2=const(mode). Conservative (passes for all valid
    producer output; 0 regressions).
  - **5 legacy type nodes DELETED** from `lnast_nodes.def`:
    `prim_type_uint`, `prim_type_sint`, `prim_type_type`, `prim_type_ref`,
    `comp_type_mixin`. Removed their writer bodies + the dead consumer branches
    (attributes/constprop/ssa). The legacy `.lnast` TEXT format was migrated to
    `#int`: added a `ty_int` token (`lnast_tokens.def`) + parser case
    (`#int` / `#int(max[,min])`), remapped the legacy `#sint`/`#uint`/`#s`/`#u`
    → `prim_type_int` and `#type`/`#ref`→`none`, `#mixin`→`tuple`, and updated
    the `lnast/tests/ln/{types,tuples}.ln` round-trip fixtures. Full
    `bazel build //...` + `parser_writer_test` green.
  - **9 legacy type/decl nodes DELETED total** (all green, full `bazel build
    //...`, 244 pass across //inou //upass //lnast //pass): the 5 above plus
    **`unknown_type`** (folded into the `prim_type_none` sentinel — `none_type`
    was RENAMED to `prim_type_none`, unifying both) and **`type_def`** (`type
    Foo = …` now lowers to `declare(ref(Foo), prim_type_none, "type")` — type_def
    had no upass reader, was a write-only stub), plus (2026-05-30):
    **`expr_type`** — DELETED. A named type is now a `ref(NamedType)` in the
    declare/type_spec type slot; producer `emit_type_expr` emits `create_ref`,
    the lnastfmt declare validator accepts a `ref` in child1, and
    `emit_op_with_fold` was taught NOT to fold the type slot (a `ref` to a const
    bundle like `const a = …; const x:a = …` must stay a type, not fold to a's
    value). `is_type` upper bound moved to `comp_type_lambda`. **`tuple_set`** —
    DELETED. Statement-level field writes are now `store` (≥3 children → the old
    tuple_set path, ≤2 → scalar); producer/builder/parser emit `store` directly,
    `process_tuple_set` (the method) survives for the ≥3-child store branch,
    runner `C_OP(tuple_set)` + `is_tuple_attr`/`is_tuple_field_key`/lnastfmt
    `is_tuple_set` checks + both writers' `write_tuple_set` removed. Also RENAMED
    `prim_type_boolean`→`prim_type_bool`. Text-format fixtures migrated.
  - **declare-once validator — LANDED (2026-05-30).** A `const`-storage variable
    may be `declare`d only once per lexical scope (one `stmts` block). `mut`/`reg`
    redeclares are legal (reset the binding — `matrix.prp`'s `mut counter = 1`
    before each loop); nested scopes (if/for/while/func_def bodies) get a fresh
    frame so inner declares shadow (`scope_simple.prp`). Runs on the live tree
    (constprop pruned dead branches → only reachable redeclares seen; then-branch
    `const x` and else-branch `const x` live in distinct frames). Verified: `const
    x=1; const x=2` errors; `mut y=1; mut y=2` is allowed. 0 regressions.
  - **`assign` node DELETED (2026-05-30) — 10 legacy nodes gone total.** Every
    write/bind is now `store`: statement scalar writes, field-path writes,
    tuple-literal field payloads `(x=v)`, func_def/io signature params, and typed
    binds `name=value:type` — all `store`, disambiguated by PARENT context (only
    `store` STATEMENTS — direct `stmts` children — are arity-dispatched by the
    runner; payloads/signatures live inside `tuple_add` and are read positionally
    by their parent handler, so the binding↔write distinction is preserved
    structurally without a separate node). Migration: all `create_assign`→
    `create_store` (producer/parser/builder/func_extract/runner/ssa + ~20 unit
    tests); the redundant `rewrite_statement_writes_to_store` walk was deleted
    (everything emits `store` directly); consumers keyed on `is_assign`/
    `Lnast_ntype_assign` moved to `is_store` (constprop named-shape merge
    `is_type(assign)`→`store`, `is_tuple_field_key`, `emit_op_with_fold` field-key
    value-fold, lnastfmt `parent_writes_pos0`/field-entry/signature validators);
    the `is_primitive_op`/`is_unary_op`/ssa-`stmt_has_dest` enum-range anchors
    moved off `assign` to `dp_assign`/`store`; `write_assign` deleted from both
    writers and **`write_store` taught to emit the typed `name=value:type` form**
    (detected by an exactly-3-children store whose LAST child is a type node —
    the typed bind keeps value-in-middle, type-last, unlike a field-path store).
    Verified across the highest-risk consumers (lnast round-trip, prp_writer,
    constprop, fcall1/6, setter_multi_field, tuple_typename/type, forunroll,
    scope_simple/matrix); suite 244 pass / 10 fail (baseline +1), full build
    green. **T5 cleanup is COMPLETE.**
- **T4 wrap/sat-qualifier + Cluster-F — LANDED; target `valid_unknown_bits`
  GREEN (2026-05-30).** Narrowing reads the range at the write; the implicit
  unsigned coercion (`on_assign_like`: a negative signed literal written to an
  unsigned var, first write only, masks to width via `get_mask_value(bits)`)
  reinterprets `v:u8 = 0sb1?01_?000` to the unsigned 8-bit value. `valid_unknown_bits`
  10/10: the residual cassert `ones == 0sb1111_1111` was a TEST bug — `v|0xff` is
  the *value* 255 (u8, unsigned), and `==` compares values not bit patterns, so
  `0sb1111_1111` (signed −1) was wrong; corrected to the value-equivalent
  `0ub1111_1111`. The hard-error branch fires only for genuine width overflow
  without wrap/sat; a `:expect_fail:` harness flag (none yet) would exercise it.
- **`typesystem` test (5 unfoldable casserts) — task-1v SEMANTICS, NOT 1t
  structural.** The test (its own header cites "task 1v in TODO_prp.md") needs 5
  distinct named-type semantic features the 1t node-model redesign does not
  provide: (1) named-type default *borrowing* (`mut b:a_type = (x=3)` should
  inherit `a_type`'s `y=1` — today `b={x:3}`, `b.x==3` folds but `b.y==1` does
  not); (2) typename propagation (`.[typename]`); (3) `comptime` non-stickiness
  on copy; (4) recursive nested typenames; (5) complex-field `[bits]`-poison.
  Each touches constprop's bundle machinery (high regression risk to the green
  baseline — the prime constraint). Left for a 1v feature pass.

**Core result:** the three conflations the plan set out to remove are removed in
the producer + all consumers — (1) two write nodes → one `store` (T2); (2) the
scattered declaration cluster → one `declare` (T3); (3) types-as-attributes →
`prim_type_int` + derived bits/sign with a single-source `(max,min)` range (T1,
uN/sN-emission-switch deferred to T5). T6 (LGraph lowering) deferred. Nothing is
git-committed.

## Types

### Basic kinds (the only ones)
Scalar leaf types — the final `prim_type_*` set:
```
prim_type_int      // integer + (max,min) range; replaces prim_type_uint+sint
prim_type_bool     // separate kind, fixed envelope bits=1,min=-1,max=0
prim_type_string
prim_type_range    // a range value type (e.g. 1..=5) — KEEP
prim_type_none     // the single sentinel: "no/inferred type" AND "unresolved"
                   //   (unifies today's none_type + unknown_type)
```
Complex types (a tree of type nodes; leaves are the prim_types above):
```
comp_type_tuple   comp_type_array   comp_type_lambda
ref(NamedType)    // a named type is just a `ref` to its symbol-table bundle
```
- `integer` → one **`prim_type_int( [max], [min] )`**, two *optional* const children (absent ⇒ unbounded). Sign/bits derived; sign is **not** a kind.
- **Deleted:** `prim_type_type`, `prim_type_ref`, `expr_type`, `comp_type_mixin`, `unknown_type` (folded into `prim_type_none`), `prim_type_uint`/`sint` (T5).
- **`type` and `ref` are NOT types** — they move into the `mode` enum (below):
  - `type Foo = (…)` is a *declaration whose mode is `type`* (its type slot holds the structure) — replaces `prim_type_type` and `type_def`.
  - `ref` is a **function-parameter passing modifier** (pass-by-reference instead of by-value; a mutable `ref` param acts as an extra output). It is *not* mixed into the typesystem; it rides in `mode` as a placeholder, with the precise LNAST encoding deferred to a future task (likely modeled like `comptime`). Declaration and call sites must agree on `ref`.

### Integer envelope (the single source of truth)
The type's `(max,min)` is authoritative. Sizing is expressed ONLY through the
type itself — `uN`/`sN` sugar and the **type-call** `int(max=…, min=…, bits=…)`
form (parsed as `function_call_type`: callee = the type keyword, argument =
`(max=…, …)`). The **`:[max=N]`/`::[ubits=N]` attribute syntax for sizing is
dropped** — it complicated an alternative spelling with no benefit. `:[…]`
stays only for genuine (hardware/synthesis/user) attributes.

Legal (all normalize to one `prim_type_int(max,min)`; `bits=` is an input that
reconciles to the range; an explicit param refines the sugar's bounds):
| surface | max | min | note |
|---|---|---|---|
| `u8` / `s4` | 255 / 7 | 0 / -8 | width sugar |
| `uint` / `int` | ∞ / ∞ | 0 / ∞ | unbounded |
| `int(max=3)` | 3 | ∞ | only max pinned ⇒ bits nil |
| `uint(max=200)` | 200 | 0 | |
| `uint(bits=3)` | 7 | 0 | bits→(max,min) |
| `u12(min=100)` | 4095 | 100 | u12 bounds, min refined |
| `int(max=4, min=-20)` | 4 | -20 | bits derived = 6 (holds -20) |

**Producer work (T1):** today `int(max=3)` mis-lowers to a tuple literal
`(max=3)` — the producer must recognize a type-keyword callee
(`int`/`uint`/`integer`/`unsigned`/`uN`/`sN`) in a `function_call_type` and read
`max`/`min`/`bits` from the argument tuple, reconciling to `prim_type_int`.

**Derived, never stored** (computed on `.[…]` read / at lowering):
- `signed` = NOT (`min` known ∧ `min` ≥ 0)
- `bits`   = both bounds known ? (`signed` ? `1+max(clog2(max+1),clog2(-min))` : `clog2(max+1)`) : **nil**  (so `int(max=3)` ⇒ bits nil — `typesystem.prp` case 9)
- `max`/`min` reads return the stored bounds; aggregate `.[size]` = cardinality (not width)

`ubits`/`sbits`/stored-`bits` **cease to exist**.

## The four buckets (types ≠ attributes)

| bucket | what | where |
|---|---|---|
| **TYPE** | kind + integer range `(max,min)` | the `declare`/lambda-port type slot. `max`/`min` ARE the type — `:u8`, `int(max=3)`, `int(max=N,min=0)` (type-call) all feed the same two slots; never an `attr_set`/`:[…]`. |
| **MODE** | storage `mut\|const\|reg` + `comptime` | the `declare` mode slot. `comptime` is **non-sticky** — enforced at value level (a var's own declared mode), so `mut r = k` (k comptime) ⇒ r not comptime; the producer only writes `comptime` for an explicit keyword on *that* decl. |
| **OVERFLOW** | `wrap` \| `sat`, per single write | qualifier on the `store`/`declare` write. Narrowing reads the range from the type. NOT a sticky attribute. |
| **ATTRIBUTE** (`attr_set`/`attr_get`) | genuine hw/structural attrs only | `defer`, `valid`, `clock_pin`, `reset_pin`, memory `latency`/`rdport`/`wrport`; derived **reads** `.[bits]/.[sign]/.[max]/.[min]/.[typename]/.[size]` (computed, never stored); user `_debug`. |

## Attribute disposition (every key recognized today)

From `is_builtin_attr` (`upass_attributes_tuple.cpp:58`) + the `.[…]` reader
(`upass_attributes_read.cpp:412`). Categories: **DERIVED** (computed read, never
stored), **→TYPE/→MODE/→OVERFLOW** (stop being an attribute; move onto the
node), **ATTR** (genuine, stays `attr_set`/`attr_get`).

- **DERIVED reads (keep computing, no storage):** `bits`, `ubits`, `sbits`,
  `max`, `min`, `size`, `typename`, `key`, `comptime`, and **`sign`** (now
  exposed — like `bits`/`max`/`min`/`size`: a read computed from the type, not a
  stored attr). `foo.[sign]` = `min < 0` (true for signed integers; false for
  unsigned / non-negative). **An `attr_set` of any of these derived
  reads (`bits`/`ubits`/`sbits`/`max`/`min`/`size`/`sign`/`typename`) is a
  compile error** — they are read-only views of the type.
- **→TYPE (fold into the type node, delete as stored attrs):** `ubits`, `sbits`,
  `bits` (stored), `max`/`min` *when set at a declaration*, `typename`
  (becomes the named type's identity). `range` stays as the range *type*.
- **→MODE (fold into `declare` mode enum):** `type` (mut/const/reg/await),
  `mut`, `const`, `comptime`.
- **→OVERFLOW (per-write qualifier):** `wrap`, `saturate`, `sat`.
- **ATTR — stays:**
  - hardware/wiring: `clock`, `reset`, `clock_pin`, `reset_pin`, `negreset`,
    `posclk`, `async`, `initial`, `defer`, `valid`, `din`, `enable`, `stop`,
    `lat`, `num`, `addr`, `fwd`, `wensize`, `rdport`, `inputs`, `outputs`
  - synthesis hints: `critical`, `delay`, `donttouch`, `keep`, `inp_delay`,
    `out_delay`, `max_delay`, `min_delay`, `max_load`, `max_fanout`, `max_cap`,
    `left_of`, `right_of`, `top_of`, `bottom_of`, `align_with`
  - misc/meta: `loc`, `file`, `private`, `crand`, `rand`, `debug`/`_debug` +
    sticky `_*`; and Category-D user attrs (inherit aggregate→field).
- **→TYPE (memory/register):** the memory/flop config attrs
  (`rdport`/`wensize`/`lat`/`num`/`addr`/`fwd`/`clock_pin`/`reset_pin`/`negreset`/
  `posclk`/`enable`/`din`/`async`/`initial`/…) **fold into new `prim_type_memory`
  / `prim_type_register` type nodes** whose fields mirror the `graph/cell.hpp`
  `Memory`(41)/`Flop`(43)/`Latch`/`Fflop`(47) cell pins (just as `prim_type_int`
  carries `(max,min)`). The type node is the "goal end type after the
  memory/matrix is flattened"; pre-flatten some fields are defaults. (Later
  phase — not T1; pull the exact pin set from `cell.hpp` then.)

## The two nodes

### `store( var, level0 … levelN, value )` — every write/binding
Replaces `assign` (0 levels) **and** `tuple_set` (N levels). Also the value
payload of a **named/typed field** in a tuple literal is a 0-level store (a
positional field stays a bare value). No type slot — a `store` can never retype
(structural "type only at declaration"). One `process_store` branches on arity
internally (0 ⇒ scalar/wire path; ≥1 ⇒ tuple path, must flatten pre-LGraph) and
calls exactly one path.

### `declare( var, <type>, const(mode), [value] )` — every declaration
The universal "bind a name with optional type/mode/value" node — used at
statement level, as a **typed field payload inside a `tuple_add` literal**, and
as a **field entry inside `comp_type_tuple`** (a named type). Slots:
- `<type>`: a type subtree — `prim_type_int`(+range) / `prim_type_bool` /
  `prim_type_string` / `prim_type_range` / `comp_type_tuple` / `comp_type_array`
  / `comp_type_lambda` / **`ref(NamedType)`** (a named type resolves through its
  symbol-table bundle — no `expr_type`), or `prim_type_none` when inferred.
- `const(mode)`: a single const carrying a value from the **`Lnast_mode` enum**
  (one source of truth, not ad-hoc strings parsed everywhere):
  ```
  enum Lnast_mode { mut, const, reg, mut_comptime, const_comptime, type, ref }
  ```
  (open: do `reg`/`await`/`stage` get comptime variants — no, they're runtime;
  is `await`/`stage` in this enum or separate timing modifiers?) Consumers map
  the const token ↔ enum via one `Lnast_mode::from_sv`/`to_sv` (mirrors
  `Lnast_ntype`). `mode == type` makes this a type binding (replaces `type_def`);
  `mode == ref` marks a ref parameter (encoding TBD, future task).
- `value`: optional init (absent for bare `var x:u8`).
- Target is a bare `ref`; member-path declaration is a hard error. **Declare-once**: a second `declare` on a live var is a redeclaration error (validated post-constprop, accounting for dead-branch fold).

### `wrap`/`sat`
Per-write OVERFLOW qualifier on the `store`/`declare` write (option A) — never a
re-declare (which would collide with declare-once). Sticky declaration
`var:u4:[wrap]` folds the `wrap` token into the `declare` mode; per-statement
`wrap x = v` is the qualifier on that `store`. The old
`attr_set(x,"wrap")`-after-assign sticky path is deleted (it was the
`wrap_checks` fragility; the `wrap_to_signed` sext bug is already fixed).

## Worked examples (final, consistent)

`mut x:u8 = 255` →
```
declare
├── ref 'x'
├── prim_type_int
│   ├── const '255'   // max
│   └── const '0'     // min
├── const 'mut'
└── const '255'
```

`x = (a:u2=nil, b=nil)` →  (x already declared ⇒ a `store`; typed field = `declare`, untyped field = `declare` with `none`)
```
tuple_add
├── ref '___1'
├── declare
│   ├── ref 'a'
│   ├── prim_type_int   // max=3, min=0  (u2)
│   │   ├── const '3'
│   │   └── const '0'
│   ├── const 'mut'
│   └── const 'nil'
└── declare
    ├── ref 'b'
    ├── none
    ├── const 'mut'
    └── const 'nil'
store
├── ref 'x'
└── ref '___1'
```
A positional field (`(1, 2)`) stays a bare value in `tuple_add`.

Named type + use:
```
type Foo = (a:u3=1, mut b=nil)
const xx:Foo = nil
```
→ the `type` declaration binds `Foo` (mode `type`); its type slot is the
`comp_type_tuple`. The use references `Foo` by a plain `ref` in the type slot
(resolved via `Foo`'s symbol-table bundle — no `expr_type`):
```
declare                            declare
├── ref 'Foo'                      ├── ref 'xx'
├── comp_type_tuple                ├── ref 'Foo'        // named type = ref to its bundle
│   ├── declare                    ├── const 'const'    // Lnast_mode::const
│   │   ├── ref 'a'                └── const 'nil'
│   │   ├── prim_type_int (7,0)
│   │   ├── const 'const'
│   │   └── const '1'
│   └── declare
│       ├── ref 'b'
│       ├── prim_type_none
│       ├── const 'mut'
│       └── const 'nil'
└── const 'type'                   // Lnast_mode::type — Foo is a type binding
```

## Type-node simplification (`lnast_nodes.def`) — final

| node | action |
|---|---|
| `prim_type_int` | **add** (range children) — landed |
| `prim_type_uint`/`prim_type_sint` | aliases → int in T1; **delete** T5 |
| `prim_type_boolean` → `prim_type_bool` | rename (cosmetic) |
| `prim_type_string` | keep |
| `prim_type_range` | **keep** (range value type) |
| `none_type` + `unknown_type` | **unify → `prim_type_none`** (one sentinel) |
| `prim_type_type` | **delete** → `mode == type` |
| `prim_type_ref` | **delete** → `mode == ref` (ref is a param modifier, not a type) |
| `expr_type` | **delete** → named type = `ref(NamedType)` in the type slot |
| `comp_type_mixin` | **delete** (already FIXME) |
| `comp_type_tuple`/`array`/`lambda` | keep |
| `prim_type_memory` / `prim_type_register` | **add** (later phase) — fields mirror `cell.hpp` Memory/Flop pins; absorb the memory/flop config attrs |
| `type_def` | **delete → `declare` with `mode == type`** (confirmed) |
| `declare`/`store` | added (Phase A, inert) |

`mode` is the `Lnast_mode` enum (one canonical token + `from_sv`/`to_sv`), not
scattered strings.

## Migration phases (each keeps `//inou/prp:all` green — no regressions vs the current baseline; see Status)

- **T1 — `prim_type_int` + type-call sizing + single-source derivation.**
  (`prim_type_int` node + `.[sign]` read already landed — see Status.) Remaining:
  producer recognizes the type-call `int/uint/uN/sN(max=,min=,bits=)`
  (`function_call_type` callee = type keyword) and reconciles to
  `prim_type_int(max,min)`; extend `uPass_attributes::Type_info` to carry the
  `(max,min)` range (today it stores `kind`+`bits`); teach `process_type_spec` +
  `derive_bits/max/min` to consume `prim_type_int`; make `bits`/`sign` purely
  range-derived; switch `uN`/`sN` emission to `prim_type_int`.
  **Migrate the ~6 tests off the `:[…]`/`::[…]` size-attr syntax** to type-call:
  `wrap_complex` (`::[sbits=8]`→`:s8`, `::[ubits=8]`→`:u8`), `wrap_trivial`
  (`::[ubits=12]`→`:u12`), `tuple_simple_attr` (`::[bits=5]`→`:uint(bits=5)` —
  pick sign), `phase2_attr_max_min` + `phase8_size_attrs_partial`
  (`:int:[max=N,min=M]`→`:int(max=N,min=M)`). Stop the typesystem from treating
  `max/min/bits/ubits/sbits` `:[…]` attrs as type-pinning. Green.
- **T2 — `store`.** Producer: statement `assign`/`tuple_set` → `store`; runner
  `process_store` (arity branch); update the structural checks (DCE
  `dce_is_def_producing`, ssa `stmt_has_dest`, coalescer barriers,
  `lnast_manager::is_tuple_field_key`, `lnastfmt parent_writes_pos0`,
  prp_writer, `set_function_registry`). Tuple-literal field payloads → store.
  Green.
- **T3 — `declare`.** Producer: decl cluster → one `declare`; typed tuple fields
  → `declare` payloads; named types → `comp_type_tuple` of field declares.
  `process_declare` in attributes (type_info+mode+comptime), constprop (value +
  shape-merge over declare/store payloads), bitwidth (range). Stop emitting
  `attr_set(type/comptime/ubits/sbits)` + `tuple_get`-for-field-type. Update
  `is_tuple_field_key`/shape-merge to read `declare`/`store` payloads. DCE: a
  `declare` is always live. Slang (`create_declare_bits_stmts`) emits the type
  node, not `__ubits`. Green.
- **T4 — wrap/sat-as-qualifier + Cluster-F.** Narrowing reads the range at the
  write; out-of-range comptime write without wrap/sat = hard error. Targets
  `valid_unknown_bits`, `typesystem`.
- **T5 — validator + cleanup.** lnastfmt: store/declare shape, type-only-on-
  declare, declare-once. Delete `assign`/`tuple_set`/`prim_type_uint`/`sint`/
  `ubits`/`sbits`; retire the `attr_set`-as-type path; the throwaway `store`
  name can stay (it IS the write node now).
- **T6 — LGraph lowering** (deferred): derive sign/bits from the range at cell
  creation (already the `lnast2lgraph.md §7` contract); wrap→get_mask,
  sat→mux+get_mask; flatten N-level `store`.

## Reconciliation notes (other docs/code to update)
- `lnast_spec.md` §11.3 ("keep assign separate; no collapse") and §11.4 example
  lowerings are **superseded** by the `store` unification (user decision
  2026-05-29) — update when T2 lands. lnast2lgraph's "no tuple op survives"
  still holds: an N-level `store` is the thing that must flatten.
- `__ubits`/`__sbits` (slang via `create_declare_bits_stmts`) and bare
  `ubits`/`sbits` (prp) both go away in T3 (CHANGE-SOURCE). Until then the
  attributes read path keeps a legacy fallback.
- Skill guide's sticky `::[wrap]` form is per-write-qualifier under this model;
  grammar today only parses `wrap x = v` (per-statement) — `::[wrap]` on a
  declaration folds into mode (T4 / grammar follow-up).

## Settled (all confirmed 2026-05-29)
- **mode** = `Lnast_mode` enum `{mut, const, reg, mut_comptime, const_comptime,
  type, ref}` — single canonical token + `from_sv`/`to_sv`. NO `await`/`stage`.
- **`stage`/`await` are NOT typesystem** (await deprecated). `stage[1..=4]
  foo:T = fcall(…)` lowers to a NORMAL declaration (`const foo:T = …`) PLUS a
  separate timing check:
  ```
  range __r 1 4 ; get_time __res1 foo ; in __res2 __res1 __r
  cassert __res2 "foo is not within the 1..=4 range"
  ```
  (needs a `get_time` op — separate timing feature, out of scope here.)
- **type sizing** = type-call `int(max=,min=,bits=)` + `uN`/`sN` sugar; NO `:[…]`
  size attributes (tests migrated in T1).
- **`type_def` → `declare(mode==type)`**.
- **`.[sign]`** exposed as a derived read (`min < 0` ⇒ signed); `attr_set` of any
  derived read is a compile error.
- **memory/flop config → `prim_type_memory`/`prim_type_register`** (later phase),
  fields mirror `cell.hpp`.
- `tuple_simple_attr`'s `::[bits=5]` → `:uint(bits=5)` (value 2 ⇒ unsigned).
- `derive_bits` caching over wide types = perf-only, deferred.

Design is settled enough to implement T1.
