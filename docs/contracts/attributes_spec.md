# attributes_spec — Pyrope attribute handling in upass

Spec: `../docs/docs/pyrope/04b-attributes.md`.
Existing related design: `upass/upass.md` (Slice 5: `attr_set` side-map; Slice 6: tuple flattening).

**Status:** Phases 1–7 of §3 are landed in `upass/attributes/`
(`upass_attributes_sticky.cpp`, `upass_attributes_phase2.cpp` …
`upass_attributes_phase6.cpp`, `upass_attributes_handler.cpp`). This
document is the normative reference for the attribute pass; it is no
longer a work plan. Sub-items still open are tracked in
[../../TODO_prp.md](../../TODO_prp.md) (Pyrope-semantics work) and
[../../TODO_livehd.md](../../TODO_livehd.md) (infra refactors).

`inou/prp` emits attributes into the LNAST (`attr_set` / `attr_get` nodes).
The attribute pass consumes them inside upass and decides, for each
attribute, whether it is consumed in upass, lowered to an LGraph wiring
directive, passed through as a hint, or errored.

## 0. Goals

1. Produce an LNAST after upass where every LNAST-only attribute that can be
   resolved locally is resolved away. Surviving attributes fall into a small,
   documented set that LGraph generation knows how to handle or diagnose.
2. Enforce the language rules around attributes: declaration-site sets,
   duplicate-set consistency, `const` single-assignment, `comptime` resolved
   at end of upass, sticky propagation for `_*` attrs.
3. Treat *unknown* attribute names as pass-through to LGraph — they are the
   user-extensibility hook (e.g. `[poison]`); some custom compiler pass is
   responsible for them. The attribute pass does **not** error on unknown
   names.

## 1. Attribute categories

Every attribute is comptime in the sense that its **value** (or the
**ref/wire identity** it points to) must be resolvable at the end of upass.
What upass *does* with each attribute, however, varies:

### A. LNAST/upass attrs — consumed before or checked at LGraph generation

`max`, `min`, `ubits`, `sbits` (translate to `max`/`min`), `bits` (read-only,
derived), `wrap`, `saturate`/`sat`, `comptime`, `const`, `mut`, `typename`,
`private`, `size`, `key`, `crand`, `rand`, `loc`, `file`.

After upass: enforced/lowered where possible. Some attrs are LNAST-only and
must not reach LGraph generation except as a hard error if missed. Others
drive LGraph-generation behavior: value-dependent reads such as
`.[bits]`, `.[ubits]`, `.[sbits]`, `.[max]`, and `.[min]` are resolved by
the LNAST `pass.upass bitwidth` step against the HHDS `max` / `min`
attrs it publishes. Reads that cannot be resolved at LNAST time (e.g.
inputs that did not go through `pass.upass bitwidth`) fall back to
LGraph `attr_get` nodes for the legacy LGraph `pass/bitwidth`.
`wrap` / `saturate` resolution lives in `bitwidth` (see
`lnast_bitwidth.md`); `attributes` keeps only the policy bit and
presence reads. `rand` propagates through calls and has
LGraph-generation behavior.

### B. LGraph signal / wiring attrs — comptime-resolved connection semantics

**Partition-level vs per-decl.** Under the partition model
(`architecture.md §3`), `clock` and `reset` are declared at the
partition signature as typed ports (`clk:clock`, `rst:reset`), and
each partition has a single clock domain and reset for its body.
Per-declaration `clk::[clock]` / `rst::[reset]` markers remain valid
for declaring signals as clocks/resets at the language level, but the
partition's clock/reset identity is the load-bearing fact used by
cross-clock checks and LEC. Reg / memory `clock_pin` / `reset_pin`
override the default by referencing a port by name.

External memories declared in a partition's `ext:` list (see
`architecture.md §3.3`) carry their own pin attributes (`addr`,
`din`, `enable`, `rdport`, `wensize`, etc.) at the boundary; the LEC
pass uses the `ext:` declaration as the abstraction handle
(`architecture.md §5.2`).

Signal classification: `clock`, `reset` mark a declared wire/input as a
clock or reset signal, e.g. `clk::[clock]`.
Debug / sticky visibility: `debug` / `_debug` and any user `_*` sticky attr
propagate through upass and remain visible to LGraph generation.
Flop/register pins: `async`, `init`, `clock_pin`, `din`, `enable`,
`negreset`, `posclk`, `reset_pin`.
Latch pins: `din`, `enable`, `posclk`.
Fluid-flop pins: `valid`, `init`, `clock_pin`, `din`, `stop`, `reset_pin`.
Pipestage: `lat`, `num`.
Memory: `addr`, `bits` (entry width), `clock_pin`, `din`, `enable`, `fwd`,
`posclk`, LGraph object `type` (`0`: async, `1`: sync, `2`: array),
`wensize`, `size` (entry count), `rdport`.
`defer`: end-of-cycle binding on a wire or reg. `x.[defer]` reads or writes
the cycle-final value of `x` after all in-cycle assignments are resolved.
On a reg the deferred value drives `Flop.din` and becomes the next-cycle `q`
(the back-edge). On a plain wire it picks the last-assigned value, which is
how intentional rings are written; a combinational loop with no register is
otherwise a bug. Resolved at LGraph gen, not runtime.
Lambda introspection: `inputs`, `outputs`.

