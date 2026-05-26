# TODO — Pyrope semantics

Pending work to finish correct Pyrope language handling: frontend, LNAST
production, attribute/type semantics, memory/register lowering, and test
infrastructure for the Pyrope path.

Items use the same Group N letters as the master plan in [TODO.md](TODO.md).
Items in the same group can be done in parallel; all letters in group N must
complete before group N+1 starts. Group letters are shared across
[TODO_prp.md](TODO_prp.md), [TODO_verilog.md](TODO_verilog.md), and
[TODO_livehd.md](TODO_livehd.md), so cross-file dependencies stay visible.

## Group 1 — foundation

- **1n** Vector / multi-dim arrays: declaration `mut data:[N][M]T = 0`,
  element access `data[i][j]`, iteration over ranges
  (`for a in 0..<4`) and tuple-of-indices (`for a in (0,1,2,3)`).
  Type is ignored at this stage (same treatment as current comptime
  path — the typesystem entry [[1v]] hooks element-type/bits checks
  in later).
  - **Shape flattening:** map multi-dim `[N][M]T` to a single flat
    vector of size `N*M` (row-major); `data[i][j]` lowers to
    `data[i*M+j]`. One canonical representation downstream.
  - **Initialization + read forwarding (non-reg):** when the vector
    is not a register, comptime-known writes are forwarded directly
    to comptime-known reads (constprop). LGraph has no non-reg
    vector cell, so for the non-comptime non-reg case the result is
    still the same shape as the `__memory` initialization — i.e.
    treat it as a memory initialized with the static contents, then
    let comptime fold reads against that init.
  - **Initialization + read wiring (reg):** when the vector is a
    register array/matrix, lower to a LGraph `__memory` cell; wire
    the memory's read ports to the users at their use sites and the
    write ports from the assignment sites. Reset/init values feed
    the memory's initial contents.
  - **Equivalence:** comptime vector/matrix and `__memory`
    initialization share the same value-shape; the only difference
    is whether the result materializes a `__memory` cell or stays
    folded into the readers.
  - Fixes failing comptime test: `prp-matrix`.
  - Add more vector/matrix tests/samples (e.g. ragged shapes,
    higher-rank, mixed range/tuple iteration, partial-row
    assignment, reg-array reset/init, mixed comptime+runtime
    indices).
  - Coordinate with [[3j]] (Pyrope Memory / `Fflop`): that entry
    covers the broader memory story (ports, multi-clock, async
    reset); this entry is the vector/array-shaped subset that lands
    in Group 1.

- **1p** Unknown bits in literals + constprop propagation. Distinct from
  validity (which is toggled by `nil`, not covered here): a value with
  `?` bits is still **valid**, it just has some bits whose value isn't
  known at comptime. Parse `0sb?` (all bits unknown), `0sb1?01?`
  (specific bits unknown), `0ub?1?0`, etc., and propagate unknown bits
  through LNAST + constprop:
  - `v = 0sb?`; `(v != 0) == 0sb?` — a comparison whose answer
    depends on an unknown yields an unknown.
  - `v + 0 == 0sb?` — width-preserving arithmetic.
  - `v + 1 == 0sb??` — carry expands the unknown across one more bit.
  - Once a known value is assigned (`v = 100`), unknowns clear and
    normal evaluation resumes.
  - Fixes failing comptime test: `prp-valid_simple` (which exercises
    unknowns, not validity).
  - May require Dlop work to carry per-bit unknown-mask metadata on
    constprop values; treat any required Dlop change as part of this
    entry rather than a separate one.

- **1q** Match-expression arm syntax: support `case <expr>` and
  `== <expr>` arm prefixes inside `match` (in addition to the
  bare-expr arm form already supported). All three forms desugar to
  the same LNAST node and the existing comptime `match` evaluation
  handles the rest. Pure frontend grammar + LNAST normalization.
  - Fixes failing comptime test: `prp-expr_match` (only uses
    equality comparisons on comptime selector values — within
    Group 1 scope).

