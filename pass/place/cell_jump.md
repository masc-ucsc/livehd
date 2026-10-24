# Experimental cell-jump continuation

The `physical_repair` diagnostic accepts `cell_jump` (0–4),
`cell_jump_settle` (100), `cell_jump_interval` (10), and
`cell_jump_fix_period` (2). Propagation is disabled by default. This is a
final-placement experiment, not an `fp_plan` or `macro_plan` option.

The experiment harness supplies the distance-one DEF initialization: movable
cells directly attached to signal IO or fixed macro pins begin at their nearest
available row sites. The remaining cells retain their scattered starts. Depth
zero uses the original scattered seed; depth one uses this pin-adjacent seed
without propagation. The C++ continuation requires that same distance-one seed
for depths two through four (`cell_place` plus `restart_seed_def`). It does not
itself create a new distance-one seed or change ordinary placement defaults.

After `cell_jump_settle` GP iterations, visit graph-distance-two cells. Visit
each subsequent depth after `cell_jump_interval` additional iterations, but only
until the first actual final-GP timing evaluation. Pending depths are skipped
once timing begins, and GP buffer repair can then change topology safely. A
jump scheduled on the first timing iteration executes before that evaluation;
subsequent jumps are disabled. Synthesis timing seeds do not end this phase.
The default final timing warm-up is 200 GP iterations, followed by a 50-iteration
cadence. Default propagated jumps at 100, 110 and 120 therefore precede timing;
changing the jump schedule cannot delay timing. Traverse
incident signal nets in either direction; clock/reset-masked nets do not form
edges. All eligible cells advance the graph frontier, but a cell moves only if
its actual pin-to-parent-pin straight segment intersects a macro interior.
Tangency to a macro edge/corner is not a crossing. Fixed row-exclusion masks
constrain destinations but are not counted as macro-crossing obstacles.

A candidate reached from multiple parents moves toward the closest crossing
parent pin (deterministic node/pin tie break). Its new origin minimizes pin L1
distance over available row/site gaps, respecting origin fences, fixed objects,
earlier-depth cells, and already relocated cells in this frontier. Pin offsets
and orientation are preserved; final legalization performs row orientation and
whole-design legality. Unselected GP cells may overlap newly seeded cells.
The search has no local radius cap. A parent currently inside a macro can still
leave a crossing after the child moves; diagnostics report this explicitly.

Each cell is visited once. A relocated cell holds its new coordinates for
`cell_jump_fix_period` subsequent GP steps, then becomes movable automatically.
Zero disables the hold. Holds change only movement, not wirelength forces,
density charge, topology, or fixed-node identity. They apply to the new
propagation relocations; the supplied distance-one initialization is unchanged.
The optimizer retains its density/smoothing schedule and fillers, restarting
momentum and invalidating the selected-iterate history after each relocation.

`cell_jump.jsonl` records frontier sizes, moved cells, before/after crossing
counts, unavailable destinations, and displacement. `cell_jump_moves.tsv`
records exact old/new coordinates and hold duration. The feature neither
resizes nor buffers cells. Placement HPWL and RC timing remain preCTS estimates;
successful routing is a separate evaluation.

Checks: `//pass/place:cell_jump_test` and DREAMPlace's
`//dreamplace/native:placement_test` / `:gp_hooks_smoke` cover macro geometry,
masked nets, depth progression, live parent coordinates, bounds, fixed geometry,
halo/filler isolation, atomic rejection of invalid updates, and exact hold/release
duration.
