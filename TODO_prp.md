# TODO — Pyrope semantics

Pending work to finish correct Pyrope language handling: frontend, LNAST
production, attribute/type semantics, memory/register lowering, and test
infrastructure for the Pyrope path.

Items use the same Group N letters as the master plan in [TODO.md](TODO.md).
Items in the same group can be done in parallel; all letters in group N must
complete before group N+1 starts. Group letters are shared across
[TODO_prp.md](TODO_prp.md), [TODO_verilog.md](TODO_verilog.md),
[TODO_livehd.md](TODO_livehd.md), and [TODO_hhds.md](TODO_hhds.md), so
cross-file dependencies stay visible.

## Group 1 — foundation

- **1n** Vector / multi-dim arrays: declaration `mut data:[N][M]T = 0`,
  element access `data[i][j]`, iteration over ranges
  (`for a in 0..<4`) and tuple-of-indices (`for a in (0,1,2,3)`).
  Type is ignored at this stage (same treatment as current comptime
  path — the typesystem entry [[1v]] hooks element-type/bits checks
  in later).
  - **Canonical representation: unnamed positional bundle.** Every
    vector/matrix lowers to a bundle whose entries are ordered by
    position (no field names). One representation serves both
    comptime forwarding and `__memory.init`. Entries with no known
    value use `0sb?` (e.g. `mem[4] = (1, 2, 0sb?, 3)`).
  - **Shape flattening:** map multi-dim `[N][M]T` to a single flat
    bundle of size `N*M` (row-major); `data[i][j]` lowers to
    `data[i*M+j]`. `__memory` itself is single-dimension only —
    matrices always flatten before lowering.
  - **Power-of-2 dimension rounding.** If a dimension `N` is not a
    power of 2, round the storage up to the next power of 2 so that
    indexing is a bit-select (cheap in hardware) instead of
    multiplication/modulo (not synthesizable cheaply). The unused
    high slots hold `0sb?` (never read on the legal index range).
  - **Initialization + read forwarding (non-reg):** when the vector
    is not a register, comptime-known writes are forwarded directly
    to comptime-known reads (constprop). LGraph has no non-reg
    vector cell, so for the non-comptime non-reg case the bundle is
    still the same shape as the `__memory` initialization — i.e.
    treat it as a memory initialized with the static contents, then
    let comptime fold reads against that init.
  - **Initialization + read wiring (reg):** when the vector is a
    register array/matrix, lower to a LGraph `__memory` cell; wire
    the memory's read ports to the users at their use sites and the
    write ports from the assignment sites. The same positional
    bundle feeds the memory's `init` port.
  - **Equivalence:** comptime vector/matrix and `__memory`
    initialization share the same positional-bundle shape; the only
    difference is whether the result materializes a `__memory` cell
    or stays folded into the readers.
  - Fixes failing comptime test: `prp-matrix`.
  - Add more vector/matrix tests/samples (e.g. ragged shapes,
    higher-rank, mixed range/tuple iteration, partial-row
    assignment, reg-array reset/init, mixed comptime+runtime
    indices).
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/matrix_partial.prp` — partial-row
    assignment, mixed range/tuple iteration on the same matrix,
    3-D row-major flatten check.
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
  - **The unknown-bit semantics live in `../hlop`, not LiveHD.**
    LiveHD just consumes whatever the HLOP dlop/slop ops publish; the
    per-bit unknown-mask machinery is added in HLOP. Every arithmetic
    cell in `graph/cell.hpp` must have a matching dlop/slop in HLOP,
    so the cell-set is closed and unknown-mask propagation covers all
    of them (no Group-1 deferral to "yield `0sb?` for ops not
    listed").
  - **Folding rule for comparisons / boolean results.** When the
    result is provably determined despite some unknown input bits
    (e.g. `0sb1? > 0sb01` is definitely true; `0sb?00 == 0sb?01` is
    definitely false), fold to the concrete `true`/`false`. Only
    yield `0sb?` when there is an input assignment of the unknown
    bits that would toggle the answer. Never lie.
  - `v = 0sb?`; `(v != 0) == 0sb?` — a comparison whose answer
    depends on an unknown yields an unknown.
  - `v + 0 == 0sb?` — width-preserving arithmetic.
  - `v + 1 == 0sb??` — carry expands the unknown across one more bit.
  - Once a known value is assigned (`v = 100`), unknowns clear and
    normal evaluation resumes.
  - Fixes failing comptime test: `prp-valid_simple` (which exercises
    unknowns, not validity).
  - Dlop work to carry per-bit unknown-mask metadata is part of this
    entry but lands in `../hlop`, not LiveHD.
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/valid_unknown_bits.prp` — partially-
    unknown literals through bitwise AND/OR masks, carry-expansion,
    and clearing unknowns by reassignment.

