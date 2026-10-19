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
