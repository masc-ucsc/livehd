# muxopt — mux-tree optimization TODO

Goal: close useful gaps in cprop and satopt, using Yosys and mux-tree research
as references. This document tracks implemented rewrites, remaining semantic
blockers, and broader validation still needed.

Reviewed against the working tree on 2026-09-26. The review below supersedes
older progress notes. Checked items have implementation or validation evidence;
unchecked acceptance/benchmark items are not a claim that the core rewrite is absent.

## Review and implementation (2026-09-26)

The proposal was **partially implemented**, with several stale unchecked items.
The LT/GT synthesis and Word_sim fixes, reusable Boolean decoder (also used by
enableopt), bool-safe XOR inversion, and bounded binary/index operator matcher
were already present. A3's select-tree implementation was also present.

This review adds:

| Area | Current implementation and scope |
|---|---|
| A1 exclusive Hotmux operator groups | Same-shape, disjoint buckets reuse the binary matcher. Groups may include the explicit default. Original outer controls/default remain; inner controls are proven-exclusive subsets. An inactive inner group selects one original operand vector rather than manufacturing a zero divisor. Named/shared/colored/checked operators are rejected. |
| A2 structural context | `cprop_muxctx.cpp`: iterative disjoint private binary/exclusive regions; at most 16 integer equality/disequality facts per path; sibling snapshots; edge-local bypass and structural bool01 data substitution. Wide truth never implies value 1. Named/shared/state/check/color boundaries remain opaque. Runs before A1. |
| A4 Hotmux CSE | Sort complete control/value pairs only after exclusivity is established; retain default and producer generations, distinguish proof metadata, retain existing naming/color policy, and reject runtime checks. |
| B1 `muxtree` | New satopt stage after `odc`: at most four ancestor facts, simulation restricted to matching columns, no nomination from empty contexts, and query-local `Prover::truth_when`. Targets private binary selects and exclusive Hotmux controls. A contextual true Hotmux control stays intact; only its other controls can be disabled, preserving global exclusivity. |
| B2 `share` | New stage after `muxtree`: Mult/Div/Rem and variable shifts, existing operand matcher and lossless-carrier guard. Initial all-use support is exactly one binary-mux data use; every other use rejects the operator. Proves complete activations exclusive, caps pair attempts at 32 previous candidates and dependency walks at 256 pins, and checks both activations and operands for cycles. Multiple-use activation unions remain an extension. |

Both new SAT stages use the normal work/query/cone budgets and report/CLI stage
selection. They conservatively rerun their bounded searches, like `simp_ctrl`;
no contextual proof rows are persisted, so graph edits, profile changes, and
cold/warm runs cannot reuse stale contextual assumptions. Persistent proof-row
reuse and an aggressive profitability option are not implemented.

Two correctness discrepancies were found and corrected:

- Formal's EQ encoder still applied unsigned coercion to mixed-sign operands,
  although LEC had been fixed. It now compares integer values with each operand's
  own extension and unsigned headroom. A signed four-bit value cannot equal 15.
- The old all-equal Hotmux fold deliberately discarded an unproven overlap
  obligation, contrary to this proposal and the shared-profile rule. It now
  requires established exclusivity. The regression covers both proven collapse
  and unproven preservation; equal data alone is not a proof of exclusivity.

Remaining semantic/integration blockers:

- **General index muxes:** cprop/Verilog zero, LEC last-arm fallback, and native
  invalid-result behavior still disagree outside the explicit arm range. The
  structural in-range guard remains mandatory; this review does not choose a
  new language-level out-of-range contract.
- **B3 synthesis assumptions:** there is still no public synthesis-profile caller
  on the private mapping copy, nor an assumption-only provenance representation.
  Stamping reusable source graphs `kFormalOnehot` would be unsound. B3 remains a
  design/integration task, independent of the shared-profile improvements.
- **Unproven Hotmux sharing:** overlap checks and priority behavior remain
  observable; these cells are excluded rather than assigned a false proof.

Remaining acceptance work includes external QoR sweeps, wider backend/width
coverage, multi-use activation unions, and persistent contextual proof reuse.
The paired Set_mask special case remains separate because it also handles
shared-arm lane factoring outside A1's removable-private-operator contract.

