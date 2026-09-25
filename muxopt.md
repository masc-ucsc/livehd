# muxopt — mux-tree optimization TODO

Goal: close useful gaps in cprop and satopt, using Yosys and mux-tree research
as references. This is an implementation plan, not a claim that the proposed
rewrites are implemented or that downstream synthesis cannot recover them.

Reviewed against the working tree on 2026-09-25. All checkboxes below are
pending implementation or validation. Keep this plan in Markdown as requested.

## Review findings that change the plan

1. **Width handling is a prerequisite.** cprop forbids using width hints as
   semantic evidence, but the LEC encoder and simulator explicitly allow a mux
   result to truncate its arms. Copying the narrowest operand width is unsafe.
2. **Hotmux obligations and defaults need separate treatment.** An unproven
   outer Hotmux cannot disappear just because its values become identical.
   A group containing the default is active when *no original control* fires,
   as well as when one of that group's controls fires.
3. **Operand shape is part of the rule.** `Rxor` and `Popcount` have an explicit
   constant bit-count operand; `Ror` has a multiset bank. `LT`/`GT` have two
   banks, not simply two arbitrary positional pins.
4. **Termination does not prove linear work.** Greedy overlapping buckets and
   rescanning new muxes can be superlinear. Charge visits and generated edges,
   and bound candidate matching independently of the gain rule.
5. **A3 needs its own gain rule and local folding.** Ignoring mux nodes, as A1
   does, cannot establish that a mux-only rewrite gains. The scalar sweep has
   already run when mux sharing emits nodes.
6. **B3 has no current public synthesis-profile caller.** Compile and standalone
   satopt run the shared profile. Blindly stamping `kFormalOnehot` there would
   misrepresent an assumption as a proof and weaken observable checks.
7. **A LEC timeout is inconclusive.** A large-design time-limited smoke run can
   be useful, but it cannot satisfy an equivalence acceptance criterion.

## References and existing implementation

