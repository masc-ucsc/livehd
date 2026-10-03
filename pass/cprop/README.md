# Constant propagation and mux sharing

The compile schedule is `cprop -> bitwidth -> enableopt -> cprop -> bitwidth`.
Each `Cprop::do_trans` makes one ordinary forward scalar/CSE sweep. It then
processes explicit wiring and disjoint mux/vector groups, and deletes dead
nodes. There is no pack fixed point, fresh-node retry sweep, or vectorization
round loop. Vector mux groups run in dependency-level order within one call.

Cprop does not use width/sign annotations as semantic evidence. Literal masks,
Concat lane contracts, and cached structural facts can justify local rewrites.
`pass/bitwidth/bitwidth_rewrite.cpp` owns range-dependent rewrites after
inference converges. Its queries read the settled `bwmap`; constructors and
retirement invalidate recycled pin facts. An aborted inference skips optional
range-dependent rewrites.

`cprop_wiring.hpp` indexes explicit Get_mask/Concat/Set_mask/constant-shift
layouts using persistent interval treaps. Shared write versions share storage;
updates and reads cost expected O(log L + K), rather than expanding every
prefix. A pass-wide interval-visit budget bounds expanded lane traffic. Unknown
or overlapping Or intervals remain opaque. Packed cycle tails may bootstrap a
Set_mask base spine once; actual computation and state/IO ports are boundaries.
Forwarding retains the exact source port and tracks retired/recycled identities.

The scalar sweep is predominantly O(V + E). CSE operand sorting and interval
indexing have their stated logarithmic costs; large-integer arithmetic is a
separate cost. Boolean conjunction inspections use operand indexes and a
pass-wide budget for shared children. Consuming private associative regions
are flattened only at their outer consumer. Finite reduction descriptors are
computed forward once, with a bounded representation size.

## Loop-invariant hoisting (compact loops)

`loop_hoist.cpp` runs serially after the per-graph sweep (`pass.cprop`
label `loop_hoist`, default true). A compact loop's body sees two kinds of
input: the ordinal, the activation and every carry-in change between
iterations; every other input is bound once per evaluation. A body cell that
reads only invariant inputs and constants (transitively) computes the same
value in every iteration, and the rolled form still computes it `count`
times -- once per call in the simulator, once per instantiated copy in
synthesis. The frontier of such cells (those a varying cell, an inner loop or
a body output consumes) moves out: the body definition gains one
`__hoist<k>` input per value, each loop instance clones the cone into its
parent once (fed from what the parent already drives into the instance's
invariant inputs) and drives the port, and the body reads the port. The
descriptor, carries, trip count and per-ordinal body are untouched: the
compact form is preserved by ruling, so this -- not unrolling -- is where
the body gets cheaper. Two filters: the value's width must be known (it
becomes a declared port, and a 0-width port takes the body off the
simulator's inline pure evaluator, which cost br_arb_lru 18 -> 25 s before
the filter), so in the compile schedule the move happens in the cprop round
after bitwidth; and the cone must compute something -- a lone slice,
widening, pack or shift of an invariant mints no gate and would only add a
bound input per instance. A body also instantiated as an ordinary Sub is
left alone (that call's inputs may all vary); a cone reading an input some
instance leaves unconnected stays. Sweeps repeat while a hoisted cone lands
in a parent that is itself a body. `LIVEHD_LOOP_HOIST_DEBUG=1` prints each
move; `--set compile.cprop.loop_hoist=false` disables it. On the lhdtrack
`for`-loop designs the bodies are ordinal-indexed slices with little
invariant computation, so the move is neutral there; the rolled form's
remaining cost is the simulator's per-call protocol (invariant binds, carry
compare/copy, wide mux arms widened to a signed carrier), not body logic.

## Mux sharing

`cprop_opshare.cpp` factors private same-shape operators out of binary muxes
before region sharing. Exclusive Hotmux groups use the same shape matcher,
retain the original outer controls/default, and use proven subset controls for
operand Hotmuxes. An inactive group evaluates an original operand vector,
avoiding newly introduced zero divisors. Operand banks retain their arity and multiplicity;
positional parameters (mask, extension position, reduction count and Concat
lane widths) must agree. Fresh operand muxes receive a lossless union carrier only when every operand
realization is known; otherwise bitwidth unions their full signed/unsigned
ranges afterwards. The retained output still has
the original observation boundary. Cprop must be followed by bitwidth before
finite-width simulation, emission or LEC; a result hint is not a truncation
operator in cprop's integer algebra.