Executable review regressions: `cprop_muxctx_test` (contexts/CSE/obligations),
`cprop_opshare_test` (nine additional Hotmux symbolic proofs), `prove_test`
(mixed-sign equality), `satopt_muxtree_test` (contexts/Unknown/budgets), and
`satopt_share_test` (cross-region arithmetic, all-use and cycle guards).
`inou/prp/tests/equiv/lec/muxopt_context.v` and its header-only `_1.v` select
`muxtree,share` for source-to-output validation. Current-run evidence is under `/tmp/livehd-muxopt-review-20260926/`.

### Current validation and measured tradeoff

- Nineteen focused targets passed in optimized mode (0.3–4.5 seconds each).
  The corresponding debug checks passed (0.5–27.2 seconds), including a rerun
  after giving the Hotmux test's setup proof the same resource floor used by
  production satopt. All remain below the 20-second opt / 60-second dbg limits.
- `prplec.py muxopt_context.v -v`: **Proven**. Both `muxtree` and `share`
  nominate, prove and apply one rewrite. `muxtree` uses 369 work units and
  `share` 366 on the debug source fixture. Native simulation of the optimized
  graph passes all 32 directed vectors. Emitted-Verilog roundtrip: **Proven**.
- The existing width pair passes 48 native vectors per source; `_1.v` is
  **Proven**, while the intentionally incorrect `_2.v` remains **Refuted**.
- Both ABC-mapped baseline and optimized netlists prove equivalent to the
  source with models from `lhd pass liberty gensim test.lib`. These gate-level
  multiplier proofs took longer than the unit checks and are not timing tests.
- One serial, matched-library microbenchmark uses `inou/prp/tests/abc/test.lib`,
  ABC, the default timing constraints, and the new source fixture. It compares
  satopt disabled with only `muxtree,share` enabled, so it isolates those stages;
  both sides include the current cprop implementation. Mapped area falls from
  **2752 to 1559** (43.4%) and gates from **860 to 502**, but full-design STA
  delay **regresses from 2.7 to 3.1 ns** (14.8%). This is a concrete area/delay
  tradeoff, not a claim of general QoR improvement or physical signoff.
- Each synthesis variant runs cold and twice warm without rebuilding. The
  second warm results and actual `synthesis_invocation.wall_ms` are retained
  in the JSON files (the external memory-monitor loop has sampling overhead).
  Both new stages have identical work/verdict/application rows cold and warm.
  Full command lines are in `qor-runs.json`; source hashes, the dirty patch and
  tool/library identities are saved alongside it.

Reproduce the new checks with:

```sh
bazel test -c dbg //pass/cprop:cprop_muxctx_test //pass/cprop:cprop_opshare_test \
  //pass/formal:prove_test //pass/satopt:satopt_muxtree_test \
  //pass/satopt:satopt_share_test --test_output=errors
# Repeat with -c opt to check the optimized runtime budget.
python3 inou/prp/tests/prplec.py inou/prp/tests/equiv/lec/muxopt_context.v -v
lhd compile inou/prp/tests/equiv/lec/muxopt_context.v --top top \
  --set pass.satopt=true --set pass.satopt.stages=muxtree,share \
  --workdir W --emit verilog:optimized.v --result-json result.json
```

The full repository suite and external logikbench/dino/lhdtrack QoR sweeps were
not run. The older checklist retains those broader acceptance items.


## Historical implementation progress (2026-09-25)

- Binary operator sharing and structurally in-range index sharing are implemented
  in `pass/cprop/cprop_opshare.cpp`. General index sharing and Hotmux operator
  sharing remain pending.
- Width policy: `Bitwidth::bw_pass` ignores derived arithmetic/mux hints and
  recomputes ranges; `set_bits_sign` only caps a result observed exclusively at
  finite declared boundaries. Fresh operand muxes have no hint, so inference
  restores a lossless signed/unsigned union. Explicit masks/extensions remain
  operands, and cprop output must pass through bitwidth before finite encoding.