- **1q** Match-expression arm syntax: support `case <expr>` and
  `== <expr>` arm prefixes inside `match` (in addition to the
  bare-expr arm form already supported). Pure frontend grammar +
  LNAST normalization.
  - **`case` and `==` are distinct LNAST nodes** — `case` lowers to
    `func_case`, `==` lowers to `eq`. They are *not* aliases:
    - `==` (`eq`) is exact equality — same fields, same values,
      matching shapes.
    - `case` (`func_case`) is a structural-subset match — the RHS
      determines which fields are compared. Example: with
      `(a=1,b=2) case (a=1)` the result is `true` (only field `a`
      is checked); with `(a=1,b=2) == (a=1)` the result is `false`
      (shape mismatch).
    - The two coincide only when the structures are fully aligned
      and all bits are known.
  - The existing comptime `match` evaluation handles the rest once
    arms produce the right LNAST node.
  - Fixes failing comptime test: `prp-expr_match` (only uses
    equality comparisons on comptime selector values — within
    Group 1 scope).
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/match_arms_mixed.prp` — three arm
    syntaxes (`case` / `==` / bare) coexisting in one match, tuple
    selector with `case`, nested match in an arm, selector from a
    comptime comb return.

- **1s** Expose `graph/cell.hpp` cell ops to Pyrope as `__xxx` function
  calls, with constprop understanding the semantics. General mechanism:
  any `Ntype_op` can surface in Pyrope as `__name(...)` (matching the
  cell's name), maps 1:1 to that cell at LNAST→LGraph time, and is
  constant-folded by constprop when inputs are comptime. Most ops
  already have LNAST nodes; this entry formalizes the surface +
  fills the gaps.
  - **Arithmetic cells in `graph/cell.hpp` must stay 1:1 in sync with
    HLOP dlop/slop.** LNAST may be a superset, but every arithmetic
    `Ntype_op` has a matching dlop and slop op; new ops added here
    (e.g. `Hotmux`) get cell + dlop + slop together.
  - Add `Hotmux` to `graph/cell.hpp` (no `Ntype_op` slot today): similar
    to `Mux` but uses 1-hot `sel` encoding. Slop emits a runtime
    assert that `sel` is one-hot at simulation. Pick an even
    (non-loop-last) slot consistent with `cell.hpp`'s bit-0 encoding
    invariant.
  - `__hotmux(sel, a, b, …)` exposed in Pyrope, lowers to the new
    `Hotmux` cell; constprop folds when `sel` is comptime + one-hot.
    For now the LNAST/constprop path lands first; the cell + dlop /
    slop implementations follow shortly under the same entry.
  - `unique if cond1 { … } elif cond2 { … }` — keep semantic that at
    most one branch is true; lower so the optimizer can produce a
    one-hot mux. Slop emits a runtime assert that the conditions are
    one-hot; comptime path validates the uniqueness invariant
    statically when conditions are known.
  - `optimize(<bool>)` — **no-op for now**. It's a synthesis hint;
    parse and discard (or lower to a verifier-side check that doesn't
    block compilation). Leave full handling for later.
  - Fixes failing comptime test: `prp-formux`.
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/hotmux_unique_if.prp` — single-arm
    `unique if` (no `elif`), three-arm `unique if` with `else`,
    `optimize()` parse-and-discard, direct `__hotmux` sweep across
    each one-hot slot.