For each differing Concat lane, every arm must have a structural unsigned
bound within the declared lane width. Otherwise the matcher rejects sharing:
a lossless mixed-sign mux can exceed the lane window, and adding a mask would
consume the binary rewrite's operator saving. Width hints do not bypass this
guard.

The matcher rejects named/shared arm operators, colored/check-bearing nodes,
unknown literals and latch-Q operands. Its worklist uses pin generations and
a graph-size work/edge budget; operand sorting has its usual logarithmic cost.
Index sharing currently requires a structurally proven in-range selector:
the runtime's invalid result, cprop's zero and LEC's last-arm out-of-range
behavior need reconciliation before general indexed sharing. Hotmux groups require matching result realizations. Existing mux-region
grouping below is independent.

`cprop_muxctx.cpp` prunes disjoint private selection regions before operator
sharing. Its iterative walk stores at most 16 equality/disequality facts per
path; copied sibling snapshots provide rollback. It changes only the relevant
data edge or disables an exclusive Hotmux control, never globally replacing a
value from a contextual fact. Named/shared/state/check/color boundaries stop
the walk. Boolean data becomes 0/1 only from structural bool01 evidence.
The region predicate emitter also consults these facts to avoid contradictory
paths without expanding a shared predicate DAG into a sum of products.

Exclusive Hotmux CSE sorts complete control/value pairs and retains a separate
default and proof distinction. Runtime-check cells and unproven Hotmuxes do not
participate. Even equal data cannot erase an unproven overlap obligation.

`cprop_mux.cpp` groups repeated data values across binary `Mux` and exclusive
`Hotmux` regions. With exactly two values, it first plans a predicate over the
owned selection tree, replacing terminals by false/true. Local folds can use
a wide signed selector directly when only its truth value matters. Both result
polarities are considered; all reachable new predicate nodes and the final
data mux must total fewer nodes than the old region. Rejected plans create no
graph nodes. Named interiors remain separate region roots. This two-value
rule also applies to bool01 data; equal-cost priority chains stay unchanged.

For larger groups, a selector activates a child under `parent_active & selector`;
the fallback uses `parent_active & !OR(selectors)`. These predicates stay shared
graph expressions. The pass never enumerates paths or expands Boolean expressions
into sums of products. Conditions reaching the same terminal are ORed together,
and the region root becomes a Hotmux over the distinct values.

Regions stop at shared outputs, non-mux data operators, and colored nodes. Each eligible internal mux has exactly one outgoing edge, which
must feed a data arm of its parent. Ownership is computed once before rewriting,
so a reconvergent graph cannot trigger repeated overlapping cone walks. The
implementation uses iterative walks and dense port indexing, without sorting
Hotmux arms. Expected time and temporary/generated space are O(V + E), plus the
representation size of distinct constants that are hashed. Hash tables make
this an expected bound, not a worst-case hashing guarantee.

The fast path recognizes Hotmux exclusivity from an existing one-hot proof or
distinct integer equality tests against the same selector. Other Hotmuxes,
including deferred runtime checks, retain their original overlap obligation.
There is no solver call. New Hotmux predicates are exclusive by construction.

A rewrite must remove repeated alternatives or a hold value and reduce the
number of word muxes. This is a structural heuristic independent of width
annotations. Shared source muxes are not counted as removable work.

Only `enableopt` invokes the state-specific part of the shared region engine.
Ordinary cprop leaves destination-Q hold regions intact. When a root exclusively feeds a single-stage Flop's `din`, a terminal equal to
that Flop's `Q` can become an enable: `old_enable & OR(non_hold_conditions)`.
Clock, reset, initialization, and the state width/sign are preserved. Pipeline
Flops, latches, and shared `din` cones do not receive this enable transformation. Index Muxes and unknown constant operands are also left
outside the rewrite.

Regression coverage includes exhaustive small control spaces, decoded and
unproven Hotmuxes, implicit-zero defaults, signed data, wide selectors, shared
cones, pipeline holds, and a 2048-level chain with a linear generated-size bound.
`//pass/lec:query_test` additionally proves pre/post single-stage transitions by
induction with symbolic data, state, enables, and freely changing reset.

```
bazel test -c dbg //pass/cprop:cprop_test //pass/bitwidth:bitwidth_test //pass/enableopt:enableopt_test //pass/lec:query_test
```