- The index audit found a real discrepancy: cprop and Verilog emission use zero
  for out-of-range selectors, LEC uses the last arm, and Dlop/Slop return invalid.
  Resolve this before allowing unbounded selectors in index sharing. The current
  guard uses structural range facts, never width hints.
- Debug validation passed: `cprop_test` (including 20 operator kinds, mixed-sign
  narrowing, private-use guards and a 2048-level cascade), `cprop_lowlane_test`,
  `bitwidth_test`, `enableopt_test`, `query_test`, and new `cprop_opshare_test`
  (24 symbolic pre/post cases across 12 operators and narrow/wide outputs,
  plus four independent signed-equality cases). Tests take 0.1–2.3 seconds each.
- Stronger mixed-sign tests exposed LEC's incorrect Verilog-style coercion
  for integer `EQ`; the encoder now extends each operand with its own sign,
  with headroom for unsigned values. A signed four-bit -1 no longer equals 15.
- Concat sharing rejects varying lanes without a structural unsigned bound
  inside their declared width. A new mixed-sign operand mux can otherwise need
  nine bits for an eight-bit lane. Adding a mask would consume the binary
  rewrite's operator saving; existing explicit masks still permit sharing.
- Source fixture `inou/prp/tests/equiv/lec/muxopt_width.v`: `prplec.py` proves
  `_1.v` and refutes the intentionally narrowed `_2.v`. Native simulation via
  `tools/native_sim.py` passes 48 directed vectors for both base and `_1.v`
  (negative signed data, unsigned high bits, selectors 0/128). Base emitted
  Verilog roundtrip LEC is Proven for both base and `_1.v`. Logs, vectors and
  `correctness-manifest.json` (source hashes and native verdicts) are under
  `/tmp/livehd-muxopt-validation/`; QoR measurements remain pending.
- A3 now plans two-group select trees before mutation, folds predicates, tries
  both polarities and requires strict total-node reduction. The basic identity
  uses two muxes instead of three, including bool01 data and wide signed
  controls. Equal-cost priority chains remain mux trees instead of expanding
  into control gates plus a Hotmux. Named interiors remain addressable.
  Unit truth-table, default, rejection, naming and obligation checks pass, as
  do four symbolic pre/post LEC cases (including a required inversion).
  `muxopt_lru_select.v` versus `_1.v` is Proven structurally after compilation;
  native simulation passes all 32 combinations of its inputs. These tests
  cover a small LRU selection kernel, not an external cache benchmark.
- The variadic audit found and fixed backend discrepancies: Verilog now emits
  n-ary EQ as all-equal comparisons, native simulation includes every EQ and
  LT/GT operand, and Verilog LT/GT derive coercion from operand signs while
  retaining explicit masks. Native comparisons use full operand carriers,
  including upper words, rather than the one-bit result width.
  `//inou/cgen:variadic_eq` proved six EQ operand permutations plus mixed-sign
  LT/GT and binary EQ against an independent Verilog reference, and passed
  native directed checks on the original graph with a 128-bit unsigned operand
  (8.2 seconds debug). Its undeclared outputs retain the emitted Verilog and
  both verdict JSON files. Existing cgen simulation tests pass (30.5 seconds
  debug); optimized-mode runtime remains to be measured.

### Reproducing the current correctness checks

From the repository root, with the debug CLI built:

```sh
bazel test -c dbg //pass/cprop:cprop_test //pass/cprop:cprop_opshare_test \
  //pass/cprop:cprop_lowlane_test //pass/bitwidth:bitwidth_test \
  //pass/enableopt:enableopt_test //pass/lec:query_test --test_output=errors
bazel build -c dbg //lhd:lhd
bazel test -c dbg //inou/cgen:variadic_eq --test_output=errors
python3 inou/prp/tests/prplec.py inou/prp/tests/equiv/lec/muxopt_width.v -v
python3 inou/prp/tests/prplec.py inou/prp/tests/equiv/lec/muxopt_lru_select.v -v
```

For the directed native checks, generate the repository-owned fixture vectors
without an external simulator:

```sh
mkdir -p /tmp/livehd-muxopt-validation
python3 - <<'PY'
import json
from pathlib import Path
work = Path('/tmp/livehd-muxopt-validation')
width = []
for a in [-128, -17, -1, 0, 7, 127]:
    for b in [0, 15, 128, 255]:
        for s in [0, 128]:
            v = b if s else a
            width.append(dict(inputs=dict(a=a, b=b, s=s),
                              outputs=dict(shifted=(v >> 4) & 7, added=(v + 3) & 7,
                                           less=int(v < 7), clipped=v & 15)))
lru = [dict(inputs=dict(lru=q, access=a, bank=b), outputs=dict(victim=(q >> ((a >> b) & 1)) & 1))
       for q in range(4) for a in range(4) for b in range(2)]
for name, vectors in [('vectors', width), ('lru-vectors', lru)]:
    (work / (name + '.json')).write_text(json.dumps(vectors))
PY
LHD=./bazel-bin/lhd/lhd python3 tools/native_sim.py \
  inou/prp/tests/equiv/lec/muxopt_width.v top \
  /tmp/livehd-muxopt-validation/vectors.json /tmp/livehd-muxopt-validation/native
LHD=./bazel-bin/lhd/lhd python3 tools/native_sim.py \
  inou/prp/tests/equiv/lec/muxopt_width_1.v top \
  /tmp/livehd-muxopt-validation/vectors.json /tmp/livehd-muxopt-validation/native-shared
LHD=./bazel-bin/lhd/lhd python3 tools/native_sim.py \
  inou/prp/tests/equiv/lec/muxopt_lru_select.v top \
  /tmp/livehd-muxopt-validation/lru-vectors.json /tmp/livehd-muxopt-validation/native-lru
```

These results use proposal commit `829de19198f324cc63d9fb133a755020e4e6d928`
plus the dirty implementation tree. They are correctness evidence, not a
before/after timing or area sweep. The shared workspace also contains unrelated
changes, so benchmark provenance must include a full diff when measurements run.

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

- [x] Reconcile width behavior before enabling A1. Inspect the Mux/Hotmux
  cases in `pass/lec/encode.cpp`, `inou/cgen/cgen_sim.cpp`, Verilog emission,
  and bitwidth inference/rewrite. The first two explicitly fit arms to a typed
  result width; document when that narrowing is legal and how it survives
  moving an operator across a mux.
- [x] Specify metadata handling for the retained root and each new operand
  mux. Do not copy the narrowest arm's annotation or assume maximum `bits`
  suffices for mixed signs. Derive a lossless carrier when justified, or leave
  intermediate hints unset for inference; retain any required explicit mask
  or extension. If an operation relies on a narrowing boundary that cannot be
  preserved, reject that candidate.
- [ ] Add regressions with narrow mux outputs, differently sized/signed
  operands, negative and wide constants, and explicit `Get_mask`/`Sext`.
  Compare native simulation, emitted Verilog and LEC, so the same encoder bug
  cannot validate both sides unnoticed.
- [x] Complete the variadic backend audit. EQ Verilog/native emission and
  native/Verilog LT/GT are fixed and tested above. The synthesis blaster and Word_sim now compare every cross-bank pair;
  `blast_compare_smoke` and `satopt_sim_test` cover these implementations.
  Formal mixed-sign EQ was additionally corrected during this review.
- [x] Define a reusable operand-shape descriptor and bounded private-region
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

- [x] Implement binary sharing and operand-shape matching with the A0 width
  policy. Full A0/A1 acceptance still requires the pending integration checks.
- [x] Add structurally in-range index muxes and exclusive Hotmux groups/defaults.
- [ ] Resolve general index fallback semantics before extending range support.
- [ ] Fold the existing paired `Set_mask` rule into the engine only after its
  regressions pass; preserve the separate one-arm lane-update optimization.
- [x] Use deterministic shape buckets, not overlapping `(shared driver)`
  buckets that omit no-common-operand opportunities. Bound pairing/search;
  a global optimal grouping is not required.
- [x] A committed rewrite strictly reduces distinct non-mux operator count.
  Track generated mux/control nodes and edges separately. Requeue only newly
  exposed private operand muxes, with ownership/generation checks.
- [x] Charge operand visits and emitted edges to a pass-wide budget. State
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

