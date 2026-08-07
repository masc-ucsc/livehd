# `lhd pass analyze` — read-only structural diagnosis

```
lhd pass analyze lg:DIR [--top m] [--set pass.analyze.checks=loops,clocks,colors]
                                  [--set pass.analyze.verbose=true]
                                  [--set pass.analyze.strict=true]
```

Surveys a **whole library** in one invocation and reports **every** finding in
**every** definition. It transforms nothing and emits no `lg:`.

## Why it exists

`inou.cgen.sim` refuses on the *first* offender and stops. So the only way to
learn what else is wrong was to fix one thing and re-run — 45 minutes per round
on minion — and the diagnostic named a node (`memory_29184:mask.ex_mask_rf_q`)
without saying which of four unrelated repairs that node implied. A plan written
against the wrong class measures as "no change", which is exactly what happened
to `todo_sim_pipeline.md`'s Phase 1: it predicted five of minion's combinational
loops and cleared zero.

## The three checks

### `loops` — and the bit that matters is `real`

The word-level cycle is computed **twice**, under the two scheduling models:

| model | Memory read | `Sub` call | who thinks this way |
|---|---|---|---|
| `strict` | an ordinary node | an ordinary node | `inou.cgen.sim`'s single-pass `forward_class` walk |
| `settled` | cuts (reads stored state) | cuts (a callable boundary) | an event-driven simulator; `graph/split_selfref.cpp` |

A cycle in **strict** but not in **settled** is a **FALSE** loop: the design is
fine and the single-pass schedule is what cannot order it. Each finding also
carries the class of what sits on the cycle (`memory` / `sub` / `set_mask` /
`get_mask` / `other`), because the repair differs completely between them.

Measured on minion (172 definitions, one command): **31 loop findings, all
FALSE** — 17 of them involving a Memory. Verilator agrees: it verilates all five
modules `lhd sim` refuses with zero circular-logic warnings.

That count was 21 before a bug in this file was fixed: `graph_util::is_type_register`
counts a Memory as a register, so a single call to it cut memories in BOTH
models and made the `!strict` clause dead code. The entire MEMORY loop class was
invisible — `vpu_tensorfma` and `vpu_ctrl` read 0 findings while cgen_sim refused
both ON a Memory. Do not reach for `is_type_register` here.

### `clocks` — where each state element's clock comes from

Every `Flop`/`Fflop`/`Latch`/`Memory` is classified: `implicit`, `plain_input`,
`plain_internal`, `gated_inline`, `gated_chain`, `gate_cell`, `from_sub`,
`gates_child_port`, `inverted`, `divided`, `unresolved` — with the resolved root,
the guard count and the gate-chain depth. `sim_ok` says whether `inou.cgen.sim` can lower it today.
Only the ones it cannot are reported unless `verbose=true`.

Measured on minion, after two CORRECTIONS this survey's own first answer needed:

* A `Sub` output whose def is a recognized ICG cell is `gate_cell`, **not** a
  problem — `inline_clock_gate_cells` inlines it before scheduling and all 372
  such sites emit clean. Reporting them as unlowerable was this pass's first
  answer and it was wrong by 372.
* The class that IS broken runs the other way: `gates_child_port` — a gate in
  THIS definition whose output crosses into a CHILD as that child's clock port.
  Inside the child that port is an ordinary graph input, so cgen_sim picks it as
  the reference clock and the child's state commits EVERY tick with the gate as
  dead code. A **silent miscompile**, and invisible from either definition alone,
  which is the whole argument for surveying a library rather than a module.
  Measured: **22 sites**, in `txfmafrac_top` (18), `vpu_lane` (2), `prim_mul_div`
  and `vpu_lane_tima`.

That 22 is deliberately CONSERVATIVE: it fires only when the child actually
resolves a state element's clock to that exact port, so a gate handed down
through two boundaries is not counted. Structurally there are ~101 such port
connections; 22 is the subset this check can prove.

### `colors` — invariants of `Color_acyclic` nobody was checking

Clears any existing colouring (an already-coloured graph makes `apply_coloring`
treat itself as *seeded* and keep the old ids), re-runs `Color_acyclic`, then
validates:

* **totality** — every `is_partitionable` node carries a colour;
* **acyclic quotient** — contract each colour to one vertex, keep cross-colour
  edges (state elements cut, since a `q` is last period's value) and require a
  DAG. This is the invariant the whole "partitions are independently callable
  methods" plan rests on. Suppressed when the *node* graph is already cyclic,
  because there the result says nothing about the colourer.

## Two limits, stated because a survey that overstates its reach is worse than none

1. **Per definition.** The settled model cuts a `Sub`, so a loop closing
   *through* an instance chain reads as FALSE. Verilator inlines across the
   hierarchy first and can see such a loop — measured: it reports `UNOPTFLAT` on
   `minion_dcache_top` where this pass says FALSE.
2. **The `lg:` library, not the emitter's graph.** `inou.cgen.sim` mutates the
   graph before scheduling (`inline_clock_gate_cells`, `flatten_false_loop_subs`,
   `split_packed_selfref_wires`). A disagreement between this pass and the
   emitter is itself a finding about those transforms.

## Output

Findings ride the normal diagnostic stream at `info` severity — never an error,
never an exit-code change, so surveying a design the transforming passes refuse
still works. `strict=true` adds one **deferred** error at the end: the run fails,
but only after every finding has been reported.

```bash
lhd pass analyze lg:out_lg | jq -c 'select(.code=="analyze-clock") | .attrs'
```