After upass: each attribute is resolved according to its own semantics.
Pin-like attrs such as `clock_pin` / `reset_pin` preserve a comptime-known
reference to the source wire so LGraph generation can connect that wire to
the associated register or memory LGraph pin. Signal classification attrs
such as `clock` / `reset` mark the source wire itself. Behavioral node attrs
such as `posclk` / posedge selection and other register/memory attrs are passed
to LGraph generation or lowered there, because they select how the LGraph node
is built. They may disappear as attributes after LGraph generation has emitted
the corresponding node/pin/mode.

### C. Synthesis hints — pass-through, may be ignored downstream

`critical`, `delay`, `donttouch`/`keep`, `inp_delay`, `out_delay`,
`max_delay`, `min_delay`, `max_load`, `max_fanout`, `max_cap`,
`left_of`, `right_of`, `top_of`, `bottom_of`, `align_with`.

After upass: preserved on the LGraph node; tools may ignore.

### D. Unknown / user-defined — pass-through, propagating

Anything not in the above lists. Survives to LGraph; some downstream pass
is expected to recognize and consume. Each new attribute requires a custom
compiler pass to handle it, either at LNAST/upass level or after LGraph
generation. From upass's point of view, unknown attrs are kept and generated
into LGraph if no upass consumed them. Typos in known attribute names are the
price of this policy — accepted. Unknown/user attrs are not restricted to
boolean values; they carry whatever comptime value the user assigned
(`true` for presence-only syntax, or an explicit integer/string/bool/etc.).

**Aggregate-to-field propagation.** When a category-D attribute is set on
a tuple aggregate, it propagates down to every field that does not
explicitly override it. This matters because Phase 4 flattens the tuple
away; without propagation the annotation would be lost before LGraph
sees it.

```
const t::[poison=42] = (a=1, b::[poison=99]=2, c=3)
//          aggregate                field-override
// After flattening: t.a → poison=42, t.b → poison=99, t.c → poison=42.
```

Category-A/B/C attributes do not use the generic category-D inheritance
rule. They follow their own per-attribute semantics instead. Examples:
`tup.[bits]` is the sum of the fields' bit widths, while `[debug]` on a
tuple marks every tuple field as debug.

**Where attrs come from.** In Pyrope source, attributes attach at the
type-declaration site (`mut a:u32:[max=100] = 0`, `reg r:[clock_pin=clk] = 0`,
tuple-literal field annotations, etc.). The variable cannot grow new
attributes later. LNAST decouples the declaration into separate `attr_set`
nodes, so the syntactic order of attr_set vs initial-value `assign` may
vary, but the underlying rule is fixed: a given `(target, attr)` pair is
set **exactly once**, and that single write must precede any read of the
target. The initial-value assignment may appear before or after the
attr_set in source order — order between attr_set and the initial
assignment is free; order between attr_set and any *read* is not.

Declaration vs. reassignment matters for attribute flow (see Phase 1):

- Declaration with direct-ref init (`const y = a`, `mut b = a`, `reg r = a`):
  only sticky `_*` / `debug` attrs propagate from RHS. Non-sticky attrs come
  exclusively from the LHS declaration; an LHS such as `const y::[foo=2] = a`
  sets `y.[foo]=2` regardless of `a.[foo]`.
- Plain reassignment (`c = b` where `c` is already declared): pure alias,
  `c` reads `b`'s entire attribute set — sticky and non-sticky alike. No
  new attributes can be introduced this way.

Aggregate-to-field inheritance does not count as a same-target duplicate:
an explicit field attr such as `b::[poison=99]` may override the
aggregate's inherited `poison=42` for that field.

### E. Deferred — not handled in initial pass

`valid` (generates hardware on use; future work). It is related to the
LGraph `Fflop` valid pin, but Pyrope-level `.[valid]` / `[valid]` behavior
is out of scope for the first attribute pass unless an already-explicit
Fflop lowering path needs to name that pin.
Debug-only temporal attrs `rising`, `falling`, `changed`, `timeout`
(only meaningful inside `assert`/`cover`/`waitfor`/`test`; handled by the
verification flow, not the generic attribute pass).

## 2. Aggregate vs. field reads on tuples

Most attributes attach to a single name and are field-local — when the
tuple expansion step rewrites `(a=1, b::[debug]=2)` into `tup.a` and
`tup.b`, the field's attributes ride along on `tup.b` automatically; no
migration step needed.

A handful of built-in attribute reads target the **aggregate** and must be
resolved *before* the tuple is flattened away:

- `tup.[size]` — number of fields (or array entries; 1 for a scalar).
- `tup.[bits]` — sum of field `.[bits]` (concatenation width).
- `tup.[typename]` — declared type name.
- `tup.[key]` — see `key_access.prp`; field-key introspection.

These reads are evaluated during step 3 (aggregate-attr resolution), before
step 4 (tuple expansion). They are aggregate reads with attribute-specific
meaning; they do not imply that the attribute value should be copied to
per-field reads.

**User / unknown aggregate attrs are different.** A category-D attribute
set on the aggregate (e.g. `t::[poison=42]`) propagates down to every
field that does not override it (see §1.D). This guarantees the
annotation reaches LGraph after Phase 4 flattens the tuple. The aggregate
read still returns the aggregate's own value:

```
const t::[poison=42] = (a=1, b::[poison=99]=2)
// t.[poison]   == 42     // aggregate
// t.a.[poison] == 42     // inherited from aggregate
// t.b.[poison] == 99     // field override wins
```

## 3. Pipeline (upass node-walk responsibilities)

This is not a separate fixed-point pipeline. The attribute pass participates
in the normal upass runner, like constprop and verifier: during a single
source-order node walk, each registered upass inspects the current node,
updates its side state, and asks the runner to emit, rewrite, consume, or
error. The numbered steps below are conceptual responsibilities and intended
ordering inside that walk, not independent whole-tree passes.

```
[ 0 ] LNAST in (from inou/prp / inou/slang)
[ 1 ] Sticky-attribute propagation         (NEW)
[ 2 ] Constant propagation                 (existing upass; extended to
                                            evaluate .[attr] reads inline)
[ 3 ] Aggregate tuple-attribute resolution (NEW; pre-flattening)
[ 4 ] Tuple expansion / array lowering     (existing Slice 6, extended)
[ 5 ] Consume/check LNAST/upass attrs (Cat. A) (NEW)
[ 6 ] Resolve LGraph-wiring attrs (Cat. B) (NEW)
[ 7 ] Pass through hints + unknown (C, D)  (NEW; trivial)
[ 8 ] LNAST out
```

For each attribute occurrence, the pass chooses one of three outcomes:
compile error if the attribute's check fails; consume/transform it if the
attribute has upass or LGraph-construction semantics; or leave it in the
emitted LNAST for a later pass/consumer. Attribute reads that constprop can
resolve during the current walk are folded at the use site; unresolved reads
remain unless the specific attribute requires a comptime error.

Attribute handling has two layers:

1. Generic propagation / storage rules that apply to every attribute name:
   declaration-site set syntax, presence-only `true`, explicit values including
   `false`, direct-ref aliasing, expression attr dropping, sticky control/data
   flow, duplicate-value checks, nil invalidation behavior.
2. A per-attribute-name handler/state machine that decides what that attribute
   means: whether `false` is inactive, whether the attr triggers compile-time
   checks, whether it is consumed/removed, whether it is left for a later pass,
   and what LGraph lowering snippet to emit if it survives.

Implementation shape: use an extensible base class / interface for attribute
handlers and a registry keyed by attribute name. Register built-in handlers for
specific names (`debug`, `clock_pin`, `bits`, etc.). When upass sees
`attr_set XXX` or `attr_get XXX`, dispatch to the handler registered for
`XXX`; unknown names use the default pass-through handler. Keep the file/module
split simple and handler-focused, e.g. `upass_attribute_bits.cpp` for
`bits` / `ubits` / `sbits` / `max` / `min`.

The handler interface has two dispatch modes:

1. **Per-name dispatch** for `on_attr_set(name, lhs, value)` and
   `on_attr_get(name, lhs)` — the registry routes to exactly one handler
   (exact-name → sticky `_*` pattern → default catch-all).
2. **Broadcast dispatch** for cross-cutting events that aren't tied to a
   specific attr name: `on_alias_assign(lhs, rhs)`, `on_expr_assign(lhs,
   rhs_refs)`, `on_if_arm_enter(cond_refs, cond_attr_reads)`,
   `on_if_arm_exit()`, `begin_iteration()`, `end_run()`. Every registered
   handler observes these so it can update its own per-name state (sticky
   buckets, `bits` cache, alias-attr migration checks, etc.).

The registry exposes `lookup(name)` for case 1 and `for_each_handler(fn)` for
case 2. Adding a new attribute is one new `Attribute_handler` subclass and
one registration line in `uPass_attributes`'s constructor.

---

### Phase 1 — Sticky-attribute propagation

**Goal.** Resolve `debug` / `_debug` and any user `_*` attribute as a sticky
property that flows through data and control dependencies. `[debug]` behaves
the same as `[_debug]` for propagation. If a sticky value can affect another
value `Z` in any way, then `Z` inherits that sticky attr. This is the
mechanism that lets debug-only statements prove they cannot influence
synthesis-visible state, and lets user sticky attrs express similar security /
taint checks.