- [x] Walk disjoint private binary-Mux/exclusive-Hotmux data regions iteratively.
  Stop at shared nodes, checks, colors and state boundaries. Prune only the
  relevant parent edge; never globally replace a value from a path-local fact.
- [x] Track zero/nonzero boolean conditions through `decode_bool_condition`;
  extend it for `Xor(b,1)` only when `b` is structurally bool01.
- [x] Add exact selector equalities/disequalities from binary `EQ(sel,k)`.
  `sel == k` decides other constant comparisons; `sel != k` only rules out
  that value. Cap stored facts at 16 per path, counting disequalities too.
  Roll back sibling facts and stop adding facts at the cap without guessing.
- [x] For binary muxes, the then edge supplies `sel != 0`, the else edge
  `sel == 0`. For exclusive Hotmuxes, an arm supplies its true control and
  default traversal supplies all controls false, subject to the fact budget.
  Index-mux path inference is a later extension requiring exact index rules.
- [x] Bypass a private mux whose selector is decided. Prune exclusive Hotmux
  arms under the same guards; leave unproven Hotmux controls/checks intact.
- [x] Replace a data occurrence of a known bool01 base with 0/1. Correct
  example: `mux(s, B, s) -> mux(s, B, 1)` for bool01 `s`;
  `mux(s, s, B) -> mux(s, 0, B)` on the else edge. Nonzero does not imply 1
  for a wide selector, so do not substitute its data value from truth alone.
- [x] Let mux-region predicate construction consult the same facts to avoid
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

- [x] For exactly two groups and no hold/check boundary, build predicate `p`
  from the owned mux skeleton: group A terminals become 1, group B terminals
  become 0. Emit `mux(p, B, A)`; preserve fallback semantics for Hotmux nodes.
- [x] Normalize the new predicate locally, before evaluating gain. In the
  example, the direct select-tree form above avoids gratuitous inversions.
  If boolean inversion is needed, use a zero test or a bool01-safe XOR.
- [x] Count **all** removable and emitted mux/control nodes for A3, with shared
  nodes counted once. Require a strict total-node reduction after bounded
  folding. A1's mux-free operator metric is not applicable here.
- [x] Relax the bool01 bailout only for profitable two-group results. Keep it
  for three or more groups until measured separately.
- [x] No speculative graph residue on rejection; use a planned descriptor or
  explicitly retire temporary nodes and invalidate facts.

Acceptance: exhaustive versions of the identity, asymmetric/repeated controls,
constant groups, equal-cost rejection, exclusive-Hotmux defaults and shared
prefixes. Add a small repository-owned LRU regression; separately measure the
external lhdtrack workload if available. Replace private “memory” references
with saved commands/results before using them as acceptance evidence.

## A4. Boolean inversion and Hotmux CSE

- [x] Share A2's bool01-safe `Xor(s,1)` decoding with scalar mux inversion.
  `Not(s)` is `-(s+1)`; for bool01 `s` it is never zero and is not logical
  inversion. For arbitrary integers it can be zero (`s == -1`).
- [x] Add a canonical pair-order-independent CSE key only for exclusive
  Hotmuxes. Current `cse_pass` sorts by `sink_bank`; for positional Hotmux
  pins that retains original pair order. Sort whole `(control,value)` pairs,
  never controls and values independently; keep default distinct.
- [x] Include every relevant attribute/proof/check distinction and preserve
  CSE's naming/color policy. Reject unproven/check-bearing cells. Do not erase
  an exclusivity obligation while merging an ordinary value node.

Acceptance: reordered exclusive pairs merge; changed default, changed pairing,
unproven overlaps and incompatible metadata do not merge unsafely.

# Part B — satopt: bounded proofs and contextual sharing

## B1. Context-aware select constants: `muxtree` stage

Depends on A2's region/fact primitives and measurements. Proposed position:
before `hotmux` in the existing fixed stage order, after the earlier value/ODC
stages (B2 would follow B1). Explicit stage selection remains supported.

- [x] Target binary-Mux selects and exclusive-Hotmux controls inside private
  regions. Form a boolean path predicate `P` from at most four ancestor facts.
  Dropping additional conjuncts weakens the premise and is conservative;
  record the exact premise used. Include Hotmux default conditions correctly.
