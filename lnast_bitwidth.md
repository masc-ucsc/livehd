# LNAST Bitwidth Upass Plan

## Summary

Add a new LNAST `bitwidth` upass that registers as a regular
`uPass_plugin` and runs as an **optimizer** step in `pass.upass`,
alongside `attributes`, `constprop`, `coalescer`, `func_extract`, and
`ssa`. It computes value ranges (`max`/`min`) on LNAST before
`pass.lnast_to_lgraph`, using the existing LGraph bitwidth pass only as
behavioral inspiration.

The pass publishes `max`/`min` as HHDS attributes on LNAST nodes/results.
`bits` and `signed` are derived from `max`/`min` on demand — they are
not stored separately. The HHDS attrs are preserved across per-node
step iterations and across outer `pass.upass` re-invocations: a register
declared without an initial bound (`register foo = 0`) can be tightened
on a later sweep once downstream constraints become known.

`pass.lnast_to_lgraph` reads these attrs to populate LGraph node bits at
creation. This makes the "first translator should mostly defer to LGraph
bitwidth" guidance in `docs/contracts/lnast2lgraph.md:305-317` obsolete
the moment this pass lands — the contract flips at that cutover.

## Pipeline integration

- `bitwidth` is an **optimizer** (writes HHDS `max`/`min`, never alone
  determines control flow). It registers alongside the other optimizers.
- Readers (`verifier`, `assert`) attach only to the final outer
  `pass.upass` invocation. While `import` is unimplemented, every
  invocation is "final" by definition, so they run by default today.
- The runner does a single tree walk per `pass.upass` call. At each
  node, every registered pass dispatches; if any pass `mark_changed`s,
  the runner re-dispatches the *other* passes on the same node, bounded
  by `max_iters` (expected 0–3 iterations). A pass does not re-dispatch
  itself unless a different pass subsequently modifies state it depends
  on.
- A second `pass.upass` call (TOP-rooted, top-down) is only required
  when an unresolved `import` function call remains in the staged
  LNAST. The "TOP" is the existing `top:xxx` entry-point convention
  (same as `inou.yosys.tolg top:foo …`). The second-call mode is not
  yet implemented because `import` does not exist; today every program
  completes in a single sweep.
- `order=` remains a debug/test escape hatch.
- External/imported call outputs start as unbounded ranges. If
  downstream constraints make them finite, proceed. If an
  unconstrained output reaches a point that needs concrete bits, emit
  a diagnostic requiring output bits or specialization. This surfaces
  only via the future TOP-rooted second pass; before `import` lands,
  every external call is inlined by `func_extract`.

## Implementation

- Add `upass/bitwidth` as a new registered upass plugin.
- HHDS attributes published on LNAST nodes/results:
  - `max` and `min` (signed unlimited-precision integers, with
    explicit `+inf` / `-inf` flags for the unbounded ends of the
    lattice).
  - `bits` / `signed` are **not** stored — they are derived from
    `max`/`min` on demand. Single source of truth; lets a later
    iteration tighten without invalidating cached views.
- Use an internal LNAST range lattice with finite `min`/`max` plus
  `-inf` / `+inf`, rather than reusing `Bitwidth_range::overflow`.
- Boolean / logical / comparison results use signed one-bit semantics:
  range `[-1, 0]`.
- Explicit type/bit/range attributes narrow inferred ranges. If a value
  cannot fit and no wrap/saturate policy applies, report an error.
- Move **all** `max`/`min`-dependent operations out of `uPass_attributes`
  into `bitwidth`. This includes wrap/saturate narrowing math
  (currently in `upass/attributes/upass_attributes_phase5.cpp`) and
  any other ops that need bits/range information to evaluate.
  - `attributes` continues to record wrap/saturate **policy** ("this
    var uses wrap on assign") and answers attribute presence reads
    (e.g. `x.[wrap]` → `true`).
  - `bitwidth` applies the narrowing to constants and inferred ranges
    using the policy bit from `attributes`.
  - A future "type check" pass (inside `attributes` or a separate
    plugin — open question) will own type-only operations that don't
    require `max`/`min`. The split is: `attributes` owns presence /
    policy; `bitwidth` owns value math; the type-check pass owns
    static type identity / compatibility.
- Promote `uPass_ssa` to a regular `uPass_plugin` so SSA runs on
  every LNAST (not just `func_extract`-spawned bodies). Tuple-shape
  unknowns at unknown-type function boundaries stay conservative
  until the future TOP-rooted second pass.
- Update `pass.lnast_to_lgraph` to read LNAST HHDS `max`/`min` attrs
  when creating constants, ops, graph inputs, graph outputs, muxes,
  and call/subgraph boundaries. LGraph nodes get bits/sign populated
  at creation; bit attributes do not need to be preserved separately
  in LNAST generation.

## Iteration and mark_changed

- A pass calls `mark_changed()` when it writes a new HHDS attr or
  rewrites a node.
- After a step at node N, if any pass marked changed, every *other*
  registered pass re-dispatches at N. The marking pass itself
  re-dispatches only if another pass subsequently modifies state it
  reads.
- `max_iters` bounds the per-node loop (default 1; tests can raise
  it for diagnostics). Convergence is expected in 0–3 steps in
  practice.
- HHDS `max`/`min` annotations persist across the per-node loop and
  across outer `pass.upass` invocations. They are intentionally not
  cleared at the end of a pass.

## Test plan

- Unit tests for the LNAST range lattice: arithmetic, bitwise ops,
  shifts, masks, sext, comparisons, boolean `[-1,0]`, joins, and
  unbounded narrowing.
- Upass tests for:
  - `x.[bits]`, `x.[ubits]`, `x.[sbits]`, `x.[max]`, `x.[min]` after
    bitwidth runs;
  - bitwidth-triggered constprop re-step at the same node (per-node
    `mark_changed` loop);
  - explicit bit constraints that fit and conflict;
  - wrap / saturate on constants and on non-constant inferred ranges;
  - tightening across outer re-invocation: preserved `max`/`min` HHDS
    attrs let a second sweep bound a register that was unbounded on
    the first.
- `lnast_to_lgraph` tests checking that created LGraph pins receive
  bits from LNAST `max`/`min` annotations.
- Do not modify contract tests or benchmarks; failing contract tests
  imply implementation fixes.

## Doc cutovers triggered by this pass

When `pass.upass bitwidth` lands, the following docs must be amended
in the same change:

- `docs/contracts/lnast2lgraph.md:305-317` — flip "defer to LGraph
  bitwidth"; LGraph nodes get bits at creation from LNAST attrs, and
  LNAST generation no longer preserves `bits` attrs.
- `docs/contracts/attributes_spec.md §Phase 5` — wrap/saturate
  narrowing math moves to bitwidth; the section retains only policy
  recording and presence-read semantics.

`upass/upass.md` is already updated for the single-walk / per-node
`mark_changed` model and for the reader-at-end policy; bitwidth slots
in as another optimizer plugin with no further structural change.

## Assumptions

- The existing LGraph `pass.bitwidth` is not refactored unless a
  concrete bug is found.
- LNAST bitwidth aims for parity with LGraph bitwidth behavior over
  time, including memory/subgraph-facing constraints where LNAST
  represents them.
- Bitwidth is an optimizer that publishes information; it may expose
  constants/ranges that require another optimizer step (e.g.
  `constprop` folding `.[max]`) before final verification. That is
  handled by the per-node `mark_changed` loop within a single walk.