- **1s** Expose `graph/cell.hpp` cell ops to Pyrope as `__xxx` function
  calls, with constprop understanding the semantics. General mechanism:
  any `Ntype_op` can surface in Pyrope as `__name(...)` (matching the
  cell's name), maps 1:1 to that cell at LNAST→LGraph time, and is
  constant-folded by constprop when inputs are comptime. Most ops
  already have LNAST nodes; this entry formalizes the surface +
  fills the gaps.
  - Add `Hotmux` to `graph/cell.hpp` (no Ntype_op slot today): similar
    to `Mux` but uses 1-hot `sel` encoding. Runtime/simulation flags
    a "non-one-hot select" error. Pick an even (non-loop-last) slot
    consistent with `cell.hpp`'s bit-0 encoding invariant.
  - `__hotmux(sel, a, b, …)` exposed in Pyrope, lowers to the new
    `Hotmux` cell; constprop folds when `sel` is comptime + one-hot.
  - `unique if cond1 { … } elif cond2 { … }` — keep semantic that at
    most one branch is true; lower so the optimizer can produce a
    one-hot mux. Comptime path validates the uniqueness invariant.
  - `optimize(<bool>)` — **no-op for now**. It's a synthesis hint;
    parse and discard (or lower to a verifier-side check that doesn't
    block compilation). Leave full handling for later.
  - Fixes failing comptime test: `prp-formux`.

- **1t** Comprehensive comptime + constprop tests for every
  `graph/cell.hpp` op exposed via the `__xxx` mechanism in [[1s]]:
  one `.prp` per op (or grouped by family — arithmetic, shift,
  reduction, compare, mux, mask, memory/flop) exercising
  comptime-known inputs and asserting the folded value. Builds the
  baseline that future equiv tests ([[4i]]) compare against once
  `lnast_to_lgraph` lands.

- **1u** `comptime` attribute query + parameter constraint:
  - `x.[comptime]` — query that constprop must resolve to `true` /
    `false` by end of upass. Rule: if any reachable definition of `x`
    is comptime-known, the query is `true`; otherwise `false`. If
    constprop hasn't replaced the query with a literal by end of
    upass, that's a bug — fail loudly. **Exception:** when `x` (or
    any def feeding it) carries a pending-import poison marker
    ([[2o]]), the query is also poisoned and stays unresolved until
    the next `pass.upass` invocation after the import clears.
  - `x::[comptime]` (declare-side / parameter constraint) — sugar
    for `cassert(x.[comptime])`. Same constprop machinery; if the
    cassert can't prove true, hard compile error at the binding
    site. Pending-import poisoning defers the diagnostic the same
    way `cassert` does.
  - `reg x:u32` declaration in the test is a `Flop` driver; its
    `.[comptime]` resolves to `false`.
  - Fixes failing comptime test: `prp-attributes`.

- **1r** Function-call return destructuring with rename:
  `mut (b, c) = dox(a=3)` (bind by return-field name, not position —
  `(c, b) = dox(a)` is still name-driven) and explicit rename
  `(x=dox.b, y=dox.c) = dox(a=3)`. Both `mut` and re-binding forms.
  Pure bundle/constprop work, symmetric with the named-arg
  binding already handled on the call side.
  - Fixes failing comptime test: `prp-fcall5`.
  - A `fcall5b.prp` test will be added covering additional cases.

- **1o** String literals + escape sequences in the lexer / `prp2lnast`:
  full double-quoted escape set (`\n`, `\t`, `\\`, `\"`, `\xNN` hex,
  `\uNNNN` if planned) and raw single-quoted strings where backslashes
  are literal (`'hello\n'` is 7 chars, not 6). String interpolation
  `"{expr}"` (passing today) must keep handling escapes inside
  interpolated segments.
  - Fixes failing comptime test: `prp-doc_basic2`.
  - Grammar fix only if needed — current expectation is the gap is on
    the `prp2lnast` text-extraction / unescape side, not in
    tree-sitter (see memory [[tree-sitter-pyrope-hidden-tokens]]).

- **1m** Pyrope `import` statement: parse `import path` /
  `import path as alias`, resolve module paths against project layout,
  load referenced `.prp` files into the same LNAST forest, and expose
  the imported tuple as a bundle entry in the importer's scope.
  Cross-tree references go through `tree_ios` ABI ([[2n]]). The
  pending-marker / second-phase resolution semantics live with [[2o]]
  (func_extract), not here.
  - Tests to add (none exist yet):
    1. Import a remote already-resolved `comb`/`pipe`/`mod` function
       (single-phase happy path — importer sees a fully-typed
       `tree_ios` ABI from the imported module).
    2. Import a remote `comb`/`pipe`/`mod` that needs the second
       phase (pending-import marker is set, importer can't comptime
       its `if`/sticky propagation until the import resolves on the
       next `pass.upass`). Pairs with [[2o]].
    3. Import a constant (no `tree_ios` ABI, just a bundle entry
       with a comptime value).
    4. Import a "flat" Verilog module via `pub`-keyword exports
       (how does `pub` mark Verilog-side exports, and how does the
       Pyrope side bind them — likely a `tree_ios` ABI variant or a
       sub-class of import that bypasses the second-phase path).