**Rules.**
- RHS sticky → LHS sticky on `assign` / compound expressions.
- Direct-ref binding has two distinct cases:
  - **Declaration with direct-ref initializer** (`const y = a`, `mut b = a`,
    `reg r = a`, …). Only **sticky** `_*` / `debug` attrs propagate from
    `rhs` to `lhs`. Non-sticky attrs do **not** propagate — the LHS is a
    new declaration and its attribute set is defined exclusively by its
    own declaration syntax (e.g. `const y::[foo=2] = a` sets only the
    attrs the LHS lists).
  - **Plain reassignment** to an already-declared name (`c = b`). This is
    a pure alias: `c` becomes the same wire as `b`, so **all** attrs —
    sticky and non-sticky — read through the alias as `b`'s. No new
    attribute is introduced; reassignment cannot grow an attribute set.
- Expression results, even identity-looking expressions such as `rhs + 0`,
  drop non-sticky attributes and propagate only sticky attributes.
- Explicit destination annotations attach to the result variable, even when
  the RHS is an expression: `const y::[foo] = a + b` marks `foo` on `y`.
  If LNAST/upass does not consume that attr, LGraph generation emits an
  attr_set after the addition/result node for `y` with `foo` as the attr input.
- For a `mut` variable carrying an unconsumed attr, LGraph generation emits an
  attr_set after each assignment to that variable. Example: `mut z = a`
  followed by `z = 300` yields two LGraph attr_set placements for `z` if the
  attr remains unhandled by LNAST/upass. This is not an attr copy between
  wires; it annotates each assignment/result for the same logical wire/value.
- For a `const` variable carrying an unconsumed attr, LGraph generation emits
  one attr_set after the single real assignment/result.
- Assignment to `nil` is an invalidation for any storage kind (`const`, `mut`,
  etc.). It does not count as a real assignment/result for attr placement; do
  not emit attr_set after nil invalidations. Invalidation does not remove
  attrs already attached to the variable/wire; `x.[foo]` still reads `foo` if
  it was set. Derived attrs define their own nil behavior: `x.[bits]` returns
  `0` when the current value is invalidated/nil, while `x.[typename]` is based
  on the declared type and is unchanged by nil/value assignment.
- Condition sticky → assignments controlled by that condition become sticky,
  even when the assigned RHS is otherwise clean. Nested conditions OR their
  sticky contexts. Attribute reads can create this context when the read attr
  is sticky: `if a.[_foo] { z = clean }` makes `z.[_foo]` true, and
  `if a.[debug] { z = clean }` makes `z.[debug]` true. A condition that only
  reads a non-sticky attr such as `a.[foo]` does not make `z.[foo]` sticky.
  Likewise, a condition variable carrying a non-sticky attr does not copy that
  attr to controlled assignments; only sticky attrs create control taint.
- At `if`/`else` joins: OR-merge (any sticky branch → result sticky). Phase
  2 may have already folded comptime conditions, so only the live branch of a
  comptime-folded `if` contributes values and sticky control context. Runtime
  joins contribute multi-branch OR merges.
- Sticky attrs are monotonic on a variable: once set or inherited, they cannot
  be cleared by a later clean assignment. Later assignments can only add more
  sticky attrs. A sticky attr acquired later takes effect from that point
  forward; it does not retroactively taint earlier expression results. Direct
  ref aliases are the same wire/value, so later sticky attrs on that wire are
  visible through all aliases.
- Per sticky attribute name independently — `debug` / `_debug` use the same
  sticky bucket, while each user `_*` attr such as `_foo` is tracked
  separately.
- Runs on original (pre-flattening) names.

**Storage model.**
Sticky attrs are monotonic *declaration-visible* state, not per-SSA-version
state. SSA/value dependencies still matter for discovering new sticky flow
through expressions, conditions, and joins, but the destination variable
keeps every sticky attr it has ever received.

The per-variable acquired-bucket map therefore survives across runner
iterations; only the per-arm control-taint stack resets, since arm
entry/exit are re-walked each sweep.

**Coverage.** Direct assignment, expression-drops-non-sticky, control
dependence, comptime-folded `if` (live branch wins), debug/`_debug`
equivalence, per-attr-name independence are covered. Gaps: runtime
`if`/`else` OR-merge of sticky on a `comb` body, and runtime sticky
control taint where the condition itself carries a sticky attr.

---

### Phase 2 — Constant propagation with attribute-read evaluation

**Goal.** Existing constprop must evaluate `.[attr]` reads inline so that
attribute-conditional control flow folds correctly. Extends `upass.md`'s
Slice 5/7 work.

**Rules.**
- `.[attr]` read on a known decl pulls from the attr side-map (or from
  derived sources for `bits`/`size`/`typename`).
- Presence-only set syntax (`x::[foo]`) stores boolean `true`; `x.[foo]`
  returns `true`.
