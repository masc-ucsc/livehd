# LNAST TODO

Consolidated punch-list for the LNAST layer: producers (`inou/prp`,
`inou/slang`, `inou/pyrope`), consumers (`pass/lnast_tolg`, `pass/lnastfmt`,
`pass/lnastopt`, `upass/*`), and alignment with the new Pyrope defined under
`../tree-sitter-pyrope` and `../docs/docs/pyrope`.

Generated from a survey of: `lnast/lnast_nodes.def`, `lnast/lnast_create.cpp`,
`lnast/lnast.cpp`, `lnast/lnast_writer.cpp`, `lnast/tests/ln/*.ln`,
`inou/prp/prp2lnast.cpp`, `inou/pyrope/prp_lnast.cpp`,
`inou/slang/slang_tree.cpp`, `pass/lnast_tolg/*`, `pass/lnastfmt/*`,
`pass/lnastopt/*`, `upass/*`, `docs/docs/pyrope/12-lnast.md`,
`docs/docs/pyrope/14a-small_pyrope.md`, `tree-sitter-pyrope/grammar.js`.

---

## Pending items summary

| Tier | Item                                                                    | Why deferred |
|------|-------------------------------------------------------------------------|--------------|
| 1    | §5 / §8.1 Golden-output harness                                         | Snapshot three-producer baselines so future migrations can diff against them. |
| 1    | §2 Doc ↔ `lnast_nodes.def` name drift                                   | Docs live in `../docs/docs/pyrope/`, outside this repo. |
| 2    | §13 full — `Tree_index`-identity tmps (no string)                       | Partial done: `Lnast::is_tmp` is the single canonical predicate, wrappers delegate to it. Remaining: a `Tree_index`-keyed tmp API on `Lnast_node` so consumers don't need any name at all. |
| 2    | §15.2 ext — offset=0 (reg Q) / offset=-1 (past cycle) / ref offsets     | `process_ast_delay_assign_op` currently only lowers offset=1. Other offsets error out rather than doing useful work. |
| 3    | §10 Symbol-table API + value-attr inference                             | Multi-day effort; needs golden harness as safety net. |
| 3    | §11 Unify `attr_set` / `tuple_set` shape; collapse `assign`             | Touches every producer and every consumer. Gated on §10. |
| 3    | §12 `$` / `%` / `#` → ST-backed direction/storage                       | Gated on §10 and §13 full. |
| 4    | §4.1 / §4.5 `__ubits` / `__sbits` / other `__` attrs → bare names       | Blocked by §10/§11 (need ST to store non-stringly attrs). |

---

## Cross-cutting pending issues

1. **`delay_assign` offset semantics are partially undefined.** Current
   lowering at `pass/lnast_tolg/lnast_tolg.cpp process_ast_delay_assign_op`
   only handles offset=1 on a wire (identity/placeholder). The full §15.2
   spec needs: offset=0 on a reg (Q pin), offset=-1 on a reg (past cycle),
   comptime-ref offsets (e.g., `xx = 3; a@[xx]`), and validation that
   non-1 offsets on wires / 0-offset on wires are rejected at lnastfmt.

2. **`assign` SSA pass injects a synthetic `err_var = 0b?`** at
   `lnast/lnast.cpp:37-42`, unconditionally prepending it to every
   top-level `stmts`. Also used as the phi-table sentinel for branches
   where a var is defined on one side only (see `undefined_var_nid` refs).
   The consumer-side guards (e.g., `lnast_tolg.cpp:92-95`) explicitly skip
   `err_var_undefined`. Non-trivial to relocate because of the
   phi-placeholder role; would need to replace the phi-side refs with a
   dedicated `create_undefined()` ref node or similar before the top-level
   emission can be removed.

3. **`Lnast::is_register` / `is_input` / `is_output` live on `Lnast`,
   not `Lnast_node`**, and they test `front()` on a `string_view`. After
   §12 these should vanish entirely. Direct `front()` / `[0] ==` checks
   in `pass/lnast_fromlg`, `inou/code_gen`, `pass/semantic`,
   `upass/constprop`, `lgraph/lgtuple`, `lnast/bundle` remain — they go
   away entirely under §12.

4. **`_._L<n>` prefix still emitted by `pass_lnast_fromlg`.**
   `pass/lnast_fromlg/pass_lnast_fromlg.cpp:82` emits `_._L<n>` prefixes
   on the LG→LNAST reverse path. Compiler tests don't hit a path that
   feeds `pass_lnast_fromlg` output back into `Pass_lnastfmt`/`Code_gen`,
   so no regression today; if that round-trip is ever exercised, those
   vars will be misclassified as non-tmps. Fix owner: whoever revives
   `lgtoln_verif_*` tests.

5. **`lnast_nodes.def` has six `FIXME: remove`s** (`uif`, `dp_assign`,
   `mut`, `phi`, `hot_phi`, plus `comp_type_mixin`). Under §11 collapse,
   `mut` should be deleted; `dp_assign` is explicitly deferred per §11.3.
   `uif` / `phi` / `hot_phi` need owners.

6. **`attr_set` / `attr_get` are variadic in code** (they accept a path
   plus a terminal key/value), but the grammar/shape isn't validated
   anywhere. §11.5 requires lnastfmt to enforce `(root, p1..pN, key, value)`
   for `attr_set` and `(root, p1..pN, key)` for `attr_get`. No validation
   runs today.

7. **`inou/code_gen/lnast_map.hpp` full X-macro generation still pending.**
   Partial done: `static_assert(namemap_*.size() == Lnast_ntype::Lnast_ntype_last_invalid)`
   added and 17 missing type-node entries filled in. Full X-macro
   generation from `lnast_nodes.def` still open — until it lands, every
   new LNAST node still needs a manual update in all three `namemap_*`
   arrays at the same enum-position.

---

## 1. Convention unification across producers

*Status:* §1.1 (tmp-var prefix) and §1.2 (undotted I/O in prp) done.
§1.3 (bare attrs vs `__`) and §1.4 (lnastfmt validator) pending — gated
on §10 ST API.

| Convention            | `inou/prp` (new tree-sitter)      | `inou/pyrope` (old)            | `inou/slang`                   |
|-----------------------|-----------------------------------|--------------------------------|--------------------------------|
| Attribute literal     | `size`, `bits` (bare const)       | `__ubits`, `__sbits`, `__bits` | `__ubits`, `__sbits`           |