- **1x** Function-scope closure-capture rule, no-import case. A
  function body may reference an outer-scope name **only** when that
  name's bundle entry is comptime-constant by the time the bundle
  pre-pass reaches the function body.
  - Outer name is comptime → inline the constant at the use site
    (bundle/constprop already does this for regular comptime refs,
    so "captured from outer scope" needs no separate path).
  - Outer name exists but is non-comptime → hard compile error at
    the function body's read site (do not silently inline, do not
    let func_extract spawn it).
  - Verifier should also catch any lingering outer-scope ref inside
    a spawned function body that survived to the read-only phase.
  - The pending-import / tainted-outer-name path lives in [[2j]].
  - Exercised by `fcall6.prp` (passing functions as first-class
    arguments to other functions and to tuple methods — clean
    closure-capture surface without imports).

- **1w** Unify `attr_set` / `tuple_set` variadic-path shape —
  `docs/contracts/lnast_spec.md §11`. Shared layout for both ops
  (root + path elements + terminal): `tuple_set root p1…pN value`
  writes into the tuple slot; `attr_set root p1…pN attr_key value`
  writes into the attribute, with the last non-value child always
  the attr key. `assign` stays distinct (§11.3 decision).
  Plus semantic rules `attr_set` must enforce: write-once per
  `(target, attr_key)`, comptime-resolvable guards/values, no
  `attr_set` after a read of the target. Pure LNAST-node-shape +
  validator work, independent of the typesystem.

- **1v** LNAST basic typesystem + min/max upass: handle `int` vs `bool`
  vs `string` as the core types. For `int`, track `min`/`max` (inferred
  or declared); `sign`/`bits`/`max`/`min` are all compile-time
  type-info, derived here. If no type is inferred or declared,
  `bits`/`max`/`min` queries fail with a compile error.
  - Subsumes what was previously a separate "bitwidth upass" entry —
    ranges and the range arithmetic for wrap/saturate computation
    live in this typesystem pass, not in a runtime/codegen pass.
    There is no separate runtime bitwidth pass.
  - Subsumes the standalone "type-check pass" too: no int/bool mixing,
    bitwidth size checks, tuple/enum consistency are all enforced
    here.
  - Downstream consumers (bit-level ops [[2q]], `tree_ios` [[2n]],
    wrap/sat [[2m]], etc.) all read from this entry.

- **1k** `self` / `ref self` as the first parameter of comb functions stored
  inside tuples (setters, getters, methods). Bundle/constprop binds `self`
  to the enclosing tuple instance at the call site (`x.method(a)` →
  `method(self=x, a)`); `ref self` is a mutating bind so the caller's
  bundle entry is rewritten on return (setter semantics); `self.field`
  reads/writes resolve against the bound bundle, not outer-scope capture.
  - Fixes failing comptime tests: `prp-setter_complex`,
    `prp-tuple_decorator_complex`.
  - Add more setter/getter test diversity beyond the current two
    (e.g. multi-field setter, getter returning a tuple, chained
    setter→getter, ref-self method mutating multiple fields) to exercise
    the binding rule in more shapes than the existing tests cover.

## Group 2 — depends on Group 1

- **2q** Bit-level operators in LNAST + constprop: bit-range read
  (`x#[3..=6]`), bit-range write (`r#[0]=…`, `r#[3..=6]=…`,
  `r#[0..+1]=…`), reductions (`#|[..]`, `#&[..]`, `#^[..]`, `#+[..]`),
  sign/zero extension (`#sext[lo..=hi]`, `#zext[lo..=hi]`), and the
  `.[bits]` attribute query (e.g. `for i in 0..<x.[bits]`).
  - Depends on [[1v]] (typesystem upass): `.[bits]`/`max`/`min` are
    published by the typesystem; bit-range ops also need the target's
    width to validate ranges.
  - Fixes failing comptime tests: `prp-bitreduce`, `prp-bitreverse`,
    `prp-bitset`.

- **2p** Source-derived SSA / tmp names (`foo_l42_a`, drop `___N`) —
  `docs/contracts/lnast_spec.md §13`. May reuse the source-map ID from
  TODO_livehd.md [[1f]] as the encoded location handle.
  - Depends on [[1f]] (source-map indirection): the naming scheme can
    reference the source-map ID rather than re-encoding `file:line:col`
    in every temp name.
  - Cascades into golden-file updates ([[1h]] and TODO_verilog.md
    [[3a]]); land before those goldens are taken.