- Explicit values are present attrs even when the value is boolean `false`.
  This applies generally: `x::[debug=false]`, `x::[clock=false]`, or
  `x::[poison=false]` make `x.[attr] != nil` true and `x.[attr]` evaluate
  false. User attrs such as `poison` are not Pyrope-specific and may also hold
  non-boolean values. For boolean built-ins, active behavior generally requires
  the attr to be set and true; set-but-false is present but inactive unless the
  per-attribute handler intentionally gives false some other meaning.
- Unset ordinary attribute → `nil`. Comparisons against `nil` inside
  `.[…]` reads are exempt from "attribute must be defined". Using `nil` as a
  value in an operation/check is a compile error (`cassert nil` is an error,
  not the same as `cassert false`).
- Built-in attributes with special derived semantics may return a value even
  when never explicitly set. Example: `bits` is present for all types and is
  derived; `size` / `typename` / `key` have their documented aggregate/type
  semantics.
- `.[bits]` / `.[ubits]` / `.[sbits]` / `.[max]` / `.[min]` are
  resolved by `pass.upass bitwidth` against the HHDS `max` / `min`
  attrs it publishes; `bits` / `signed` derive from `max` / `min`.
  For runtime/unknown values that LNAST cannot bound (e.g.
  Verilog-ingress inputs), preserve/lower the read for the legacy
  LGraph `pass/bitwidth` fallback. For an invalidated/nil value,
  these attrs return `0`.
- `.[size]` is field count (1 for scalar).
- Constprop evaluates attribute reads opportunistically during the normal
  upass node walk. There is no attribute-specific fixed-point loop.

**Open issues.**
- `comptime` attribute itself: `x.[comptime]` is a *post-hoc* check —
  evaluable only after constprop has classified `x`. Order of evaluation
  inside a pass matters.

**Coverage.** `.[size]`, `.[typename]`, `.[key]`, `.[max]`/`.[min]` for
all declaration forms (`u8`/`s8`/explicit/`range`), `.[bits]`/`.[ubits]`
per assignment, `.[comptime]` across storage classes, and `nil` escape
hatch for unset attributes.

---

### Phase 3 — Aggregate tuple-attribute resolution

**Goal.** Resolve attribute reads whose target is the *whole tuple* before
phase 4 erases the tuple shape.

**Scope.** `tup.[size]`, `tup.[bits]`, `tup.[typename]`, `tup.[key]`, built-in
aggregate-set attrs with specific propagation semantics (e.g. `[debug]`),
and the aggregate tuple's own user-set attrs (e.g.
`mut foo::[potato=4] = (...)`, where `foo.[potato]` reads from the aggregate
and fields inherit it unless they override it).

**Rules.**
- `tup.[bits]` = `sum(field.[bits])`.
- `tup.[size]` = field count (or array length).
- Built-in Pyrope attrs use their own semantics. For example,
  `tup.[bits]` is a sum over fields, and aggregate `[debug]` marks every
  field as debug.
- Unknown/user attrs (e.g. `[potato]`, `[poison]`) inherit from the tuple
  aggregate to each field unless a field explicitly overrides the attr.
  See `tuple_simple_attr.prp` lines 16–17.

**Coverage.** Aggregate `tup.[bits]` (sum-of-fields), `tup.[size]`,
`tup.[typename]`, `tup.[key]`, aggregate-to-field cat-D propagation
with field-level overrides winning, and confirmation that built-in
aggregate reads use their dedicated semantics rather than generic
inheritance.

---

### Phase 4 — Tuple expansion / array lowering

**Goal.** Lower LNAST tuple/array shapes to LGraph-compatible shapes.
Builds on `upass.md` Slice 6.

**Lowering rules** (per chat agreement):
1. Explicit LNAST array → LGraph Array.
2. Named tuple, mixed types → split each field into its own variable
   (`tup.a`, `tup.b`).
3. Unnamed tuple, all fields same type → fold into LGraph Array.
4. Unnamed tuple, mixed types → split as separate variables.
5. Array of tuple (mixed-field types) → split fields first; each field
   becomes its own (possibly multi-D) variable.
6. Multi-D arrays → flatten to 1-D using a comptime index equation
   (`i*N + j`, etc.) since LGraph is 1-D only.

**User-attr migration.** When fields are split out, any category-D
aggregate attribute that was not overridden at the field site is copied
onto the field's flattened name (so LGraph still sees it). Field-level
overrides are preserved verbatim. Category A/B/C attrs follow their own
per-attribute migration rules.

**Open issues.**
- Multi-D flatten with non-power-of-two inner dim emits a runtime
  multiply at every dynamic-index site. Functionally correct, perf
  footnote.
- Whole-element assignment `buf[i] = (a=…, b=…)` after split must
  expand into per-field stores (`buf_a[i] = …; buf_b[i] = …`).
- Dynamic indexing into a split-AoT works mechanically (`buf[run_i].a`
  → `buf_a[run_i]`) since each field is its own array.
- Pass-by-reference for attributes (e.g. `reset_pin`) interacts with
  procedure boundaries — confirm refs survive expansion intact.