- **1t** Comprehensive comptime + constprop tests for every
  *foldable* `graph/cell.hpp` op exposed via the `__xxx` mechanism in
  [[1s]]: many small `.prp` files (one per op is fine — easier to
  debug than family-grouped files) exercising comptime-known inputs
  and asserting the folded value. Tests live in
  `inou/prp/tests/comptime/` alongside `cellmap_*.prp`. Builds the
  baseline that future equiv tests ([[4i]]) compare against once
  `lnast_to_lgraph` lands.
  - **Storage cells (`Memory`, `Flop`, `Latch`, `Fflop`, `Sub`) are
    out of scope here.** Constprop is single-cycle for now, so a
    flop / memory has no "folded value" to assert at this stage.
    They get exercised via `inou/prp/tests/equiv` (a future entry,
    paired with the LGraph / slop generation in [[4i]] and [[3a]]).
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/cellmap_misc.prp` — coverage gaps not
    in `cellmap_comb.prp` (Sum subtract pin, Mod, Rand/Xand,
    GE/LE/NE, Sext vs Zext on the sign bit, Mux high-slot,
    Get_mask high nibble, shift-by-zero / mul-by-one identities).

- **1u** `comptime` attribute query + parameter constraint:
  - `x.[comptime]` — query that constprop must resolve to `true` /
    `false` by end of upass. **Rule:** `x.[comptime]` is `true`
    only if a single concrete constant value for `x` can be computed
    at compile time. A runtime-selected value is *not* comptime
    even when every reachable definition is itself a constant —
    e.g. in `x = if runtime_cond { 1 } else { 2 }`, both arms are
    comptime constants but the live value depends on a runtime
    selector, so `x.[comptime]` is `false`. Same for `match` with
    comptime arms but a runtime selector. If constprop hasn't
    replaced the query with a literal by end of upass, that's a
    bug — fail loudly. **Exception:** when `x` (or any def feeding
    it) carries a pending-import poison marker ([[2o]]), the query
    is also poisoned and stays unresolved until the next
    `pass.upass` invocation after the import clears.
  - `x::[comptime]` (declare-side / parameter constraint) — sugar
    for `cassert(x.[comptime])`. Same constprop machinery; if the
    cassert can't prove true, hard compile error. Error reporting
    location can be whatever's easiest to emit for now (call site,
    parameter declaration, or both) — proper diagnostic placement
    is deferred to the future error-reporting redesign.
    Pending-import poisoning defers the diagnostic the same way
    `cassert` does.
  - `reg x:u32` declaration in the test is a `Flop` driver; its
    `.[comptime]` resolves to `false`.
  - Fixes failing comptime test: `prp-attributes`.
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/attr_comptime_query.prp` —
    `x.[comptime]` on derived/tuple/match values, parameter-side
    `n::[comptime]` constraint accepting known-comptime args.

