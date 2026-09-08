# Constant propagation and mux sharing

`Cprop::do_trans` folds constants and scalar identities, canonicalizes packed
wiring, merges identical combinational expressions, and then runs mux sharing
before final dead-node cleanup. Bitwidth inference remains a separate pass.

## Mux sharing

`cprop_mux.cpp` groups repeated data values across binary `Mux` and exclusive
`Hotmux` regions. A selector activates a child under `parent_active & selector`;
the fallback uses `parent_active & !OR(selectors)`. These predicates stay shared
graph expressions. The pass never enumerates paths or expands Boolean expressions
into sums of products. Conditions reaching the same terminal are ORed together,
and the region root becomes a Hotmux over the distinct values.

Regions stop at shared outputs, non-mux data operators, width/sign changes, and
colored nodes. Each eligible internal mux has exactly one outgoing edge, which
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

Only data widths of at least four bits are considered. A rewrite must remove
repeated alternatives or a hold value, reduce estimated word-mux cost, and save
more bit-mux work than a conservative allowance of two one-bit gates per visited
branch. This is an area heuristic, not a timing guarantee: shared predicate
chains can still have substantial depth. Shared source muxes are not counted as
removable work.

When a root exclusively feeds a single-stage Flop's `din`, a terminal equal to
that Flop's `Q` can become an enable: `old_enable & OR(non_hold_conditions)`.
Clock, reset, initialization, and the state width/sign are preserved. Pipeline
Flops, latches, shared `din` cones, and width/sign mismatches do not receive this
enable transformation. Index Muxes and unknown constant operands are also left
outside the rewrite.

Regression coverage includes exhaustive small control spaces, decoded and
unproven Hotmuxes, implicit-zero defaults, signed data, wide selectors, shared
cones, pipeline holds, and a 2048-level chain with a linear generated-size bound.
`//pass/lec:query_test` additionally proves pre/post single-stage transitions by
induction with symbolic data, state, enables, and freely changing reset.

```
bazel test -c dbg //pass/cprop:cprop_test //pass/bitwidth:bitwidth_test //pass/lec:query_test
```