**Action items**

1. Decide if attribute identifiers carry `__` (old Pyrope, slang) or not
   (new Pyrope docs / tree-sitter). See §4, §11 — gated on §10.
2. Make `pass.lnastfmt` perform a *validation* sweep in addition to its SSA
   cleanup. Currently `Pass_lnastfmt::is_temp_var` only handles `___`
   prefix and `is_ssa` is a crude `substr(0,2) != "__"` test at
   `pass/lnastfmt/pass_lnastfmt.cpp:203-207`.

---

## 2. Node-name drift between docs and implementation

*Status:* **deferred** — the docs referenced in the original TODO live in
`../docs/docs/pyrope/`, which is outside LiveHD.
Aligning them belongs in that repo. No code-side action needed.

`docs/pyrope/12-lnast.md` describes nodes by names that don't exist in
`lnast/lnast_nodes.def`. Either the doc needs an update or the code does.

| Doc name (`12-lnast.md`) | Code name (`lnast_nodes.def`) |
|--------------------------|-------------------------------|
| `tup_add`/`tup_set`/`tup_get`/`tup_concat` | `tuple_add`/`tuple_set`/`tuple_get`/`tuple_concat` |
| `not`, `and`, `or`, `xor` | `bit_not`, `bit_and`, `bit_or`, `bit_xor` |
| `lnot`, `land`, `lor` | `log_not`, `log_and`, `log_or` |
| `reduce_or`, `reduce_and`, `reduce_xor` | `red_or`, `red_and`, `red_xor` |
| `let`, `var` (as top-level assignment variants) | Only `assign`, `dp_assign`, `mut` exist |
| `attr_ref_set`, `attr_ref_check` (sub-nodes of `ref`) | Only top-level `attr_set`/`attr_get` |
| `in`, `has`, `does`, `to`, `range` as first-class ops | Only `range` exists; `is` exists but no `in`/`has`/`does` |
| `set`, `check`, `assign` as three-way distinction | `assign` (+ `dp_assign`, `mut` — both `FIXME: remove`) |

The doc examples freely attach attribute sub-nodes to a `ref`, which LNAST's
current tree shape does not support (a `ref` has no children in the current
implementation). This is the single biggest semantic gap.

**Action items**

1. Decide whether LNAST stays with flat `attr_set`/`attr_get` statements or
   adopts the doc's nested `attr_ref_set`/`attr_ref_check` attached to `ref`.
2. Rename doc terminology or `lnast_nodes.def` so they match one-to-one.
3. Add missing first-class ops used in docs: `in`, `has`, `does`, `to`,
   `popcount` (`popcount` *is* in the def file but missing from the doc's
   bit-selection lowering). Also `implies`, `and_then`, `or_else` examples in
   `12-lnast.md` assume specific lowerings that have no matching LNAST node.
4. Resolve the `let`/`var`/`assign`/`const`/`mut`/`reg` storage-class question.

---

## 3. New Pyrope syntax with unclear LNAST lowering

*Status:* **out of scope** for the current track — scoped out per
"don't evolve Pyrope to latest tree-sitter grammar". §3.1 through §3.14 all
assume the new surface syntax and therefore wait until Pyrope migration is
scheduled. Kept here unchanged as the design notes for that future effort.

### 3.1 Storage classes (`const`/`mut`/`reg`/`comptime`)

```pyrope
comptime SIZE = 16
comptime mut counter = 0
const my_constant = 42
mut my_wire = 0
reg my_state = 0
```