- **1r** Function-call return destructuring with rename:
  `mut (b, c) = dox(a=3)` (bind by return-field name, not position —
  `(c, b) = dox(a)` is still name-driven) and explicit rename
  `(x=dox.b, y=dox.c) = dox(a=3)`. Both `mut` and re-binding forms.
  Pure bundle/constprop work, symmetric with the named-arg
  binding already handled on the call side.
  - **No silent fixes.** When LHS names don't line up with the
    return-field names, that's a hard compile error (Pyrope's
    general rule: errors on anything fixable by changing the
    source). Concretely:
    - LHS names a return field that doesn't exist → error.
    - LHS omits return fields it doesn't want → only allowed via
      explicit rename / explicit drop syntax; the bare
      destructuring `(b) = dox(...)` against a `(b, c)` return is
      a compile error, not a silent drop of `c`.
    - LHS uses positional names that don't match any return field →
      error (no positional fallback when name-driven binding fails).
  - **Explicit rename `(x=dox.b, ...) = dox(...)` is pure syntactic
    sugar.** The `dox` on the LHS is a textual reference to the
    function being called on the RHS — the parser/binder only
    cares that the named field exists in that return. The LHS
    function name must match the RHS call exactly (no using a
    different function name even with the same return shape).
  - Fixes failing comptime test: `prp-fcall5`.
  - A `fcall5b.prp` test will be added covering additional cases.
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/fcall_rename_deep.prp` — rename
    through a 3-level-deep return path, dropping fields,
    cross-function name reuse, rebind-without-mut after a previous
    `mut` destructure.

- **1o** String literals + escape sequences in the lexer / `prp2lnast`:
  - **Double-quoted strings** (cooked): escape set is exactly
    `\n`, `\t`, `\\`, `\"`, `\xNN` (hex). No `\uNNNN` for now —
    out of Group 1 scope. Interpolation `"{expr}"` (passing today)
    must keep handling escapes inside interpolated segments.
  - **Single-quoted strings** (raw): backslashes are literal
    (`'hello\n'` is 7 chars, not 6) **and interpolation is also
    disabled** — `'hello {x}'` is the 11-char literal `hello {x}`,
    no expression substitution. The single quote turns off both
    escape processing and `{…}` interpolation.
  - Fixes failing comptime test: `prp-doc_basic2`.
  - Grammar fix only if needed — current expectation is the gap is on
    the `prp2lnast` text-extraction / unescape side, not in
    tree-sitter (see memory [[tree-sitter-pyrope-hidden-tokens]]).
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/string_escapes.prp` — full `\n`/`\t`/
    `\\`/`\"` escape set, `\xNN` mid-string, raw single-quoted
    strings, cooked-vs-raw size comparison, escapes interleaved
    with `{…}` interpolation.