- [Yosys `opt_share`](https://github.com/YosysHQ/yosys/blob/main/passes/opt/opt_share.cc)
  and [SAT-based `share`](https://github.com/YosysHQ/yosys/blob/main/passes/opt/share.cc).
  Pin the Yosys revision in measurement results; source line numbers on `main`
  are not stable references.
- [SmaRTLy, arXiv 2510.17251v1](https://arxiv.org/html/2510.17251v1): contextual
  logic inference and mux-tree rebuilding. A cone-size cap is a bound, not an
  implementation of its dependency-based subgraph filtering.
- [Wang et al., ISEDA 2023, “Optimization of Multiplexer Combination in RTL
  Logic Synthesis”](https://ieeexplore.ieee.org/document/10218464/).
  Verify the original draft's Fig. 1–6 correspondence before citing individual
  figures; the identities below are specified independently.
- Pištek et al., 2010, “Optimization of multiplexer trees using modified truth
  table”: retain as a research lead; verify the original paper before claiming
  equivalent coverage. An author-hosted follow-up is
  [“Reduction of Multiplexer Trees using Modified Lookup Table”](https://www2.fiit.stuba.sk/~jelemenska/publikacie/WCIT2011_pistek_multiplexer.pdf).
- Local entry points: [cprop overview](pass/cprop/README.md),
  [`Cprop::do_trans` / `scalar_mux` / `cse_pass`](pass/cprop/cprop.cpp),
  [`Mux_sharing`](pass/cprop/cprop_mux.cpp),
  [`canonicalize_flop_enable`](pass/enableopt/enableopt.cpp),
  [satopt stages and profiles](pass/satopt/README.md),
  [synthesis pipeline](pass/synth/README.md).

## Semantics and common guards

Notation throughout: `mux(s, F, T)` returns `F` when `s == 0`, otherwise `T`.
`!s` means a logical zero test, never unlimited-precision bitwise `Not`.

- Binary `Mux`: select on pid 0, else on pid 1, then on pid 2. Its selector
  may be wide or signed; a nonzero value need not be 1.
- Index `Mux`: preserve every arm index and the out-of-range behavior. The
  current [LEC encoder](pass/lec/encode.cpp) uses the last arm as fallback;
  cross-check simulation and Verilog emission in A0.
- `Hotmux`: interleaved `(control, value)` pairs, optional trailing default;
  absent default means zero. Controls carry a one-hot-or-zero obligation.
  Shared-profile rewrites preserve both values and observable checks.
- Structural exclusivity can come from `kFormalOnehot` or distinct exact
  integer equalities on one selector. The current decoder recognizes i64
  constants; extending that to arbitrary integers is separate work.
- `bits`/signedness hints do not justify cprop algebra. Explicit masks,
  extension operands and Concat lane widths are semantic and must survive.
- Each sink pin has one driver. Use `Ntype::sink_bank` and preserve operand
  multiplicity; raw sink pid is not the bank of a commutative operand.
- Initial scope: uncolored, pure combinational cells without runtime checks.
  Current `Mux_sharing::collect` rejects *all* colored candidates, rather than
  merely requiring equal colors. Supporting colored regions needs a separate
  ownership/metadata audit; never cross a color boundary.
- Preserve latch-Q direct-arm holds and leave flop-Q hold regions to enableopt.
  Exclude state, memory, instances, clock cells, property/runtime-check cells,
  and operations whose required attributes the matcher does not model.
- Preserve named/addressable outputs and source metadata using the existing
  CSE policy. Use cprop's generation-aware forwarding, normalization and
  retirement helpers; invalidate cached facts when replacing or recycling pins.
- Initially skip unknown-valued constants and unsupported/invalid operand
  shapes. Moving evaluation must not introduce an observable invalid operation
  on an inactive path (zero divisors, illegal shifts, etc.).

# Part A — cprop: structural rewrites without a solver

A1's objective remains fewer non-mux operator nodes. Extra data muxes and their
widths are not charged to that objective. This is a chosen heuristic, not a
promise of improved area or delay; report both. Bound graph growth separately.

Target integration in `Cprop::do_trans`, preserving existing pack handling:

`scalar/CSE -> canonicalize_concat_pack -> vectorize_bit_muxes -> A2 prune -> A1 share -> mux_share_pass (+A3) -> vectorize_bit_reductions -> merge_concat_slices -> DCE`

The compile schedule remains `cprop -> bitwidth -> enableopt -> cprop -> bitwidth`.
Newly emitted nodes need local normalization: a second compile invocation is
not guaranteed for standalone cprop, and there is no whole-graph fixed point.

## A0. Resolve semantic and infrastructure prerequisites

- [ ] Reconcile width behavior before enabling A1. Inspect the Mux/Hotmux
  cases in `pass/lec/encode.cpp`, `inou/cgen/cgen_sim.cpp`, Verilog emission,
  and bitwidth inference/rewrite. The first two explicitly fit arms to a typed
  result width; document when that narrowing is legal and how it survives
  moving an operator across a mux.
- [ ] Specify metadata handling for the retained root and each new operand
  mux. Do not copy the narrowest arm's annotation or assume maximum `bits`
  suffices for mixed signs. Derive a lossless carrier when justified, or leave
  intermediate hints unset for inference; retain any required explicit mask
  or extension. If an operation relies on a narrowing boundary that cannot be
  preserved, reject that candidate.
- [ ] Add regressions with narrow mux outputs, differently sized/signed
  operands, negative and wide constants, and explicit `Get_mask`/`Sext`.
  Compare native simulation, emitted Verilog and LEC, so the same encoder bug
  cannot validate both sides unnoticed.
- [ ] Define a reusable operand-shape descriptor and bounded private-region
  ownership walk. Keep pass-local state out of persistent graph attributes.
- [ ] Audit pass ordering with `split_selfref`'s Get_mask distribution,
  `cprop_lowlane`, bitwidth rewrites and pack canonicalization. Check the actual
  compile schedule for rewrite oscillations, not just two A1 calls.

Acceptance: one documented width policy with executable regressions and no
contract-test changes. A0 is required before A1 acceptance.

## A1. Operator sharing through a mux

**Binary rule:** `mux(s, f(P...), f(Q...)) -> f(R...)`, with `R_j = P_j`
when the paired operands match, otherwise `R_j = mux(s, P_j, Q_j)`.
Examples:

- `mux(s, X+A, X+B) -> X + mux(s, A, B)`.
- `mux(s, A<<k, B<<k) -> mux(s, A, B) << k`.
- `mux(s, a&b, c&d) -> mux(s,a,c) & mux(s,b,d)`.

No shared operand is required. Both arm operators must be removable: initially
require one outgoing edge from each distinct operator, feeding this mux's data
arm. Multiple occurrences of one operator need an explicit all-uses-owned
extension; do not count them as multiple removable nodes.

**Matching contract:**

| Operations | Required shape and pairing |
|---|---|
| `Sum`, `LT`, `GT` | Equal arity in each bank. Match common operands by multiset, preserving duplicates; pair remaining operands deterministically within the same bank. Never move an operand between `as` and `bs`. |
| `And`, `Or`, `Xor`, `Mult`, `EQ`, `Ror` | Equal multiset arity. Retain common operands, then pair residuals deterministically. No associativity assumption is needed beyond the cell's defined bank semantics. |
| `SHL`, `SRA`, `Div`, `Rem` | Same op and positional `a`, `b`; either or both may differ, subject to validity guards. |
| `Sext` | Same constant extension-position operand initially; only `a` differs. |
| `Rxor`, `Popcount` | Same nonnegative constant bit count `b`; only `a` differs. These are not unary cells. |
| `Not` | Same unary op. |
| `Get_mask` | Same exact constant mask; mux the data operand. |
| `Set_mask` | Same exact constant mask; preserve `a` and `value` roles. Keep existing same-base lane factoring until the common engine covers its behavior. |
| `Concat` | Same lane count and exact declared width per lane; mux only lane values. |

Constants participate in matching by exact value/representation as appropriate
for existing cprop helpers. Multiple differing pairs are allowed, including
changes on both Sum banks, provided each pair stays within its own bank.
`X+A` versus `X-B` does not match: the bank arities differ. Identity insertion
and inserted negation are outside the initial rule.

**Index Mux extension:** every explicit arm must match the same shape. Create
operand index muxes with identical select/index/fallback semantics. Do not
replace an out-of-range zero/fallback with `f(0, ...)` by accident.

**Hotmux extension, first implement proven/decoded exclusive cases:**

- Partition values by complete compatible shape, including explicit default
  operators. A group must contain at least two distinct removable operators.
- Differing operands get inner Hotmuxes with the group's original controls.
  If the original default is in the group, use its operand as inner default.
  Otherwise use implicit zero only if inactive evaluation remains valid.
- For a group without the default, the outer control is `OR(c_i)` for that
  group. Leave the original default unchanged.
- For a group containing the default, keep the shared result as outer default
  and retain the grouped controlled arms pointing to it. Do not use only
  `OR(c_i)` as its activation: that misses the no-control case. Any later
  collapsing must account for `!OR(all_original_controls)`.
- Remove the outer Hotmux only when all alternatives, including its explicit
  default, are represented by the shared operator and no obligation remains.
  With an implicit-zero default, retain the outer fallback unless the shared
  operator is independently shown to return zero when no control fires.
- Any new exclusive Hotmux needs justified proof provenance. Explicitly retire
  absorbed proven Hotmuxes; generic DCE intentionally keeps obligation cells.

**Unproven Hotmux extension is pending, not enabled by the above rule.**
Preserving the outer controls alone is insufficient: an inner subset Hotmux
must preserve the original arm order to retain priority on overlaps, and it
adds a check. First specify preservation of overlap diagnostics, priority
reference values, named outputs, and inactive-path evaluation. Do
not remove an unproven outer cell or stamp subset controls proven. The initial
implementation leaves these cells untouched.

**Implementation TODO:**

- [ ] Land binary sharing and operand-shape matching after A0.
- [ ] Add index muxes, then exclusive Hotmux groups/defaults as separate steps.
- [ ] Fold the existing paired `Set_mask` rule into the engine only after its
  regressions pass; preserve the separate one-arm lane-update optimization.
- [ ] Use deterministic shape buckets, not overlapping `(shared driver)`
  buckets that omit no-common-operand opportunities. Bound pairing/search;
  a global optimal grouping is not required.
- [ ] A committed rewrite strictly reduces distinct non-mux operator count.
  Track generated mux/control nodes and edges separately. Requeue only newly
  exposed private operand muxes, with ownership/generation checks.
- [ ] Charge operand visits and emitted edges to a pass-wide budget. State
  sorting/hash and large-integer costs explicitly. Operator-count descent
  proves termination, not O(V+E) total work.

**Acceptance tests:** structural operator counts plus exhaustive small control
spaces and LEC; positional one/two-operand changes; duplicate multiset entries;
changes on both Sum banks; width/sign regressions from A0; index fallback;
Hotmux default in/out/absent; unchanged unproven obligations; shared fanout;
colored/runtime-check rejection; latch/flop holds; cascade chains; high-fanin
and 2048-level fixtures with measured visit and generated-size bounds.
Add a repository-owned equiv fixture for the source-to-graph flow.

## A2. Path-condition pruning

Extract reusable boolean/value facts and rollback into a helper such as
`cprop_muxctx.{hpp,cpp}`. Share primitives with enableopt; do not replace its
state-specific reasoning wholesale. Its current walker also traverses `Or`,
`Set_mask` and `Concat` and reasons from flop-enable clauses.

- [ ] Walk disjoint private binary-Mux/exclusive-Hotmux data regions iteratively.
  Stop at shared nodes, checks, colors and state boundaries. Prune only the
  relevant parent edge; never globally replace a value from a path-local fact.
- [ ] Track zero/nonzero boolean conditions through `decode_bool_condition`;
  extend it for `Xor(b,1)` only when `b` is structurally bool01.
- [ ] Add exact selector equalities/disequalities from binary `EQ(sel,k)`.
  `sel == k` decides other constant comparisons; `sel != k` only rules out
  that value. Cap stored facts at 16 per path, counting disequalities too.
  Roll back sibling facts and stop adding facts at the cap without guessing.
- [ ] For binary muxes, the then edge supplies `sel != 0`, the else edge
  `sel == 0`. For exclusive Hotmuxes, an arm supplies its true control and
  default traversal supplies all controls false, subject to the fact budget.
  Index-mux path inference is a later extension requiring exact index rules.
- [ ] Bypass a private mux whose selector is decided. Prune exclusive Hotmux
  arms under the same guards; leave unproven Hotmux controls/checks intact.
- [ ] Replace a data occurrence of a known bool01 base with 0/1. Correct
  example: `mux(s, B, s) -> mux(s, B, 1)` for bool01 `s`;
  `mux(s, s, B) -> mux(s, 0, B)` on the else edge. Nonzero does not imply 1
  for a wide selector, so do not substitute its data value from truth alone.
- [ ] Let mux-region predicate construction consult the same facts to avoid
  building contradictory paths (`s & !s`, `EQ(x,3) & EQ(x,5)`). Retain shared
  predicate DAGs; do not enumerate paths into a sum of products.

Acceptance: nested repeated-select and nested-case fixtures; contradiction and
rollback cases; wide signed/nonzero selectors; fact-cap exhaustion; shared
fanout; state/check guards. Existing enableopt tests pass unchanged.

## A3. Two-group emission using a select tree

Identity: `mux(S, mux(C,A,B), mux(D,A,B)) -> mux(mux(S,C,D), A, B)`.
Current `Mux_sharing` emits shared predicate DAGs and a Hotmux; it does **not**
expand paths into a sum of products. Its all-bool01 bailout prevents predicate
logic from outweighing the saved data selection.

- [ ] For exactly two groups and no hold/check boundary, build predicate `p`
  from the owned mux skeleton: group A terminals become 1, group B terminals
  become 0. Emit `mux(p, B, A)`; preserve fallback semantics for Hotmux nodes.
- [ ] Normalize the new predicate locally, before evaluating gain. In the
  example, the direct select-tree form above avoids gratuitous inversions.
  If boolean inversion is needed, use a zero test or a bool01-safe XOR.
- [ ] Count **all** removable and emitted mux/control nodes for A3, with shared
  nodes counted once. Require a strict total-node reduction after bounded
  folding. A1's mux-free operator metric is not applicable here.
- [ ] Relax the bool01 bailout only for profitable two-group results. Keep it
  for three or more groups until measured separately.
- [ ] No speculative graph residue on rejection; use a planned descriptor or
  explicitly retire temporary nodes and invalidate facts.

Acceptance: exhaustive versions of the identity, asymmetric/repeated controls,
constant groups, equal-cost rejection, exclusive-Hotmux defaults and shared
prefixes. Add a small repository-owned LRU regression; separately measure the
external lhdtrack workload if available. Replace private “memory” references
with saved commands/results before using them as acceptance evidence.

## A4. Boolean inversion and Hotmux CSE

- [ ] Share A2's bool01-safe `Xor(s,1)` decoding with scalar mux inversion.
  `Not(s)` is `-(s+1)`; for bool01 `s` it is never zero and is not logical
  inversion. For arbitrary integers it can be zero (`s == -1`).
- [ ] Add a canonical pair-order-independent CSE key only for exclusive
  Hotmuxes. Current `cse_pass` sorts by `sink_bank`; for positional Hotmux
  pins that retains original pair order. Sort whole `(control,value)` pairs,
  never controls and values independently; keep default distinct.
- [ ] Include every relevant attribute/proof/check distinction and preserve
  CSE's naming/color policy. Reject unproven/check-bearing cells. Do not erase
  an exclusivity obligation while merging an ordinary value node.

Acceptance: reordered exclusive pairs merge; changed default, changed pairing,
unproven overlaps and incompatible metadata do not merge unsafely.

# Part B — satopt: bounded proofs and contextual sharing

## B1. Context-aware select constants: proposed `muxtree` stage

Depends on A2's region/fact primitives and measurements. Proposed position:
before `hotmux` in the existing fixed stage order, after the earlier value/ODC
stages (B2 would follow B1). Explicit stage selection remains supported.

- [ ] Target binary-Mux selects and exclusive-Hotmux controls inside private
  regions. Form a boolean path predicate `P` from at most four ancestor facts.
  Dropping additional conjuncts weakens the premise and is conservative;
  record the exact premise used. Include Hotmux default conditions correctly.
- [ ] Word_sim nominates a constant only using columns satisfying `P`.
  Conflicting samples reject it; zero matching samples are not evidence of
  constancy. Initially skip those candidates or make a separately budgeted
  reachability query. Simulation never establishes the rewrite.
- [ ] Prove `P -> (sel == 0)` or `P -> (sel != 0)` for a binary mux.
  The latter does not require `sel == 1`. Use a dedicated contextual query or
  a query-local implication expression; current `Prover::is_true` accepts a
  pin, not an arbitrary formula. Never leak path assumptions to later queries.
- [ ] On Proven, rewrite only the observed parent edge/owned region, following
  A2's guards. Unknown, Refuted, unsupported cones and budget exhaustion do
  not rewrite. Record counterexamples for later nominations.
- [ ] Defer unproven Hotmuxes initially. Any extension must preserve every
  control and check cone: a contextual fact cannot globally tie a control.
  Existing `apply_selects` ties controls using **global** proofs; it cannot be
  reused unchanged for this purpose.
- [ ] Include the target, premise, region exits, profile, graph identity and
  proof options in cache identity. Invalidate/rebuild prover and simulation
  state after mutations; no stale proofs after rewiring or pin reuse.
- [ ] Wire stage enumeration, parsing, defaults, ordering, budgets, reports and
  cache serialization. Report candidates, rejects, queries, proven/refuted/
  unknown, applied, budget skips, work and node changes.

Acceptance: correlated but structurally undecided selects, wide selectors,
empty sample sets, unreachable contexts, sibling-context isolation, preserved
checks, forced budget/Unknown paths and cold/warm cache agreement. Measure the
incremental gain over A2 and ABC separately. `all_regions=false` is useful only
where color information exists; do not enable a cross-color filter in an
uncolored compile flow and silently filter out every candidate.

## B2. Sharing operators under exclusive activations: proposed `share` stage

Depends on A1's shape/width policy and B1's bounded contextual infrastructure.
This covers operators whose consumers are in different mux regions, beyond
A1's common-mux pattern. Initial stage position: after `muxtree`, before
`hotmux`, with normal satopt budget accounting.

- [ ] Collect **all** uses of each candidate output, including graph outputs,
  state updates, named/opaque consumers and checks. Derive an activation that
  over-approximates every observable use. OR multiple supported path conditions;
  an unsupported use rejects the candidate. Exclusivity of only one use is
  insufficient.
- [ ] Bucket compatible shapes and bound pair attempts, fanout walks and
  activation DAG size. On a walk cap, reject or conservatively treat the
  operator as always active; never drop an unvisited use.
- [ ] Nominate when simulation sees no overlap; prove
  `are_exclusive({act1, act2})`. Both-false behavior is unobserved only after
  the all-use analysis above and the common invalid-evaluation guards.
- [ ] Replace paired operands with `mux(act1, Q_j, P_j)` and rewire the owned
  consumers to one shared operator. Recheck actual removed/added nodes.
- [ ] Prevent cycles through **both controls and operands**. Neither selected
  activation nor any new operand dependency may reach either replaced output.
  A bounded reachability check can establish this; a bare topological-number
  comparison is not sufficient. Reject unsupported cyclic regions.
- [ ] Reuse width, metadata, check, state and color guards from A1. Aggressive
  mode relaxes profitability filters only, never correctness guards.
- [ ] Treat width thresholds as tunable LiveHD heuristics: initially consider
  `Mult`/`Div`/`Rem` at width >=4, variable `SHL`/`SRA` at width >=8, and an
  operand-width ratio <=2. These are not a verified exact copy of Yosys policy.
- [ ] Add proposed option `pass.satopt.share.aggressive` only with parser/help,
  cache-key and test updates. Add the same report/cache/budget integration as B1.

Acceptance: exclusive activations share; overlapping or unaccounted uses do
not; activation/operand dependency cycles are rejected; unreachable/invalid
paths retain behavior; forced solver failures leave the graph unchanged.
Use small wide-arithmetic fixtures plus separate area/delay/runtime benchmarks.

## B3. Synthesis-only exclusivity assumptions: design/integration required

The existing synthesis profile permits ignoring obligations. However, the
current public satopt callers use `Profile::shared`, and synthesis maps the
compiled graph. This is not a one-line satopt change.

- [ ] Identify a concrete caller on the private synthesis copy, with both ABC
  and usyn behavior specified. Do not reintroduce mapper-local satopt runs or
  change shared compile defaults implicitly.
- [ ] Specify how assumption-derived exclusivity is represented/scoped. Do not
  persist it as a globally proven `kFormalOnehot` fact in reusable source
  graphs, LEC inputs, simulation graphs, or shared-profile proof caches.
- [ ] Define interaction with colors and runtime checks before invoking A1/A2/
  `share_mux_regions`; their initial guards reject those nodes.
- [ ] Test overlapping controls: shared compile/simulation/formal must retain
  the failure, while any authorized synthesis assumption stays confined to its
  private lowering. Distinguish assumed versus proven facts in reports/cache.
- [ ] Measure QoR only after the integration boundary and tests are in place.

Keep B3 independent of A1–B2; it is not a prerequisite for shared-profile gains.

# Existing coverage and deferred work

| Idea | Current coverage / remaining limitation |
|---|---|
| Identical Hotmux values, mux-chain grouping | `Mux_sharing` groups private binary/exclusive regions when its gain check passes; unproven obligations remain. |
| Constant select/arms, constant-arm boolean folds | `scalar_mux` and scalar propagation; logical inversion is distinct from bitwise `Not`. |
| Flop feedback to enable | enableopt's state-specific region engine. |
| Globally constant selectors | satopt `constants`; B1 adds path-local facts. |
| Small Boolean resynthesis | ABC, usyn and `simp_ctrl` cover some functions; this does not establish full equivalence to the Pištek method. |

- [ ] Measure SmaRTLy-style case-tree rebuilding before implementing decision
  reordering. Compare explicit case fixtures against the pinned Yosys flow.
- [ ] Audit dense-case/table-lookup (`pmux2shiftx`) and per-column narrowing
  against existing lowlane/bitwidth/satopt behavior before claiming a gap.
- [ ] Defer identity-arm sharing (`mux(s,X,X+A) -> X+mux(s,0,A)`). It does not
  satisfy A1's strict operator reduction, and accumulator holds belong to
  enableopt. Reconsider only as a bounded compound rewrite with demonstrated
  net gain and the same state guards.

# Delivery order and acceptance evidence

1. **A0**, then **A1 binary**: resolve widths and land the smallest useful
   sharing engine. Add index and exclusive-Hotmux support incrementally.
2. **A3**: independent two-group improvement, with its own total-node metric.
3. **A2 + A4 inversion**, then **A4 Hotmux CSE**. Keep existing enableopt and
   contract tests unchanged. Final runtime order remains A2 before A1.
4. **B1**, gated by measured residual opportunities after A2.
5. **B2**, gated by wide-arithmetic opportunities and all-use analysis.
6. **B3** separately, after its private-synthesis integration is specified.

For every implementation step:

- [ ] Run relevant unit tests: `//pass/cprop:cprop_test`,
  `//pass/bitwidth:bitwidth_test`, `//pass/enableopt:enableopt_test`, and
  `//pass/lec:query_test`; add the affected satopt and CLI suites for Part B.
  Keep each test under 20 seconds opt / 60 seconds dbg.
- [ ] Add small repository-owned fixtures and require explicit Proven LEC
  verdicts for them. Also check structure and obligations: equivalence alone
  does not show the optimization fired or that a runtime check survived.
- [ ] Save before/after non-mux operator, total-node, mux/control and edge counts,
  pass runtime, visit/work counts, generated-size bounds, and applicable satopt
  stage rows. Test long chains and shared/high-fanin rejection paths.
- [ ] Run source-to-output equivalence checks with recorded commands/options.
  For large external designs, record Proven / Refuted / Unknown / timeout
  separately. A time-limited run with no failure is a smoke result only.
- [ ] Benchmark logikbench mux cases and available dino/lhdtrack or wide-arithmetic
  designs outside hermetic tests. No BUILD/test script may read a sibling
  benchmark repository. Record unavailable workloads instead of substituting
  invented measurements.
- [ ] Report matched-library/constraint area, delay and runtime for LiveHD
  before/after and pinned Yosys (`opt -full`, with `share` measured separately).
  Include ASAP7/sky130 where available; do not assume the node metric predicts
  either QoR result. Report regressions explicitly.
- [ ] Record revisions, dirty-tree diffs, tool/library versions, commands,
  stage/profile options, seeds, limits and result locations. Do not rebuild
  during a measurement sweep; after a rebuild, run warm commands twice and
  report the second warm result because code salts invalidate caches.

No implementation has been validated by this document review. Width behavior,
unproven-Hotmux sharing and B3's integration remain explicit design tasks;
the other items have concrete guards and acceptance criteria above.