**Coverage.** General tuple flattening, uniform unnamed tuple SoA lowering,
array-of-named-tuples surviving as per-field arrays, 2-D flatten via
1-D index, and alias migration (`const u = t`, `const v = t.a`).

**Behavior.** Aggregate cat-D attrs are materialized onto each field's
flattened path at tuple_add and at shape-acquiring aliases, with
per-field overrides winning. Direct-ref aliases propagate `tuple_get`
chains and direct attrs from RHS to LHS. Structural lowering
(mixed-type split, multi-D flatten) lives in constprop / Slice 6 — not
in the attribute pass.

---

### Phase 5 — Consume/check LNAST/upass attrs (category A)

**Goal.** Consume/check category-A attrs where LNAST/upass is responsible, and
mark/lower attrs that intentionally survive to LGraph generation.

**Per-attribute work.**
- `mut` / `const` / `comptime`: declaration markers. Validate:
  - `const`: at most one non-nil assignment.
  - `comptime`: value resolved at end of upass; else error.
  - `mut`: presence-only; multi-assign allowed.
  - Names starting with uppercase: implicitly `comptime const`.
- `bits` / `ubits` / `sbits` / `max` / `min` (and `range` sugar): owned
  by `pass.upass bitwidth` (see `lnast_bitwidth.md`). `attributes`
  forwards explicit constraints to `bitwidth`, which validates
  `max >= min`, `max >= last_assigned_value` (or wrap/sat handles
  overflow), and publishes HHDS `max` / `min` attrs. Reads are
  value-derived: nil/invalidated value → `0`; known value `1233` →
  `max == min == 1233` and `bits` / `ubits` / `sbits` derived from
  `max` / `min`. When LNAST cannot bound the value (e.g.
  Verilog-ingress inputs), the read is preserved/lowered as
  `attr_get` for the legacy LGraph `pass/bitwidth` fallback.
- `wrap` / `sat` / `saturate`: split between two passes once
  `pass.upass bitwidth` lands (see `lnast_bitwidth.md`).
  - `attributes` owns **policy recording and presence reads**.
    Declaration attrs (`[wrap]`, `[saturate]`) mark the variable as
    using the named narrowing operation on every assignment. Reads
    like `w.[wrap]` / `w.[saturate]` return boolean `true` when set
    (or `false` for an explicit false value). Explicit false follows
    the general attr-value rule: `w::[wrap=false]` behaves like no
    wrap policy for assignment narrowing, while `w.[wrap] != nil`
    still reports that the attr was explicitly present and
    `w.[wrap]` evaluates false.
  - `bitwidth` owns the **narrowing math**. It reads the policy bit
    from `attributes` and applies wrap / saturate to constants and
    to inferred ranges using the variable's `max` / `min`.
    Statement-level `wrap` / `sat` prefixes apply the same operation
    only to that one result assignment; the attr does not stick on
    the result. If widths/ranges are comptime-known, `bitwidth`
    folds/lowers to slice/clamp logic directly. If not time-constant,
    LGraph generation emits the required mask/arithmetic gates;
    that logic uses `max` / `min` from LNAST HHDS attrs.
  - Prior to `pass.upass bitwidth`, the narrowing math lived
    end-to-end in `upass/attributes/upass_attributes_phase5.cpp`.
    That code moves to the new pass at the cutover.
- `typename`: Pyrope type metadata. Pyrope basic / named types are exposed
  through `.[typename]`; for example `foo:XX` means `foo.[typename] == "XX"`.
  Validate type/typename is invariant across all assignments (even for `mut`).
  Do not confuse this with LGraph object `type` pins, such as Memory
  `type = 0/1/2` for async/sync/array category.
- `private`: tuple-field visibility; consumed during tuple expansion
  (phase 4) by emitting access checks.
- LNAST-only attrs: `comptime`, `const`, `mut`, `private`, `typename`, `key`,
  `loc`, `file`, and `crand` must be consumed/checked by LNAST/upass. If any
  of these are still pending when LNAST-to-LGraph translation sees them, LGraph
  generation reports an error.
- `debug` / `_debug`: mark variable and propagate as the same sticky attr.
  Upass does not reject non-debug code that reads debug-tainted values. It
  preserves the propagated debug attribute through to LGraph generation, where
  synthesis/debug legality and elision decisions are made.
- `size`: read-only; resolved in phase 2/3.
- `key`, `loc`, `file`: consumed with their LNAST-only semantics, expanding to
  literal values or compile-time checks as appropriate.
- `crand`: compile-time random counterpart to `rand`. LNAST/upass should error
  if the target is not boolean or integer, use the target `min`/`max` range,
  and fold the read to a constant. Declaration `[crand]` can be viewed as
  random initialization fixed for the compile cycle. Each `crand` instance/read
  may produce a different compile-time constant. Since LNAST may not know the
  final LGraph-computed `max`/`min`, use the LNAST-known type/range information
  available at that point; when final `max`/`min` are unavailable, use the
  target variable's declared/inferred integer type bits to bound generation
  (plain integer defaults follow the variable type, likely signed).