Questions: does LNAST carry a storage-class annotation on `ref`? Is `reg`
modeled via the `#` prefix (today's convention) or a new node? Does `comptime`
become an attribute (e.g., `::[comptime]`) or a type kind?

### 3.2 Attributes on declarations vs reads

```pyrope
mut data:u32:[max=1000, min=0] = 0      // set at decl
cassert counter::[bits] == 8             // read via ::[ ]
```

Two distinct syntaxes (`:[...]` set, `::[...]` read). Current `inou/prp`
emits the same `attr_get` in both cases. Decide LNAST shape for decl-time sets
(nested under the declaration? a separate `attr_set` statement right after?).

### 3.3 Memory and array declarations

```pyrope
reg ram:[1024]u32:[latency=1, rdport=(0,1), wrport=(2,3)] = 0
mut out1 = ram.port[0][addr3]:[rdport=0]
```

Arrays/memories with port attributes have no example in `12-lnast.md`. LNAST
currently has `comp_type_array` but no way to attach a multi-attribute bundle
to it. Port access `ram.port[0][addr]:[rdport=0]` needs a lowering rule.

### 3.4 Ranges with three operators

```pyrope
1..=5      // inclusive
0..<4      // exclusive
2..+3      // size-based (length = 3)
```

`lnast_nodes.def` has a single `range` node with implicit semantics. The doc
shows `a..<b` lowering to a `sub`+`range` pair but `..+` is not spec'd.

### 3.5 Tuple positional-named keys (`:pos:name`)

Docs mention `x.:3:foo = 2` as equivalent to `x[3] = 2` *and* a label-check
that slot 3 has name `foo` (`12-lnast.md:119-123`). This requires a composite
key format that LNAST consumers don't agree on:
- `bundle.cpp` parses `:<pos>:<name>` format internally.
- `upass_constprop.cpp:271` writes `":{pos}:{name}"`.
- `inou/pyrope/prp_lnast.cpp` and `inou/prp/prp2lnast.cpp` never emit this
  form.

Decide: is `:pos:name` a producer-visible convention or purely an internal
bundle key? If external, document the lowering.

### 3.6 Bit-selection modifiers (`#[..]`, `#sext[..]`, `#|[..]`, `#+[..]`)

```pyrope
const t1 = foo#sext[..=4]
const t5 = foo#+[..=4]    // popcount
```

`12-lnast.md` gives a lowering, but `#+` → `popcount` and `#sext` → `sext`
after `get_mask` is not implemented in any current producer. Confirm the
lowering and add tests.

### 3.7 Match expressions and `unique if`

`grammar.js:231-250` defines `match` with a rich `match_operator` list (`has`,
`in`, `case`, `does`, `is`, plus negations). Current LNAST has `if` but no
`match`. Decide: desugar to nested `if`/`hot_phi` (risky for unique semantics)
or add a `match` node.

### 3.8 `puts`, `test`, `assert` vs `cassert`

`assert` (runtime) and `cassert` (compile-time) both appear in docs. LNAST has
`cassert` only. `puts` (debug print) has no LNAST encoding.

### 3.9 Lambda / function types with timing

```pyrope
comb add(a:u8, b:u8) -> (result:u8) { ... }
pipe[1] counter() -> (reg count:u8) { ... }
flow alu(in1, in2) -> (out) { ... }
```

`comp_type_lambda` exists but doesn't carry `comb`/`pipe`/`flow`/`mod`
classification or the `[N]` pipeline depth. Need to either extend the node or
add an attribute.

### 3.10 `ref` parameters

Small Pyrope has `ref` both as storage (`ref x`) and as argument modifier
(`comb(ref self)`). `grammar.js:394` calls it `ref_identifier`. LNAST's `ref`
*node type* is unrelated to this; every variable reference is a `ref` node.
This name collision will bite when implementing argument passing.

### 3.11 Timed identifier `@[...]`

```pyrope
counter@[] += 1     // deferred
a@[-1]              // previous cycle
:@[N]               // timing type check on LHS
```

The `delay_assign` node added in §15.2 covers the offset semantics. Still
needed: a producer emission for each surface form in `inou/prp` (none of
`@[N]`, `@[-1]`, `@[]`, `::[defer]` produce LNAST today).

### 3.12 Enums / variants / `impl` / `type` statements

`tree-sitter-pyrope/grammar.js:261-393` defines `type_statement`,
`impl_statement`, `enum_definition`, `enum_assignment`. LNAST has
`comp_type_enum` and `type_def` (FIXME: rename to `type_bind`) but no
documented lowering for trait-style `impl` blocks.

### 3.13 For-comprehension and for-unrolling

```pyrope
for i in 0..=7 { memory[i] = init_value }   // compile-time unrolled
x = [e*2 for e in data]                     // comprehension in expression
```

LNAST has `for` but the semantic distinction between runtime-loop (none in
Small Pyrope) and compile-time-unroll needs a marker. Comprehensions inside
expressions (`for_comprehension` in the grammar) have no LNAST example.

### 3.14 `when`/`unless` statement gate

`grammar.js:760-767` allows `stmt when cond` / `stmt unless cond`. This is a
single-statement guarded execution with no LNAST example — probably lowers to
`if` but should be documented.

---

## 4. In-band string meaning — inventory and cleanup

*Status:* **pending** — individual entries below are status-tagged. Most
require §10 ST rework to fully resolve.

The LNAST tree currently relies on string-prefix conventions inside `ref` and
`const` nodes to encode semantics. This forces every consumer to string-match
and collides with any identifier that happens to start with those characters.

### Known conventions still in use today

| Convention        | Meaning                            | Example location                                         |
|-------------------|------------------------------------|----------------------------------------------------------|
| `$name`           | Graph input                        | `lnast.hpp:170`, `slang_tree.cpp:83`, `prp_lnast.cpp`    |
| `%name`           | Graph output                       | `lnast.hpp:169`, `slang_tree.cpp:90`                     |
| `#name`           | Register / flop                    | `lnast.hpp:168`                                          |
| `___<n>`          | Compiler tmp (canonical)           | `lnast.cpp:988`, `lnast_create.cpp:12`, `prp2lnast.cpp:1288`, `prp_lnast.cpp:29` |
| `name|<n>`        | SSA subscript                       | `lnast/lnast.cpp:639-650`, `lnast/lnast.hpp:154-160`     |
| `__ubits`/`__sbits`/`__bits` | Bitwidth attribute      | `lnast_create.cpp:448-450`, `bundle.cpp:925`             |
| `__dp_assign`     | Deferred-parent assign marker      | `lnast.cpp:145`, `cgen_verilog.cpp:1023`                 |
| `__reset`         | Reset pin for a register           | `inou/pyrope/tests/cnt_attr7.prp`                        |
| `__mask`          | Bit-mask-derived name              | `inou/yosys/lgyosys_tolg.cpp:2374`                       |
| `__fir_const`     | FIRRTL constant marker (sub-name)  | `inou/locator/traverse_lg.cpp:176+`                      |
| `__range_begin`/`__range_end` | Range endpoints         | `inou/code_gen/code_gen.cpp:221-263`                     |
| `__valid`/`__retry` | Dataflow handshake attributes    | `inou/code_gen/code_gen.cpp:842-845`                     |
| `0.<attr>` vs `__<attr>` | Root-attribute canonicalization | `lnast/bundle.cpp:736-759`                           |
| `:pos:name`       | Bundle composite key               | `upass_constprop.cpp:271`, `bundle.cpp:745`              |
| `__` but `___` is NOT attribute | Attribute predicate   | `lnast/bundle.cpp:736`, `inou/pyrope/prp_lnast.cpp:1612`, `upass_constprop.cpp:377` |

### What to rethink

1. **Attribute vs identifier conflict.** Proposal: drop the `__`
   prefix and carry attribute-ness via node-kind — e.g., a dedicated
   attribute-key node or the `attr_set` / `attr_get` statement always
   tagging its last child as an attribute name. Gated on §10/§11.

2. **`%`/`$`/`#` I/O marker in `ref` text.** Partial progress:
   the dotted `$.` / `%.` variant was removed from prp. Full removal is
   §12.

3. **Tmp-var prefix proliferation.** Done for strings (§1.1).
   `Tree_index`-identity tmps (§13) **pending**.

4. **`:pos:name` bundle key.** Decide if it's a language
   feature (document in `12-lnast.md` §Tuples) or a symbol-table
   implementation detail (hide it).

---

## 5. `pass.lnastfmt` extension — a consistency checker

*Status:* **pending.** No full validator implemented yet. Golden-output
harness also pending.

`pass/lnastfmt/pass_lnastfmt.cpp` today does only one job: remove SSA
subscripts and collapse assign-of-tmp chains. The pass should grow into a
format *validator* run before every LNAST consumer (`lnast_tolg`, `upass`,
`code_gen`). Minimum checks still needed:

1. Every `ref` text matches one of: `$<name>`, `%<name>`, `#<name>`,
   `___<digits>` (tmp), `<alpha_id>`, `<alpha_id>___<digits>` (SSA).
2. Every `const` text is either a numeric literal (parseable by
   `Lconst::from_pyrope`), a `"string"` literal, or an attribute identifier
   from a documented whitelist (or drops the `__` prefix per §4.1).
3. `assign`/`dp_assign` arity validation is reportable in release builds
   (partial done). Still to add: a positive check that the RHS is `ref` or
   `const` (rather than e.g. a nested statement).
4. `tuple_set`/`tuple_get`/`tuple_add`/`attr_set`/`attr_get` child shapes
   match the docs in `12-lnast.md`. The docs are the spec; lnastfmt should
   enforce them.
5. Unary vs binary `minus` / `bit_not` arity is unambiguous. Right now
   `lnast.cpp` and consumers distinguish unary `-x` (2 children: dst, src)
   from binary `a-b` (3 children: dst, a, b) by *counting children*. An arity
   check is easy; better still, split into `neg` and `minus`.
6. `dp_assign`, `mut`, `uif`, `phi`, `hot_phi` — all marked `FIXME: remove` in
   `lnast_nodes.def`. The formatter should warn when a fresh producer emits
   them and reject them in CI once the migration is done.
7. `delay_assign` — "reject offset=0 on non-reg src" still pending. Needs
   symbol-table visibility into declared `storage=reg` attrs, which lives
   in `lnast_tolg` right now, not `lnastfmt`. Deferred until §10 ST API.

This also gives us a tool to compare the three producers on the same input
(once a common source language exists — probably the new small-Pyrope) and
diff-check their LNAST output.

---

## 6. Producer-specific issues

### 6.1 `inou/prp` (new tree-sitter-based)

- Attribute access `a.[size]` correctly lowers to `attr_get` with bare
  `const size` — this is the *future* convention per §4.1. The old
  producers still emit `__size`/`__ubits`/etc. The inconsistency means
  `upass_constprop.cpp:375-381` (which strips `__*` attributes) silently
  works on old output and silently *fails to strip* new output.
- `fdef not supported yet` warning still fires on
  `inou/prp/tests/comptime/attr_size.prp` (lambda inside expression).
  Needed for most real tests.
- `inou/prp/tests/comptime/*.prp` are parse-smoke tests only; no assertions
  on the generated LNAST. Adding golden-output comparison would catch the
  §1 drift automatically.

### 6.2 `inou/pyrope` (old PRP parser)

*Status:* frozen per user direction ("don't evolve Pyrope").

- Uses an older Pyrope grammar that does not match `tree-sitter-pyrope`. The
  three TODO items in `inou/pyrope/TODO.md` (fcall args, missing
  `..fname..` method call, redundant `assign` after expression) are still
  open; the new parser should not re-implement these bugs.
- Plan: freeze `inou/pyrope` feature work, migrate tests to `inou/prp` as
  that stabilizes, delete `inou/pyrope` when covered.

### 6.3 `inou/slang`

- `process_top_instance` at `slang_tree.cpp:113` — the `I(false)` is
  actually unreachable in practice (InterfacePort is handled in its own
  branch with a FIXME log). First SV interface test will print FIXME and
  silently skip, not crash.

---

## 7. Consumer-side assumptions to document or relax

### 7.1 `pass/lnast_tolg`

*Status:* pending (§12).

- `lnast_tolg.cpp:40-42` assumes the graph always has `$` and `%` top-level
  IOs. If a module has no outputs, `get_graph_output("%")` still has to be
  valid. Confirm this still holds under the §4.2 cleanup.
- Many `FIXME` comments embed the `__xxx` string assumption — once §4.1
  lands, these need auditing.
- `process_ast_delay_assign_op` dispatches offset=1; other offsets error
  out. Needs full §15.2 offset support when the time comes.

### 7.2 `pass/lnastopt`

- Performs SSA-aware copy propagation. Does not currently cross `if`
  boundaries; check whether this is intentional vs. an outstanding TODO.

### 7.3 `upass/*` (work in progress)

- `upass_constprop.cpp:375-381` hardcodes the `__` attribute-skip
  predicate — see §4.1. Still pending.

---

## 8. Test infrastructure

*Status:* **pending.**

1. Add a golden LNAST test per producer: run
   `inou.prp|inou.pyrope|inou.verilog … |> lnast.print` on a shared corpus
   and diff against `*.lnast.expected` files. Catches §1 drift regressions.
2. Convert `inou/pyrope/tests/lnast_prp_test.sh` into a bazel `sh_test` so
   regressions surface in `bazel test //...`.
3. `lnast/tests/ln/*.ln` uses its own surface syntax (see
   `lnast/tests/ln/types.ln`). Document this syntax — it's the LNAST textual
   serialization and is not the same as the `lnast.dump` output.

---

## 9. Suggested ordering

Smallest blast-radius first:

1. Write §3 snippets into `inou/prp/tests/comptime/` as parse-smoke tests
   so drift shows up immediately (no LNAST-shape work needed).
2. Land a golden-output harness in `pass/lnastfmt` / `lnast/tests`
   (§5, §8.1). Capture current behavior of all three producers as baseline.
3. Resolve the `__attr` → bare-attr question (§4.1). Touches a lot of files
   but is mostly mechanical once lnastfmt can enforce.
4. Decide storage-class representation (§2, §3.1). Blocks the new-Pyrope
   front-end.
5. Everything else in §3 (match, timed ref, memories, etc.) — one PR per
   item, each gated on §5.

---

## 10. Plan: attribute propagation rules for the symbol-table rework

*Status:* **pending.** Not started.

Complements §1 and §4. Decides how attributes flow (or don't) once they live
in a side symbol table instead of in-band strings.

### 10.0 Model: "attributes don't propagate — values are computed"

- **Declarations own attributes.** The only place user/decl attributes are
  written is at the declaration site (`let`, `mut`, `const`, `reg`,
  `comptime`, port, lambda arg, tuple literal field). Examples of decl-only
  attrs: `storage` (const/mut/reg/comptime), `direction` (in/out),
  `reset_pin`, `clock_pin`, `initial`, `pipeline_depth`, `debug`, user-
  asserted bounds (`max=N`, `min=N`).
- **Assign/expressions do NOT propagate attributes.** `y = x` creates a
  fresh entity `y`. None of x's decl-only attrs travel to y. This matches
  hardware semantics (y is a new wire/net, not a view of x).
- **Value attrs `__min`/`__max` are computed, not propagated.** They are
  derived per-SSA-version from the RHS expression via interval arithmetic.
  `y = x` computes `y.__max = x.__max` as a trivial case of identity, not
  as a propagation rule. `z = a + 1` computes `z.__max = a.__max + 1`.
- **Tuple literals are the one structural exception.** In
  `a = (foo=b, c)`, field `a.foo` carries forward b's decl-only attrs, and
  the positional slot carries c's. This is because a tuple literal
  constructs structure that references the source entities per-field; it's
  not a flat expression.
- **Bit-width attrs (`bits`/`ubits`/`sbits`) are derived views over
  `__min`/`__max`** — not stored independently. Consumers that need a bit
  width query a helper that inspects min/max and the signedness attribute.

### 10.1 Two ST lifetimes

- **Per-declaration table** (small, written once, read often): decl-only
  attrs keyed by the pre-SSA declaration name (`a`, not `a___3`).
- **Per-SSA-version table** (larger, populated by the inference pass):
  `__min`/`__max` keyed by SSA name (`a___0`, `a___1`, ..., `___t5`).

### 10.2 Value-attr inference pass (eager)

- Runs as part of `pass.lnastfmt` (or a dedicated sub-pass invoked by
  lnastfmt) **after** SSA, **before** any consumer that needs widths.
- Walks LNAST once; for each SSA definition computes `__min`/`__max` from
  the RHS using interval arithmetic. Writes the result into the per-SSA
  ST entry.
- For regs: fixed-point over all RHS expressions assigned to the reg's SSA
  versions, unioned with the declared `initial` value. Converges quickly
  because the reg decl provides a user-asserted bound (or widens until the
  bound is hit, at which point lnastfmt errors if assigned values exceed
  the assertion).
- Constant-folds trivially along the way (does not replace `lnastopt` —
  just produces the ranges, not the folded tree).

### 10.3 User-asserted bounds vs derived bounds

- Asserted bound at decl (e.g., `mut a:[max=100]`) is stored as a
  decl-only attr on `a`. It is a constraint, not a fact about a specific
  SSA version.
- Derived `__max` is per-SSA-version, computed by §10.2.
- After each assignment, the inference pass must check:
  `a___i.__max <= a.max_asserted` and symmetric for min. Violations are
  lnastfmt errors (with source location from the LNAST token).
- Keep them as separate keys in the ST (`max_asserted` vs `__max`) to
  avoid ambiguity.

### 10.4 Consumer contract

- Decl-only attr lookup: `st.get_decl_attr(decl_name, key)` — returns the
  declaration's attr or absent.
- Value attr lookup: `st.get_value_attr(ssa_name, key)` — returns the
  per-SSA `__min`/`__max`. Must be populated by §10.2 before the consumer
  runs; lnastfmt errors otherwise.
- Direction / storage / reg-ness checks (replacing today's
  `front() == '$'` etc.) go through decl-only lookup: `is_input(name)` →
  `st.get_decl_attr(decl_of(name), direction) == input`.

### 10.5 Node/shape changes

- Every `ref` carries (or can be resolved to) its declaration name. Either:
  - Add a `decl_id` field on `Lnast_node` populated during parse, or
  - Keep using the pre-SSA text (before the `___<n>` suffix) as the key.
- The existing `Lnast_node::subs` field already separates SSA subscript
  from the text. §4.3 cleanup (stop concatenating `___<subs>` into the ref
  text) directly supports the ST keying scheme here.

### 10.6 Gating / ordering

- Land the ST data structure + API first (empty, unused).
- Populate from existing in-band strings in lnastfmt (both old `__ubits`
  and new bare `bits` forms). Consumers still read in-band.
- Migrate consumers one at a time to read via ST. Each migration gated
  on golden tests (§8.1).
- Stop emitting in-band strings from producers.
- Delete string predicates (`front() == '$'`, `starts_with("__")`, etc.).

---

## 11. Plan: unify `attr_set` with `tuple_set` syntax; collapse `assign`

*Status:* **pending.** Gated on §10.

Outcome of a design discussion on Issue 3 (path addressing) and §2's
`let`/`var`/`assign`/`mut` confusion.

### 11.1 Shared variadic-path shape

`attr_set` and `tuple_set` share the same child shape (root ref, tuple
path elements, terminal value), differing only in how the terminal pair is
interpreted:

- `tuple_set root  p1 p2 ... pN   value`
  Writes `value` into tuple slot `root.p1.p2…pN`.
- `attr_set  root  p1 p2 ... pN   attr_key   value`
  Writes `value` into the attribute named `attr_key` on tuple slot
  `root.p1…pN`. The **last non-value child is always the attribute key**;
  everything between root and attr key is tuple path. Resolves the path-vs-
  attr-key ambiguity (Issue 3) with a positional convention.

`attr_get` mirrors `attr_set` without the value — last child is the attr
key; everything before is the tuple path.

`tuple_get` unchanged.

### 11.2 Semantic difference: write-once, comptime-guarded

`attr_set` is allowed **anywhere in source code**, including inside control
flow. The constraints are on *evaluation*, not location:

- **Write-once per `(target, attr_key)` pair.** Across the comptime-resolved
  execution, each attribute slot is written at most once. Second write
  ⇒ lnastfmt error.
- **Guards must be comptime-resolvable.** Any enclosing `if`/`for`/`while`/
  `match` condition controlling an `attr_set` must be evaluable at compile
  time. Runtime-guarded `attr_set` ⇒ lnastfmt error.
- **Values must be comptime-resolvable.** The value passed to `attr_set`
  must be a const, a comptime variable, or a comptime expression.
- **No `attr_set` after a read.** Once a variable has been *read* in source
  order, any subsequent `attr_set` on that variable is a compile error.
  This prevents the "first half sees one attr set, second half sees
  another" bug class. A "read" is any use that observes the value or the
  attribute set: RHS of `assign`/`tuple_set`, operator argument,
  call-site argument, `if`/`while`/`match` condition, or target of an
  `attr_get`. Pure `tuple_set` (writes) do **not** freeze — they are
  validated against the final resolved attr set at comptime resolution.
- **Source-order independence among attr_sets and writes.** Provided no
  read has occurred yet, `attr_set` and `tuple_set` can interleave freely.
  Example legal sequence: `tuple_set a 5; attr_set a max 10` — the write
  is validated against `max=10` at comptime resolution. Illegal sequence:
  `b = a; attr_set a max 10` — the read of `a` has already occurred.
- **Source-order dependence for attr-reads inside attr values.** If
  `attr_set a max (1 << a.bits) - 1` reads `a.bits`, the corresponding
  `attr_set a bits ...` must evaluate first in source order. Reading an
  unset attr during the comptime phase ⇒ error. Keeps the comptime phase
  from needing topological sort.
- **Derived `__min`/`__max` are NOT emitted via `attr_set`.** The value-attr
  inference pass (§10.2) writes them to the per-SSA-version ST table
  directly; `attr_set` remains exclusively for user/decl-time attrs.
- **Unset attrs at use site are "absent", not error.** A consumer querying
  `a::[max]` when no `attr_set a max ...` ever ran (e.g., comptime-guarded
  branch was false) gets "absent". Consumers decide whether absence is
  meaningful (e.g., width inference falls back to `__min`/`__max`
  derivation).

Common patterns enabled:

```pyrope
comptime USE_PIPELINE = true
mut stage = 0
if USE_PIPELINE {
  attr_set stage storage reg
} else {
  attr_set stage storage mut
}

comptime N = 4
for i in 0..<N {
  attr_set bus.lane[i] bits 32   // each i targets a distinct slot
}
```

### 11.3 Collapse `assign` into `tuple_set`

`tuple_set` with two children (`root`, `value`) is semantically identical to
today's `assign`. Collapse:

- `assign root value` → `tuple_set root value` (canonical 2-child form).
- Remove `assign` from `lnast_nodes.def` (or keep as a deprecated alias for
  migration).
- `mut` (already `FIXME: remove`) → `tuple_set` + decl-time `attr_set ...
  storage mut`. Mutability lives in the attribute, not a node kind.
- `dp_assign` (also `FIXME: remove`) stays as its own concern — "deferred
  parent ownership" is a scope question, not a tuple-path operation.
  Separate issue.
- Producers, `lnast_create.cpp`, and consumers migrate in lockstep with the
  ST rollout.

### 11.4 Example lowerings

**Declaration with inline attrs and initial value:**

```pyrope
mut data:u32:[max=1000, min=0] = 0
```

```
attr_set data storage mut
attr_set data bits 32
attr_set data max 1000
attr_set data min 0
tuple_set data 0
```

**Nested-field attribute:**

```pyrope
mut config:[clock = (freq:u32:[max=1e9] = 100_000_000)]
```

```
attr_set config clock freq storage mut
attr_set config clock freq bits 32
attr_set config clock freq max 1_000_000_000
tuple_set config clock freq 100_000_000
```

**Register with reset pin:**

```pyrope
reg counter:u8:[reset_pin=rst, initial=0] = 0
```

```
attr_set counter storage reg
attr_set counter bits 8
attr_set counter reset_pin rst
attr_set counter initial 0
tuple_set counter 0
```

### 11.5 lnastfmt enforcement

Single-pass validation:
1. Track `(decl_name, attr_key)` pairs seen via `attr_set`. Duplicate ⇒
   error "attribute already set".
2. For each `tuple_set`/read, mark the decl as "in use phase". Any later
   `attr_set` on that decl ⇒ error "attribute set after first use".
3. Any `attr_set` encountered inside a non-decl scope (child of `if`,
   `for`, `while`, `func_def`, `stmts` nested under control flow) ⇒ error.
4. Validate attr_key against the known-attr whitelist. Unknown keys ⇒
   warning during migration, error post-migration.

### 11.6 Files affected

- `lnast/lnast_nodes.def` — add/confirm `attr_set` as variadic; mark
  `assign`/`mut` deprecated.
- `lnast/lnast.cpp`, `lnast/lnast_create.cpp` — emit `tuple_set` for the
  assign case; emit `attr_set` with path-value form.
- `lnast/bundle.cpp:736-956` — replace the `__`-prefix canonicalization
  with ST-backed attribute lookup.
- `inou/prp/prp2lnast.cpp`, `inou/pyrope/prp_lnast.cpp`,
  `inou/slang/slang_tree.cpp` — producers emit unified form.
- `pass/lnastfmt/pass_lnastfmt.cpp` — add validations §11.5.
- `pass/lnast_tolg/*`, `inou/code_gen/code_gen.cpp`, `upass/*` —
  consumers read via ST API; stop string-matching `__xxx`.

### 11.7 Ordering

- Gated on the ST API from §10 landing first.
- Can progress in parallel with §15 offset extensions — orthogonal
  node-shape changes.

---

## 12. Plan: replace `$`/`%`/`#` prefixes with ST-backed direction/storage

*Status:* **pending.** Gated on §10 + §13 full.

Remove prefix-in-ref-text as in-band signaling; move direction/storage to the
symbol table as decl-only attributes.

### 12.1 Representation

- **Stored ref text carries bare names only** — no `$`/`%`/`#` prefix, no
  dotted variant.
- Direction and storage are decl-only attributes set via `attr_set`:
  - `attr_set foo direction input` (replaces `$foo`)
  - `attr_set foo direction output` (replaces `%foo`)
  - `attr_set foo storage reg` (replaces `#foo`)
- Consumers query via ST helpers: `st.is_input(name)`, `st.is_output(name)`,
  `st.is_reg(name)`. The string-prefix tests (`front() == '$'` etc.) go
  away.

### 12.2 `lnast.print` rendering (LLVM-IR-style)

`lnast.print` synthesizes prefixes from the ST on dump for human
readability:
- `$foo` for inputs, `%foo` for outputs, `#foo` for regs, bare name
  otherwise.
- Tmps can render LLVM-style as `%<tree_index>` once §13 lands.
- SSA subscripts render as `foo.<n>` or similar (see §4.3 on moving SSA
  subscript out of ref text).
- Round-trip is not a goal — `lnast.print` is for humans, not parsing.

### 12.3 Top-level I/O bundle

- `pass/lnast_tolg/lnast_tolg.cpp:40` uses bare `"%"` as the output-bundle
  sentinel. Drop the sentinel: lnast_tolg iterates decls with
  `direction=out` from the ST.
- Similarly for input-bundle lookups.

### 12.4 Clock / reset / other implicit signals

- No reserved names. Clock and reset are ordinary inputs (`attr_set clk
  direction input`), with the reg's `clock_pin` / `reset_pin` attributes
  pointing to them by name (`attr_set counter clock_pin clk`).
- Removes the special-case handling for `$clk`/`$reset`-like conventions.

### 12.5 Migration order

1. Land ST API + `is_input` / `is_output` / `is_reg` helpers (initially
   populated by falling back to the prefix). No consumer changes yet.
2. Migrate consumers one at a time to use the helpers. Each migration
   gated by the golden-output tests (§8.1):
   - `pass/lnast_tolg/*`
   - `inou/code_gen/code_gen.cpp`
   - `upass/*`
   - `lnast/bundle.cpp`
3. Migrate producers to emit bare names + `attr_set`:
   - `inou/prp/prp2lnast.cpp:1227-1229` — drop dotted form, emit attrs.
     (Dotted form already removed.)
   - `inou/pyrope/prp_lnast.cpp` — all `$`/`%` call sites.
   - `inou/slang/slang_tree.cpp:83,90` — I/O wiring.
4. Remove prefix-parsing in `lnast.hpp:168-170` / `lnast.cpp` `is_input`
   etc. Dump code renders from ST.

### 12.6 Files affected

- `lnast/lnast.hpp:168-170` — `is_input`/`is_output`/`is_reg` predicates
  forward to ST.
- `lnast/lnast.cpp` — remove prefix-based string tests.
- `lnast/lnast_writer.cpp` — ST-backed rendering.
- Producers: `inou/prp/prp2lnast.cpp`, `inou/pyrope/prp_lnast.cpp`,
  `inou/slang/slang_tree.cpp`.
- Consumers: `pass/lnast_tolg/*`, `inou/code_gen/code_gen.cpp:842+`,
  `upass/upass_constprop.cpp:21,375-381`, `lnast/bundle.cpp`.

### 12.7 Gating

Depends on §10 (ST API) and §11 (`attr_set` semantics) being in place.
Largest mechanical change — should be the last major cleanup, not the
first.

---

## 13. Plan: tmp vars use `Tree_index`; foreign names sanitized per-producer

*Status:* **partial.** String-prefix drift resolved (§1.1); `Tree_index`
identity pending.

### 13.1 Tmp variables

- Compiler-generated tmps carry **no ref text**. Their identity is
  `Lnast_node::Tree_index`. **Pending.** Today all three producers emit
  the canonical `___<n>` string.
- A tmp is an `Lnast_node` with kind `ref` and a tmp-marker (bit on the
  node, or a dedicated leaf kind — decision during impl).
- When one operator's output feeds another's input, they reference each
  other via `Tree_index`, not by name.
- `lnast.print` renders tmps LLVM-style: `%<tree_index>` in dumps.
- `pass_lnastfmt.cpp:204` `is_temp_var` string-match goes away; becomes
  `node.is_tmp()`.

### 13.2 SSA interaction

- Tmps are SSA by construction (single-assignment by tree position); the
  SSA pass does not rename them.
- `lnast.cpp:1229`'s `starts_with("___") → skip SSA` guard becomes
  unnecessary.

### 13.3 Foreign-language identifier safety

LNAST stored ref text is restricted to the "safe identifier" grammar:
`[A-Za-z_][A-Za-z0-9_]*`. Producers for source languages with looser
rules must sanitize and preserve the original.

**Rule:** any identifier that does not match the safe grammar is rewritten
to a safe form by the producer, and the original name is stored via
`attr_set <safe_name> source_name "<original>"`.

Examples:
- Verilog escaped `\foo-bar` → safe `foo_bar` (or `foo_bar_<hash>` for
  collision avoidance) + `source_name="\foo-bar"`.
- Digit-leading `003` → safe `v003` + `source_name="003"`. (Verilog
  producers are free to follow Yosys's `$003` convention if preferred —
  that's a producer-internal sanitization choice, not an LNAST-level
  requirement.)

**Why not keep Yosys's `$`/`\` convention in LNAST directly?** Under §12,
`$` is reserved by `lnast.print` for input rendering. Allowing `$`-
leading stored text would either collide with that convention or force
multi-char escape in dumps. Cleaner to keep LNAST text always-safe and
push producer-specific conventions into the sanitizer layer.

**Emitting back to the source language:** emitters (e.g., a Verilog
backend) read `source_name` from the ST when present to reconstruct the
original identifier.

### 13.4 Benefits

- Eliminates cross-producer tmp collisions (only one tmp representation:
  `Tree_index`).
- Faster SSA (no string allocation per tmp, no substring checks).
- Clean handling of arbitrary source identifiers without collisions with
  LNAST-internal naming.
- Dump output is shorter (`%5` vs `___t5`).

### 13.5 Migration

1. Add `Lnast_node::is_tmp()` and `tmp_ref(Tree_index)` API.
2. Producers stop generating `___t<n>` strings; emit tmp refs by tree
   position.
3. Add a sanitizer in `inou/slang` (and future `inou/verilog`) that
   rewrites non-safe names and emits `source_name` attrs.
4. Consumers that look up tmps by name get a migration helper mapping old
   string forms to `Tree_index` — temporary, removed once producers are
   migrated.
5. Regenerate `lnast/tests/ln/*.ln` golden files.
6. Delete the `___*` prefix generators and the associated
   `substr`/`starts_with` tests from `lnast.cpp:988,1229`,
   `pass_lnastfmt.cpp:204`, and producer code.

### 13.6 Ordering

Independent of §10–§12. Recommended **before** §12's prefix cleanup
because it reduces the string-matching surface area §12 has to audit.

---

## 14. Plan: migration rollout for §10–§13

### 14.1 Per-producer strategy

- **`inou/prp` (new tree-sitter-based).** About to be rewritten to target
  the new `tree-sitter-pyrope` grammar. Emits the new LNAST form from day
  one. No legacy-shape support needed. Breaking semantics during the
  rewrite is fine.
- **`inou/pyrope` (legacy PRP parser, on sunset per §6.2).** Keep it
  functional only long enough for `inou/prp` to cover its test corpus.
  Migration at emission sites only:
  - Where it emits `tuple_set X __<attr> V`, emit `attr_set X <attr> V`.
  - Where it emits a `$X` / `%X` / `#X` prefix, emit bare `X` + the
    corresponding `attr_set X direction/storage ...`.
  - Skip the harder migrations: tmp-vars → `Tree_index` (§13). The tests
    exercising these keep the legacy form on this producer until it is
    deleted.
- **`inou/slang`.** Owned locally; update emission sites directly to the
  new form. No dual-mode needed.

No producer runs in dual-mode. Each flips atomically in its own PR. The
consumer side (reading via ST) is already ready when the flip lands.

### 14.2 Consumer migration

- Land the ST API (§10) with a populator that reads the **current** in-
  band form in `pass.lnastfmt` (legacy reader). Consumers switch to ST
  helpers one at a time.
- Each consumer migration (one PR each): `lnast_tolg`, `code_gen`,
  `upass_constprop`, `bundle`, `lnastopt`.
- Between PRs, master is green: consumers that have flipped use the ST;
  those that haven't still use string tests. Both paths are exercised by
  the golden-output harness (§8.1).

### 14.3 Legacy-form reader lifetime

`lnastfmt`'s legacy reader (populating the ST from `$foo` / `__bits` /
`___t<n>`) stays alive as long as `inou/pyrope` does. When `inou/pyrope`
is deleted, the legacy reader goes with it. At that point, all producers
emit canonical form and the ST is populated directly from `attr_set`
nodes.

### 14.4 Ordering

- **§13 (Tree_index tmps)**: land first. Smallest blast radius, no ST
  dependency. Only `inou/prp` and `inou/slang` migrate here; `inou/pyrope`
  keeps `___<n>` until deletion.
- **§10 (ST API + value-attr inference)**: second. Unblocks everything.
- **§11 (attr_set/tuple_set shape + collapse assign)**: third. Depends on
  §10.
- **§12 (prefix → direction/storage attrs)**: fourth. Depends on §10 + §13.
- **§15.2 offset extensions**: any time after §10. Orthogonal to the
  other tracks; small diff.

### 14.5 Golden-output safety net

`pass.lnastfmt` grows into a validator + normalizer (§5). Before any
change, snapshot the current three-producer output for the test corpus
into `*.expected` goldens. Each migration PR:

1. Runs `producer | lnastfmt | lnast.print` and diffs against goldens.
2. For PRs that intentionally change goldens (e.g., fixing §1 drift),
   call out the diff in the PR description and re-bless.

Acceptance criterion for "migration complete": all three producers emit
byte-identical LNAST for a shared canonical corpus (small-Pyrope).

### 14.6 Risk: goldens codify current bugs

The current `__bits`/`__ubits` split and prefix conventions are
inconsistent (per §1). The initial golden baseline will encode those
inconsistencies. Expect ~10% of golden diffs during migration to be
"this was wrong before; it's right now" — requires reviewer judgment.

---

## 15. Plan: `delay_assign` offset extensions

*Status:* §15.2 `delay_assign` node landed (offset=1 lowered via a wire
placeholder). Both `inou/pyrope` and `inou/slang` producers emit
`delay_assign`. §15.1 (`__create_flop` → sticky attribute) done.

### 15.2 ext — full offset semantics (pending)

**Node shape** (already in `lnast/lnast_nodes.def`):

```
delay_assign:
  child 0: ref    (dst — always a fresh tmp)
  child 1: ref    (src — a declared variable name)
  child 2: const | ref   (offset — must resolve to a comptime constant)
```

**Semantics (target, not yet implemented):**

- Positive offset = future / deferred read. Negative offset = past cycle.
- Offset `0` is legal **only if `src` is a reg**, and reads the flop Q pin
  (pre-update value at cycle start). For a non-reg, offset `0` is a compile
  error — lnastfmt rejects.
- Offset `1` on a reg reads the D / next-cycle value (the `::[defer]` case
  today encoded as offset=1). Offset `1` on a wire reads the settled
  end-of-block value.
- Offset operand may be a `const` literal or a `ref` to a comptime-const
  variable (e.g., `xx = 3; a@[xx]`), but must *not* depend on runtime input.
  Resolution happens during constant folding; lnastfmt flags if the offset
  is still non-const after folding.
- Surface mapping:
  - `a::[defer]`  → `delay_assign tmp a 1` (non-reg) or `1` (reg = next-cycle D)
  - `a@[N]`       → `delay_assign tmp a N`
  - `a@[-1]`      → `delay_assign tmp a -1`
  - `a@[]`        → `delay_assign tmp a 1`  (shorthand for defer)

**Semantics (currently implemented):**

- Offset=1 on a wire: lowered by `process_ast_delay_assign_op` as a wire
  placeholder (Or-identity node).
- Any other offset: `Pass::error` — full offset semantics still pending.

**SSA rules:**

- `dst` is always a fresh tmp; the SSA pass does not subscript it further.
- `src` references the **pre-SSA declaration name**. Today this works
  because `get_vname` returns token text (pre-SSA) and the SSA subscript
  lives in `Lnast_node::subs` separately. The TODO's intent of "SSA pass
  must skip child 1 of `delay_assign` when renaming" should be explicitly
  enforced once tmp identity switches to `Tree_index` (§13).

**Files affected (pending):**

- `inou/prp/prp2lnast.cpp` — emit `delay_assign` for `::[defer]`, `@[N]`,
  `@[-1]`, `@[]` (none of these work today).
- `pass/lnast_tolg/lnast_tolg.cpp` — extend `process_ast_delay_assign_op`
  to handle offset=0 (reg Q pin), offset=-1 (past cycle), ref offsets.
- `pass/lnastfmt/pass_lnastfmt.cpp` — validations:
  - child 2 must be comptime-const after folding; reject runtime refs
  - offset `0` only when `src` has `storage = reg` (needs §10 ST)

**Test coverage** (still to add to `lnast/tests/ln/`):

- reg Q-pin read at offset 0
- defer on a wire (offset 1, non-reg) ← currently the only lowered case
- past cycle on a reg (offset -1)
- comptime-ref offset (`xx = 3; a@[xx]`)
- reject: offset 0 on a wire
- reject: runtime-ref offset

---

*Anything added here should come with a file:line pointer so the next pass
through can tell what's still true.*