- **2o** Two-phase `upass/func_extract` (parallel per-LNAST, then top-down
  resolve) — `import.md`. Depends on the `import` feature landing (no
  standalone entry yet — likely needs one); without imports the
  pending-marker machinery has nothing to gate on.
  - Pending-import poison for the planned `bundle` pre-pass: when an `if`
    condition (or any value sticky/attribute propagation reads) depends on
    a name whose Bundle entry is still unresolved because of a pending
    `import`, the entry must carry a `pending: ImportRef` marker. Bundle
    pre-pass treats a pending-tainted cond as non-comptime *and* must NOT
    propagate sticky/attribute state into the if's arms (the import may
    still inject an `attr_set` or change the cond's comptime value).
    Equivalent rule for attribute propagation through expression ops:
    skip the migration when any RHS ref carries the pending marker, since
    the import may change which `_*` attributes flow in. The pending
    marker is propagated by bundle pre-pass through the same expression-op
    migration path used for sticky `_*` attrs (functionally a second
    sticky channel).
  - SSA-flatten gate: the SSA post-pass owns the `tuple_*` → flat-name
    lowering and rewrites the LNAST (the optimization passes need the
    Bundle ptr during their loop, so flattening earlier would complicate
    `does`/`==`/etc.). SSA must NOT flatten a Bundle whose entry carries
    a pending-import marker — the import may still change the shape,
    field values, or attached attributes. Such bundles stay as
    `tuple_*` nodes for the next `pass.upass` invocation to revisit.
    Re-runs of `pass.upass` after the import resolves clear the marker.

- **2n** `tree_ios` leaf ABI with `bits`/`min`/`max`/`signed` slots,
  mimicking `GraphIO` for consistency — `import.md §Function signatures`.
  Downstream upasses (func_extract, constprop) read leaf IO via the ABI
  rather than poking into each LNAST.
  - Depends on [[1v]] (typesystem upass): typesystem publishes
    `bits`/`min`/`max`/`sign`, which the ABI carries.

- **2l** Enum support: parse `enum X = (l0, l1, l2)` and `enum X:Base = (...)`,
  lower to LNAST as comptime-tagged tuples; value-assigned variants (`b=5`,
  auto-increment after), the `in` operator (`x in Enum`), typed associated
  values, and the inline-decl form (`const Color = enum(...)`).
  - Basic enums are nearly independent, but complex enums depend on
    [[1k]] (`self` / `ref self`) since associated tuples carry
    setters/methods — so this lands in Group 2.
  - Fixes failing comptime tests: `prp-enum_simple`, `prp-enum_hier`,
    `prp-enum_color` (depends on [[1k]]), `prp-enum_types` (interplay
    with `does`).

- **2j** Function-scope closure-capture rule, **import-aware case**.
  Companion to Group 1 [[1x]] which handles the no-import portion. The
  pending-import path: when a referenced outer-scope name is tainted
  by a pending import, do NOT inline; leave the ref intact and defer
  the function from func_extract spawn until a re-run of `pass.upass`
  (after import resolves) clears the taint. Depends on [[1m]] (import)
  and [[2o]] (pending-import poison + bundle pre-pass).

## Group 3 — depends on Group 2

- **3k** Automate doc-example ingestion: `extract.rb` (or equivalent)
  pulling `cassert` examples from `docs/docs/pyrope` into
  `inou/prp/tests/docs/`, with CI iteration until they pass. Most have
  been imported manually already; this entry is the remaining
  automation + ongoing sync, not urgent.

- **3g** Memory port tuple semantics + `pipestage` `lat`/`num` —
  `docs/contracts/attributes_spec.md §6`.
- **3j** Pyrope Memory / `Fflop` / clock-reset end-to-end: declaration
  syntax, lowering to LGraph `Memory` cell, multi-clock-domain checks,
  sync/async reset trees.

## Group 4 — depends on Group 3

- **4i** Golden-output baseline in `inou/prp/tests/equiv/`: feature-broad
  equiv tests with LNAST/LGraph golden dumps so semantic regressions
  surface as diffs. Depends on TODO_verilog.md [[3a]]
  (`lnast_to_lgraph` finished + Verilog round-trip), since these are
  equiv tests for the lnast→lgraph path, not compile-time-constant
  checks. Each failing-comptime-test category (enums, bits, wrap/sat,
  matrix, valid, strings, match-case, fcall-rename, mux/unique-if,
  comptime attribute) gets at least one equiv counterpart added once
  its feature entry lands.

- **4a** Test functionality on top of slop generation (`test::[…]` blocks,
  runtime harness).
- **4b** Emit `assert` / `assume` / `coverpoint` into slop.
- **4h** Compile-error test infrastructure: harness that pins expected errors
  / warnings to expected source spans (golden-error files), consumed by
  `inou/prp`, upass, and lgraph-pass diagnostics from
  [TODO_livehd.md](TODO_livehd.md) **3f**.
