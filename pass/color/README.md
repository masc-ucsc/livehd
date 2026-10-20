# Synthesis coloring

`lhd synth` uses `pass.color.synth.mode=cones`. Colors partition mapped
regions and are also reused as placement groups. The predicted-AIG budget
(`max_gate`, default 30000) measures synthesis work, not SRAM area.

Clock and reset control closures are protected when `ctrl_cones` is enabled
(the ABC profile). Their roles come from native state pins and Liberty
metadata. The traversal follows their combinational fanin and stops at state;
the registers and memories they drive remain outside those colors. Even a
one-node clock/reset closure stays separate through tiny-color absorption
and forward merging. Recognized latch/AND ICG implementations still stay
together for clock-gate mapping. Implicit reset muxes inside a native register
are still lowered with that register; coloring does not rewrite state semantics.

## Memory-first experiment

Enable with:

```
lhd synth design.v --top top --workdir W \
  --set synth.liberty=technology.lib \
  --set pass.color.synth.memory_color_first=true
```

The option defaults to false. With it enabled:

1. Extract clock/reset and ordinary control closures first.
2. Create a cone for each memory, using address/data inputs across all its
   ports. Clock, reset, enable and other control inputs do not seed it.
   Memories with identical source-driver pin bundles share an initial cone.
3. Walk these cones before ordinary cones, using the existing operator cuts,
   register boundaries and traversal budget.
4. Visit live memory groups in deterministic memory encounter order. Each
   group gets one best eligible overlap merge per round. A group already
   served in that round cannot be consumed again until the next round.
   Rank by shared predicted AIG; break ties by smaller union, then node order.
5. Stop when no positive shared-AIG contact fits the existing size budget.
   Remaining logic uses the ordinary cone flow. Completed memory groups are
   protected from later ordinary overlap, forward and tiny-color merges, so
   those phases cannot undo the round-robin order.

All memories are traversal boundaries, independently of this option. Reaching
a memory output never walks through that memory into its address/data inputs.
This includes asynchronous-read Liberty RAMs whose output declarations do
not have HHDS loop-break markers, in all three synthesis-coloring modes
(`cones`, `synth`, and `pipe`). These are coloring boundaries; asynchronous
read behavior and the graph's sequential loop-break flags are unchanged.

Liberty RAM identification uses `memory()`. Its address and write-data roles
come from `memory_read()` / `memory_write()` metadata, and clock roles from
`clock: true`. No SRAM master/pin spelling or width heuristic is used. A manual
`lhd pass color synth` command can supply the same inventory with
`--set pass.color.synth.liberty=technology.lib`; the fused synthesis driver
supplies its selected `synth.liberty` automatically. A RAM without interface
metadata remains a boundary but contributes no guessed address/data seeds.

The top's coloring report records `memory_instances`, `memory_input_cones`,
`memory_rounds`, `memory_merges`, `memory_merges_per_round`, `clock_groups`
and `reset_groups`. The final zero-merge round records that no eligible
positive-overlap merge remains. Merged memory groups get one turn per round,
not one turn per original memory in the group.

Focused checks:

```
bazel test -c opt //pass/color:all //pass/liberty:liberty_dff_test
```

## Placement colors after synthesis

`synth.place.fp.colors=true` with `synth.place.fp.color_source=placement`
selects the read-only analysis in `floorplan_color.cpp`, immediately before
floorplan placement. It uses the mapped physical graph and Liberty pin roles;
it never overwrites synthesis colors or changes connectivity. The default
source remains `synthesis`.

Each SRAM/non-standard-cell macro seeds both its input and output combinational
cones. Shared logic merges seeds transitively. Registers, latches, macros and
IOs are boundaries; clocks, reset closures and constants are excluded. The
constant exclusion includes buffered combinational chains whose inputs are all
known constant; the exclusion stops at registers and macros. For
Liberty memories, only metadata-declared address and write-data inputs start
backward walks. Functional enable/write-mask inputs are excluded in this first
version. Unknown block macros use their non-clock signal ports. Unsupported
sequential interfaces remain barriers rather than being traversed as logic.

Beyond each colored cone, the first boundary register and one additional
combinational stage reserve space. Traversal stops before the next register or
another colored node/macro. Each uncolored cell's area is divided equally among
the distinct eligible recipients. Duplicate paths never multiply its area.
Below-threshold colors remain traversal barriers but receive no reservation.
Reservation changes only a centroid's repulsion budget, not its attraction
members, measured center, cell geometry or density charge.

After graph coloring, membership is split into portions belonging to the
deepest selected hierarchy candidate (or the root scope). Shared external
logic keeps its own portion instead of promoting a local SRAM group to the
root. Only portions with a movable macro and sufficient local physical area
become centroid leaves. One-stage reservation reachability is recomputed from
the local portions; it is not copied to all portions of the original color.
This changes centroid membership, not netlist connectivity or physical fences.

The analysis returns pass-local membership; no graph attribute is registered.
During FP timing repair, existing owners and macro-group identities are frozen.
Inserted buffers inherit their recorded physical origin's owner. Removed cells
are dropped, and physical areas plus one-stage reservations are recomputed on
the edited graph. A fresh placement starts a fresh color analysis.

### Memory-first placement colors

`synth.place.fp.color_source=memory_first` selects a bounded alternative, still
only immediately before FP and only when `synth.place.fp.colors=true`. It never
runs synthesis coloring or changes synthesis attributes. The algorithm borrows
the synthesis memory-first scheduling policy, adapted to the mapped physical
DATA graph:

1. Visit macros in stable instance-name order. Identical nonempty address/data
   driver-pin bundles share a seed; separate output pins remain distinct.
2. Walk input cones backward, stopping at state, macros, excluded control logic
   and the growth budget. First ownership partitions physical logic; overlaps
   record shared logic area without duplicating ownership.
3. Walk ordinary state/output input cones to supply possible growth regions.
   These do not independently become floorplan colors.
4. In each round, give each surviving memory group at most one best eligible
   merge. Rank by shared-area evidence, then smaller combined owned logic area,
   then stable region order. Neither participating group can merge again that
   round. Stop at a zero-merge round.
5. Keep only groups containing macros. Unclaimed ordinary regions remain
   uncolored and use the same one-stage reservation rule as the other physical
   color source. Split these graph colors into hierarchy-local portions before
   centroid construction, as for the other physical color source.

`synth.place.fp.color_maximum` (30000) bounds walked/merged logic area in units
of the design's average physical standard-cell area, the same unit used by FP
candidate eligibility. This is not the synthesis predicted-AIG unit. Macros do
not consume this logic-growth budget, matching their role as boundaries, but
all macro physical area contributes to centroid eligibility and repulsion.
SRAM output cones are not added unconditionally in this variant; an ordinary
cone joins only through positive shared logic that fits the budget.

The mapped graph no longer carries the original high-level arithmetic/operator
cuts. This variant reuses memory-first scheduling and bounded overlap growth,
with Liberty-derived state/control boundaries; it does not promise the same
partitions as pre-synthesis coloring.

The report records initial memory groups, merges per round and truncated
cones. FP repair keeps the initial identities and ownership, even if resizing
later takes a group beyond its original growth cap; areas/reservations refresh.
The source and budget are included in placement cache identity. Existing
`synthesis` and unbounded two-sided `placement` sources retain their behavior.
