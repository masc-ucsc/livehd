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

## Mux sharing

`cprop_mux.cpp` groups repeated data values across binary `Mux` and exclusive
`Hotmux` regions. A selector activates a child under `parent_active & selector`;
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