- [x] Word_sim nominates a constant only using columns satisfying `P`.
  Conflicting samples reject it; zero matching samples are not evidence of
  constancy. Initially skip those candidates or make a separately budgeted
  reachability query. Simulation never establishes the rewrite.
- [x] Prove `P -> (sel == 0)` or `P -> (sel != 0)` for a binary mux.
  The latter does not require `sel == 1`. Use a dedicated contextual query or
  a query-local implication expression; current `Prover::is_true` accepts a
  pin, not an arbitrary formula. Never leak path assumptions to later queries.
- [x] On Proven, rewrite only the observed parent edge/owned region, following
  A2's guards. Unknown, Refuted, unsupported cones and budget exhaustion do
  not rewrite. Record counterexamples for later nominations.
- [x] Defer unproven Hotmuxes initially. Any extension must preserve every
  control and check cone: a contextual fact cannot globally tie a control.
  Existing `apply_selects` ties controls using **global** proofs; it cannot be
  reused unchanged for this purpose.
- [ ] Persist contextual proof rows keyed by the target, premise, region exits, profile, graph identity and
  proof options in cache identity. Invalidate/rebuild prover and simulation
  state after mutations; no stale proofs after rewiring or pin reuse.
- [x] Wire stage enumeration, parsing, defaults, ordering, budgets, reports and
  report serialization. Persistent proof rows are not written. Report candidates, rejects, queries, proven/refuted/
  unknown, applied, budget skips, work and node changes.

Acceptance: correlated but structurally undecided selects, wide selectors,
empty sample sets, unreachable contexts, sibling-context isolation, preserved
checks, forced budget/Unknown paths and cold/warm cache agreement. Measure the
incremental gain over A2 and ABC separately. `all_regions=false` is useful only
where color information exists; do not enable a cross-color filter in an
uncolored compile flow and silently filter out every candidate.

## B2. Sharing operators under exclusive activations: `share` stage

Depends on A1's shape/width policy and B1's bounded contextual infrastructure.
This covers operators whose consumers are in different mux regions, beyond
A1's common-mux pattern. Initial stage position: after `muxtree`, before
`hotmux`, with normal satopt budget accounting.

- [x] Collect **all** uses of each candidate output, including graph outputs,
  state updates, named/opaque consumers and checks. Derive an activation that
  over-approximates every observable use. The initial implementation accepts exactly one binary-mux data use;
  multiple or unsupported uses reject the candidate. Activation unions are deferred. Exclusivity of only one use is
  insufficient.
- [x] Bucket compatible shapes and bound pair attempts, fanout walks and
  activation DAG size. On a walk cap, reject or conservatively treat the
  operator as always active; never drop an unvisited use.
- [x] Nominate when simulation sees no overlap; prove
  `are_exclusive({act1, act2})`. Both-false behavior is unobserved only after
  the all-use analysis above and the common invalid-evaluation guards.
- [x] Replace paired operands with `mux(act1, Q_j, P_j)` and rewire the owned
  consumers to one shared operator. Recheck actual removed/added nodes.
- [x] Prevent cycles through **both controls and operands**. Neither selected
  activation nor any new operand dependency may reach either replaced output.
  A bounded reachability check can establish this; a bare topological-number
  comparison is not sufficient. Reject unsupported cyclic regions.
- [x] Reuse width, metadata, check, state and color guards from A1. Aggressive
  mode relaxes profitability filters only, never correctness guards.
- [x] Treat width thresholds as tunable LiveHD heuristics: initially consider
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

- [x] Run relevant unit tests: `//pass/cprop:cprop_test`,
  `//pass/bitwidth:bitwidth_test`, `//pass/enableopt:enableopt_test`, and
  `//pass/lec:query_test`; add the affected satopt and CLI suites for Part B.
  Keep each test under 20 seconds opt / 60 seconds dbg.
- [x] Add small repository-owned fixtures and require explicit Proven LEC
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

The 2026-09-26 review records the implemented subset and remaining blockers.
General index semantics, B3, broader activation unions, persistent contextual
proof reuse and full QoR/backend acceptance remain pending. Unit proofs alone
do not complete the proposal.
