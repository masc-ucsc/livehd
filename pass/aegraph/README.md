# pass/aegraph: acyclic e-graph constant propagation

`pass.aegraph` is a second constant-propagation engine for LGraph, an
alternative to `pass/cprop` behind one knob. It is a Cranelift-style acyclic
e-graph ("aegraph"): hash-consing, eager rewrites at insertion, no rebuild,
extraction, then elaboration back into the graph. The design and the plan are
`todo/livehd/2a-aegraph.html`; this file says what exists today.

**Status: milestone B, the skeleton engine.** There are no rewrite rules yet, so
the engine does exactly what hash-consing and the constant analysis give for
free: common-subexpression sharing and folding of fully known cones. Classic
cprop stays the default and is not removed.

## Selecting the engine

```
lhd compile foo.prp --set compile.cprop.engine=classic          # default
lhd compile foo.prp --set compile.cprop.engine=aegraph
lhd compile foo.prp --set compile.cprop.engine=aegraph+classic  # aegraph, then classic, in each slot
lhd compile foo.prp --set compile.cprop.engine=aegraph --set compile.cprop.engine_slots=first
```

* `compile.cprop.engine` accepts only `classic|aegraph|aegraph+classic`. There
  is no `none`: at least one engine always runs. A bad value is rejected when
  the options are read, not when the pass runs.
* `compile.cprop.engine_slots=both|first|second` swaps only one of the two
  compile cprop slots (`first` runs before `pass.legalize`, `second` after
  `enableopt`); the other slot stays classic. Default `both`.
* The kernel does the dispatch (`compile_graph_passes`, `lhd/lhd_kernel_common.cpp`):
  the set name stays `compile.cprop` for every engine, and the driver merges the
  `compile.aegraph` set into a `pass.aegraph` step. `Pass_cprop::optimize`
  refuses any non-classic `engine` that reaches it.
* `low_lanes`, `loop_hoist` and `engine` are registered on `pass.aegraph` as
  INTERNAL labels (hidden from `lhd list options`): each knob has ONE
  spelling, `compile.cprop.*`, and `--set compile.aegraph.low_lanes` (or
  `loop_hoist`, `engine`, `engine_slots`) is rejected with that hint.
* The compile-cache descriptor names the resolved engine
  (`pipeline=cprop[aegraph:both],bitwidth`), so an engine switch never replays
  the other engine's graphs. `//pass/aegraph:pass_aegraph` is in
  `//lhd:compile_salt`.

## What one run does (`aegraph.cpp`)

1. **Pre-pass.** Tarjan SCC over the combinational structure (state and IO break
   cycles). A non-trivial SCC, or a self edge, is a combinational cycle that may
   be false at word level (a packed struct written field by field); its members
   are OPAQUE: never hash-consed, never folded. The classic packed-slice folds
   run later, in the post steps.
2. **Build.** One forward walk. IO, state, `Sub`, `AttrSet`, `Clock_cell`, `LUT`,
   `runtime_check` nodes and non-exclusive `Hotmux` cells are skeleton leaves:
   their driver pins are leaf classes. A pure node is normalized (commutative
   banks sorted by class id, multiplicity kept: `Sum(a,a)` is `2a`, `Xor(a,a)`
   is `0`) and hash-consed on `op + operand classes + color + synth_policy +
   proven`. The `proven` code is in the key so a proven `Hotmux` never merges
   with an unproven twin. Names are not in the key.
3. **Constant analysis.** Every class carries a three-valued `Dlop` (or no
   value). A node whose operands all have values is evaluated with the same
   Dlop semantics classic folds with. The result becomes a constant ONLY when it
   is fully known (the P7 contract): a result with `?` bits stays a node, and a
   `?` literal is one const node that is never merged by value and never
   re-minted. A fold that is exactly an operand (constant-selector `Mux`, a
   shift by zero, a window covering a whole literal) forwards that operand's
   class. An out-of-range or disconnected `Mux` selection stays a `Mux` (Q1).
4. **Extract.** Cost-free: the representative of a class is its first member,
   the original node.
5. **Elaborate (diff).** Original nodes that are class representatives are
   reused untouched: nids, names and srcids survive. A duplicate is retired by
   reconnecting each consumer sink to the representative (snapshot the edges,
   `connect_sink`, then `del_node`). Guards: a ring that closes over a compact
   loop keeps the node (`closes_loop_self_edge`); a parallel edge that hhds
   would dedup keeps it; two named twins both stay (a named twin absorbs an
   unnamed one and moves its name). The retired root's width stamp is copied to
   the representative when that has none. Dead pure nodes are then deleted.

After elaboration `Cprop::do_post` runs the classic region passes (pack
canonicalization, vector-mux narrowing, muxctx, mux/op sharing, flop-enable
folding, dead-code sweep) and, with `low_lanes`, low-lane narrowing; then the
library-wide `loop_hoist`. These are classic *sub-routines*, shared with
`Cprop::do_trans` through `Cprop::region_passes`; the engine never runs the full
classic forward sweep (that is what `aegraph+classic` does).

With no rules the result is the input graph except for shared and folded
nodes. It is deterministic: class ids follow forward-walk order and no decision
iterates an unordered container.

## Files

| file | role |
| --- | --- |
| `aegraph.hpp` / `aegraph.cpp` | `Egraph` (classes, e-nodes, hash-consing), the constant analysis, the engine (`livehd::aegraph::optimize`) |
| `pass_aegraph.{hpp,cpp}` | the `pass.aegraph` EPRP method: worker pool, post steps, `loop_hoist`, one `[pass.aegraph]` summary line per run |
| `aegraph_test.cpp` | no-rule round trip (including a `Subnode_loop` body and a non-exclusive `Hotmux`), CSE keys, folds, P7, cycles, determinism, LEC soundness |

## Testing the engine on the whole corpus

The default suite keeps `classic`. To run the same `inou/prp` tests with the
design under test compiled by the aegraph (the reference and any golden Verilog
stay classic):

```
bazel test -c opt //inou/prp:all --test_tag_filters=-manual,-external-simulator,-fixme \
    --test_env=LHD_PRP_COMPILE_SETS=compile.cprop.engine=aegraph
```

`LHD_PRP_COMPILE_SETS` (space separated `k=v`) feeds `PrpRunner.compile_sets`,
the `--set` list that rides only the compile and sim commands of the design
under test. It is deliberately separate from `equiv_sets` (the `--bitfuzz`
list), which is also appended to `lhd lec`. `pyrope_test.py --aegraph` is the
same thing for one fixture, and `//inou/prp:prp-aegraph-sim` / `prp-aegraph-lec`
are manual targets that run a whole tier that way (`bazel test -c opt
--test_tag_filters= //inou/prp:prp-aegraph-lec`). Three header-only equiv variants
(`loop_carry_kinds_3`, `runtime_index_store_lanes_3`, `hotmux_unique_1`) pin
`:set: compile.cprop.engine=aegraph` in the default suite.