- `rand`: not LNAST-only. It must propagate through function calls with
  dedicated logic and is handled during LGraph generation. A read `x.[rand]`
  generates a runtime random value valid within `x.[min]` and `x.[max]`.
  LNAST/upass should error if `x` is not boolean or integer. LGraph generation
  emits the runtime random-number logic using the target range. Declaration
  `[rand]` can be viewed as runtime random initialization; `x.[rand]` is random
  per cycle, while `x.[crand]` is random per compile cycle. `x.[rand]` is an
  effectful runtime operation: LNAST must not fold, remove, or common it away;
  LGraph generation should preserve/lower each read into runtime random-call
  logic.
- `deprecated` / `warn`: unclear semantics; remove from the active planned
  attribute list for now rather than implementing placeholder behavior. Also
  remove them from the upstream Pyrope attributes documentation in a follow-up
  doc cleanup.
- Sticky user `_*`: propagated by Phase 1 and kept visible to LGraph
  generation rather than consumed here unless an explicit upass handles the
  attr.

**Coverage.** Wrap/sat declaration vs statement-level forms, sticky
`_*`/`debug`, `[crand]` folding, `[ubits]` on `const`, `const`
single-assign patterns. Gaps: `comptime` on a non-resolvable value
(needs harness `:expect_fail:` flag), `mut` with declared `typename`
and a type-changing reassignment.

**Behavior.** Wrap/sat distinguishes the declaration form (attr_set
fires *before* any assign on the target) from the statement form
(attr_set fires *after* a real assign). The declaration form persists
the policy and narrows each subsequent assignment; the statement form
narrows once and clears any stored attr value so a follow-up `.[wrap]`
/ `.[saturate]` read returns `false`. Narrowed values publish through
the runner's fold side-channel so constprop sees them ahead of any
raw-assigned value. The const handler tracks per-target assignment
count and raises a compile error on a second non-nil bind. Negative-
test enforcement is currently blocked on the test harness lacking a
pipeline-exit-failure assertion flag.

---

### Phase 6 — Resolve LGraph-wiring attrs (category B)

**Goal.** Resolve attribute targets (which wire, which port, which reg
field) at compile time and apply each attribute's connection semantics for
LGraph generation. Category-B attributes are not a single generic pass-through
class.

**Rules / semantics.**
- `clock_pin` / `reset_pin`: only meaningful for registers and memories.
  Runtime pin connections use `ref xxx`; LGraph generation connects `xxx` to
  the associated register or memory clock/reset pin. Preserve the reference
  identity, not the current value of `xxx`.
  Example: `reg xx:u17:[clock_pin = ref clk2] = 3`.
  `clock_pin` does not require `clk2.[clock] == true`, but it must reject a
  target explicitly marked `clk2.[clock] == false`, and it must reject a
  comptime target/value because the clock pin needs a runtime wire.
  `clock_pin=false` is not a disable form. `reset_pin` follows the same ref
  connection rule for runtime reset wires, but `reset_pin=false` is valid and
  disables the reset/default reset connection.
  For memories, `clock_pin` can be one ref shared by all ports or a tuple of
  refs keyed by port.
- `clock` / `reset`: set on a wire/input declaration to classify that signal,
  e.g. `clk::[clock]` or `rst::[reset]`. These are not aliases for
  `clock_pin` / `reset_pin` and they do not drive default-pin selection on
  a reg/memory — that defaulting is name-based (see lnast2lgraph.md §12).
  Explicit false is a present but negative classification:
  `clk::[clock=false]` means this is not a clock signal.
- `posclk`: passed to LGraph generation for flop/latch/memory edge selection.
  The same rule applies to other register/memory behavioral attrs such as
  memory timing/mode attrs: preserve/lower them at LGraph generation rather
  than treating them as local validation-only metadata. Mode/edge-selection
  values such as `posclk` must be comptime-known; the design cannot switch
  between positive and negative edge behavior at runtime.
- Structural register/memory attrs such as `sync` / `async`, `negreset`,
  memory LGraph object `type`, `wensize`, `rdport`, and `fwd` are also
  comptime-known requirements because they change node mode, pin structure,
  port classification, or generated logic.
- Runtime port attrs such as memory/register `din`, `enable`, and memory `addr`
  accept either `ref xx` where `xx` is a runtime wire/value, or a comptime
  constant/value `zz`. Runtime non-ref expressions should be materialized to a
  wire before being used as the attr value.
- `defer`: never passes down as an attribute. It is consumed as an
  end-of-cycle wiring rule. `x.[defer] = rhs` overrides the cycle-final
  value of `x`; `x.[defer]` on RHS reads that cycle-final value. For regs
  the deferred value drives `Flop.din` and becomes the next-cycle `q`. For
  plain wires the deferred value is the wire's settled end-of-cycle value,
  which is how intentional rings are written; a combinational loop with no
  register in it is otherwise a bug.