- **1m** Pyrope `import` statement: parse `import path` /
  `import path as alias`, look the path up by **LNAST tree name** in
  the forest (`find_tree` / `find_io`), and expose the imported
  entity as a bundle entry in the importer's scope. Cross-tree
  references go through `tree_ios` ABI ([[2n]]). The pending-marker
  / second-phase resolution semantics live with [[2o]] (func_extract),
  not here.
  - **Tree-naming convention:** for a file `foo/bar.prp`, every
    `comb`/`pipe`/`mod` defined inside gets its own LNAST tree
    named `foo/bar.<entity>` (e.g. `comb xx() …` → tree name
    `foo/bar.xx`). Trees are created for **both** `pub` and
    non-`pub` definitions — every entity always gets a tree
    because every `comb`/`pipe`/`mod` needs one. The `pub` keyword
    controls *exportability*: only `pub`-marked entities can be
    named by `import("foo/bar.xx")` from another file.
  - **Pub constants / tuples** at file scope (`pub const yy = 33`)
    can't ride a per-entity tree, so the import machinery
    synthesizes a single "file wrapper" LNAST tree per source file
    that returns all of that file's `pub` non-function values.
    Roughly equivalent to writing:
    ```
    comb bar() -> (yy, other_pub_var) {
        yy = 33
        other_pub_var = 44
        xx = import("foo/bar.xx")  // only if `pub comb xx ...`
    }
    ```
    Result is read-only at the import site (matches the "imports
    are constants" semantics).
  - **Cross-file compile order.** A future Group-2 mechanism
    (paired with [[2o]]) will run all files in parallel and iterate
    a pending-import resolution loop until everything resolves
    (with a cycle-detection cap). That mechanism is **not** in
    Group 1. For Group-1 tests, use explicit phase ordering —
    invoke `import_phase1` / `import_phase2` (or whatever the
    test-side directive ends up being) and hand-order which files
    are processed in which phase.
  - **Tests to add (no good coverage today; will need to fix
    and/or create new tests):**
    1. Import a remote already-resolved `comb`/`pipe`/`mod` function
       (single-phase happy path — importer sees a fully-typed
       `tree_ios` ABI from the imported module).
    2. Import a remote `comb`/`pipe`/`mod` that needs the second
       phase (pending-import marker is set, importer can't comptime
       its `if`/sticky propagation until the import resolves on the
       next `pass.upass`). Pairs with [[2o]].
    3. Import a constant (no `tree_ios` ABI, just a bundle entry
       with a comptime value — via the synthesized file-wrapper
       tree).
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
  - **Function-value captures are treated identically to
    constant captures.** When `fn foo(x) { y = bar(x); … }` is
    processed, `bar` must be resolvable to a known function at
    that time (functions are themselves comptime values). If `bar`
    is pending from import, that pending-ness propagates outward —
    `foo`'s body becomes pending too, and nothing is computed
    until the next upass invocation resolves it (the [[2j]] path).
  - Verifier should also catch any lingering outer-scope ref inside
    a spawned function body that survived to the read-only phase.
  - The pending-import / tainted-outer-name path lives in [[2j]].
  - Exercised by `fcall6.prp` (passing functions as first-class
    arguments to other functions and to tuple methods — clean
    closure-capture surface without imports).
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/closure_capture.prp` — direct capture
    of outer comptime `const`s and tuples, derived captures, and a
    captured constant flowing through a higher-order `apply`.

- **1w** Unify `attr_set` / `tuple_set` variadic-path shape —
  `docs/contracts/lnast_spec.md §11`. Shared layout for both ops
  (root + path elements + terminal): `tuple_set root p1…pN value`
  writes into the tuple slot; `attr_set root p1…pN attr_key value`
  writes into the attribute, with the last non-value child always
  the attr key. `assign` stays distinct (§11.3 decision).
  - **`attr_set` is declaration-only.** A statement like
    `a.[foo] = bar` in Pyrope is a **compile error** — attributes
    can only be set at the declaration site
    (`const a::[foo=bar] …`, `mut a::[foo=bar] …`, `type T::[…]`,
    etc.). `attr_get` (`a.[foo]` reads) can appear anywhere.
  - **`tuple_set` is freely re-assignable** (regular SSA-form
    tuple-field updates: `t.x.y = 1; t.x.y = 2` is fine).
  - **Write-once enforcement for `attr_set` runs after constprop**,
    not at `lnastfmt` time. Conditional / mutually-exclusive
    declaration sites may textually look like multiple writes but
    collapse to one once constprop folds dead branches; the
    write-once check can only be honest once that folding has run.
  - Other rules: comptime-resolvable guards/values, no `attr_set`
    after a read of the target. Pure LNAST-node-shape + validator
    work, independent of the typesystem.
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/attr_set_path.prp` — `attr_set` with
    1-element and deep tuple paths, `tuple_set` with a deep path,
    sticky attr propagation through an expression op, mixing
    `attr_set` and `tuple_set` against the same root path.

- **1v** LNAST basic typesystem + min/max upass: handle `int` vs `bool`
  vs `string` as the core types. For `int`, `sign`/`bits`/`max`/`min`
  are all compile-time type-info, derived here.
  - **A type annotation is required** to query `.[bits]`/`.[max]` /
    `.[min]`. The range is driven by the **declared type**, not by
    the current value. Examples:
    - `mut x = 5` (no annotation) → `x.[bits]` / `x.[max]` /
      `x.[min]` is a compile error. No silent fallback to "infer
      from the current value".
    - `mut x:u4 = 5` → `x.[bits] == 4`, `x.[max] == 15`,
      `x.[min] == 0`, regardless of `x`'s actual value at any
      program point.
  - This is a deliberate simplification: the old bitwidth pass
    inferred ranges from values, which was hard to explain (signed
    representation adds bits in many cases and the result depends
    on optimization quality). Declared-type-driven ranges are the
    new contract.
  - **Breaks existing tests** that depend on value-inferred bits /
    max / min. Those need fixing (add explicit type annotations or
    drop the `.[bits]`/`.[max]`/`.[min]` queries) **before** this
    entry can be implemented.
  - Subsumes what was previously a separate "bitwidth upass" entry —
    ranges and the range arithmetic for wrap/saturate computation
    live in this typesystem pass, not in a runtime/codegen pass.
    There is no separate runtime bitwidth pass.
  - Subsumes the standalone "type-check pass" too: no int/bool mixing,
    bitwidth size checks, tuple/enum consistency are all enforced
    here.
  - Downstream consumers (bit-level ops [[2q]], `tree_ios` [[2n]],
    wrap/sat [[2m]], etc.) all read from this entry.
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/typesys_range.prp` — inferred ranges
    from initializers, declared `range=..` narrowing, `.[bits]` vs
    declared bound, bool typed distinctly from int.

- **1k** `self` / `ref self` as the first parameter of comb functions stored
  inside tuples (setters, getters, methods). Bundle/constprop binds `self`
  to the enclosing tuple instance at the call site (`x.method(a)` →
  `method(self=x, a)`); `ref self` is a mutating bind so the caller's
  bundle entry is rewritten on return (setter semantics); `self.field`
  reads/writes resolve against the bound bundle, not outer-scope capture.
  - **Chained receivers bind to the innermost target.** For
    `x.a.b.method(...)`, `self` binds to `x.a.b` (the receiver
    immediately preceding `.method`). `ref self` permits the
    method to modify fields *within* `x.a.b` — it does **not**
    grant write access to the parent slots `x.a` or `x`. The
    `ref` keyword's only effect is to allow mutation of `self`'s
    own fields; without `ref`, the method can only read.
  - The same scoping applies transitively to nested `self.other()`
    calls — the inner method's `self` binds to the outer method's
    `self` (= the original `x.a.b`), so mutations through
    `self.other_method()` (when `other_method` itself takes
    `ref self`) propagate back to the same `x.a.b` at the caller.
  - Fixes failing comptime tests: `prp-setter_complex`,
    `prp-tuple_decorator_complex`.
  - Add more setter/getter test diversity beyond the current two
    (e.g. multi-field setter, getter returning a tuple, chained
    setter→getter, ref-self method mutating multiple fields) to exercise
    the binding rule in more shapes than the existing tests cover.
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/setter_multi_field.prp` — multi-field
    `ref self` setter, getter returning a tuple, chained
    setter→getter, conditional `ref self` mutator, and two
    independent instances of the same tuple type with no
    cross-talk.

- **1y** Comptime-bounded recursion in `comb` bodies (only `comb`;
  `pipe`/`mod` recursion has different semantics and is out of scope
  here). A comb may call itself (directly or via a mutually-recursive
  cycle) as long as every call chain provably reaches a
  comptime-decidable base case during elaboration. The recursion
  unrolls inline at constprop time — each recursive call is inlined
  into its caller the same way a non-recursive comptime call is,
  until the base-case branch is selected and the recursion bottoms
  out. Implementation work:
  - Allow a comb's bound name to be visible inside its own body (and
    inside any mutually-recursive sibling's body) so the self-reference
    resolves; today the binding-order rule treats this as an
    unresolved-name error.
  - **Termination is enforced by a hard per-function recursion
    counter, not by a clever progress check.** Cap each function's
    recursive-inline depth at a hardcoded ceiling (default 10000);
    if the counter trips, fail with a hard compile error naming the
    offending call site. A more sophisticated "args strictly
    decrease" / "structural progress" analysis would be welcome if
    someone designs one, but the counter is the agreed Group-1
    contract — it's simple, correct, and works as long as real
    tests stay under the cap.
  - Structural recursion (a comb that recursively instantiates other
    cells / itself to build a network topology) is the same mechanism:
    each recursive call lowers to a fresh subgraph at the call site,
    and the comptime base case caps the depth. Confirm a balanced
    reduction tree (see test below) lowers to the expected
    log-depth adder tree, not a degenerate chain.
  - Expected test (added, debug pending):
    `inou/prp/tests/comptime/recursion.prp` — `fib` and `fact` as
    pure-value recursion (fibonacci/factorial up to comptime-known
    arguments), plus a recursive balanced `tree_sum` that instantiates
    a binary adder tree by splitting an index range and recursing on
    each half (covers power-of-two, single-leaf base case, asymmetric
    ranges, and sub-range over a larger tuple).

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