- LGraph pin names are the compatibility contract:

  | LGraph op | Attribute / pin names |
  | --- | --- |
  | `Memory` | `addr`, `bits`, `clock_pin`, `din`, `enable`, `fwd`, `posclk`, LGraph object `type`, `wensize`, `size`, `rdport` |
  | `Flop` | Pyrope `init` maps to LGraph pin `initial`; `async`, `clock_pin`, `din`, `enable`, `negreset`, `posclk`, `reset_pin` keep the same spelling |
  | `Latch` | `din`, `enable`, `posclk` |
  | `Fflop` | Pyrope `init` maps to LGraph pin `initial`; `valid`, `clock_pin`, `din`, `stop`, `reset_pin` keep the same spelling (`valid` is listed for LGraph compatibility; Pyrope-level valid semantics are deferred) |
- Other category-B attrs need per-attribute lowering rules rather than a shared
  "preserve attr_set" representation.

**Open issues.**
- Memory port attrs (`addr`, `din`, `enable`, `wensize`, `rdport`):
  encode the port-tuple semantics.
- Pipestage `lat`/`num`: TBD.

**Coverage gaps.** Register with non-default `clock_pin`/`reset_pin`
round-trip through upass; counter using `preg.[defer]` on RHS; memory
with per-port `clock_pin` tuple.

**Behavior.** Cat-B handlers are scaffolds today: the shared attr_set
path records ref values and constants into a side-map that LGraph
generation reads, and the per-attribute handler slots are intentionally
light. Future per-attribute validation (e.g. `clock_pin` requiring a
runtime ref, mode attrs requiring comptime values) plugs into the
existing dispatch surface without touching the registry.

---

### Phase 7 — Pass through hints and unknown attrs (categories C, D)

**Goal.** Trivial. Any attribute not consumed by an upass handler survives the
pass unmodified; LGraph generation or a later pass interprets it.

**Coverage.** Custom user attrs (`[poison]`, `[label]`, multi-attr
`[count, tag]`, `[donttouch]`) round-trip via comptime read. Gap:
end-to-end LGraph smoke test (can't be cassert-checked at comptime).

**Behavior.** Dispatch order is exact-name → sticky `_*` pattern →
default no-op. The shared attr_set path records every value into the
side-map regardless of which handler matched, so cat-D attrs round-trip
without per-attribute logic.

## 4. Cross-cutting issues

- **Single node-walk semantics.** Attribute handling follows the existing
  upass model: transformations are made while walking nodes in source order,
  alongside constprop/verifier decisions. There is no separate attribute
  convergence loop; if an attribute requires a value/check that cannot be
  resolved when required, that attribute's semantics decide whether to emit a
  compile error or leave the node for a later consumer.
- **Iteration model — single walk.** The runner does one tree walk per
  `pass.upass` call; there is no whole-tree or per-node fixed-point loop.
  When a handler folds an attr read to a known constant (e.g. `.[bits]`,
  `.[size]`), it surfaces the value through the runner's fold side-channel
  so constprop/verifier pick it up at the use site during the same walk.
  A handler may still call `mark_changed` for diagnostics, but nothing
  re-walks — values an *earlier* node depended on are resolved by source
  order, not by re-sweeping.
- **Attribute syntax cleanup.** `::[...]` is only the declaration-site set
  form after `mut`, `const`, `reg`, tuple field introduction, etc. Attribute
  reads are always `a.[attr]`. Old tests that used `a::[attr]` as an
  expression read should be fixed; the attribute pass should not preserve
  compatibility with that old syntax.
- **Pass-by-reference semantics.** The doc states "attributes are passed
  by reference" so that, e.g., `reset_pin` connects to the *wire*, not a
  copy of its current value. Phase 6's emission must preserve refs, not
  inline values, when the attribute target is a wire.
- **Local/global upass orchestration.** Attribute propagation across function
  inputs/outputs uses the same machinery as local propagation. A practical
  pipeline can have two upass scopes: a local per-file pass, runnable in
  parallel, that simplifies LNAST, expands loops, and handles locally declared
  functions; and a later global pass that starts from the top and can propagate
  through imported/associated modules and functions. Attribute handlers should
  not need special local-vs-global logic beyond marking/preserving attrs and
  applying the normal call/return binding rules. During LNAST-to-LGraph
  translation, if an attribute that only LNAST/upass should have translated
  (`comptime`, `const`, `mut`, `private`, `typename`, `key`, `loc`, `file`,
  `crand`) is still pending, LGraph generation reports the error; this may be
  the first point where the missing translation is knowable.

## 5. Deliverable layout (proposed)

A new `upass/attributes/` micro-pass module with phases 1, 3, 5, 6, 7
implemented as in-order steps within the upass runner. Phase 2 stays in
`upass/constprop/` (extended to evaluate `.[attr]` reads). Phase 4 stays
in `upass/` Slice 6 territory (extended with the lowering rules above).
