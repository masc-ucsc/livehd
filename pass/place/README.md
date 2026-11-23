# Physical placement stage

With ABC, OpenTimer and an explicit workdir, the default runs macro placement,
fixes the macros, and then places the cells. Without macros it uses a single
placement stage. Macro snapping remains enabled. The default `synth.place.fp=auto`
plans chunks before native full placement. A small design can use one chunk. To edit even a
single chunk at the floorplan GUI gate, set `synth.place.fp=chunks`. Set it to `false` to
bypass floorplanning, or `centroid` to use the older centroid experiment.

## Color and macro chunk floorplan

Use the staged worker flow to keep placement work and feedback for a selected set of physical homes.
Supply the usual synthesis inputs, Liberty and LEFs once. The project records the invocation and its working directory so relative sources and reader options can be replayed. For multiple workers, geometry screening, IO negotiation and independent timing checks, see [Comparing physical chunk placements](#comparing-physical-chunk-placements).

```sh
lhd synth chunk init design.v --top top --workdir W --budget 1GiB \
  --set synth.liberty=cells.lib --set synth.lef=tech.lef,cells.lef
cd W
lhd synth chunk list
lhd synth chunk show "$id"
lhd synth chunk macro "$id" --neighbors
lhd synth chunk final "$id" --neighbors
lhd synth chunk list --stage final --status done
lhd synth chunk gui
```

Set `id` to an ID from `list`. Run commands from the workspace or pass `--workdir W` to each.
After editing HDL, replay the saved recipe and run outstanding placements.

```sh
lhd synth chunk update
lhd synth chunk list --stage final --status stale
lhd synth chunk macro
lhd synth chunk final
```

`init` packs whole colors and macros across instance scopes up to the estimated worker budget.
By default it groups objects that share two-object nets and uses hierarchy order to
break ties. Pass `--set synth.place.chunk.connectivity=false` to `init` to pack in
hierarchy order. The packing order can affect both wirelength and timing. Home dimensions account for the placement
worker's clearance on each side of movable hard macros.
Use `update --budget 768MiB` to resize an existing plan, or separate workdirs to compare placements side by side. A no-change `update` retains completed work.
For an edit, the summary reports added and removed IDs, changes in physical membership and changes to placement inputs.
Existing macro/final results carry forward when membership, packet, worker, admission policy and shared IO still match.
A source edit that changes a reset tree or shared contacts can affect several chunks even when their membership stays fixed.
A failed update leaves the last successful floorplan active.

For machine-readable status and artifact paths, use `list --json`.
Macro and final process outstanding jobs by default. Supplying one or more IDs explicitly reruns those jobs. `--neighbors` adds one hop of
homes sharing an edge or overlapping the selected boxes. Workers run sequentially and read only their own
packets. The macro worker runs native mixed-size placement and legalization. Final seeds the cells from
that solution, fixes its hard macros and applies the packet's detailed-placement setting. When there is no current macro handoff, final starts from the original packet and places both cells and macros. A native memory's parent-floorplan pose seeds the independent worker, which can move that memory anywhere legal inside its home.
These stages use the isolated DREAMPlace recipe. Whole-design H2D, timing repair, multi-height cell
legalization, DEF stitching and CTS are outside this worker flow.

Macro placement (and final run directly from the packet) slides eligible shared contacts along their common boundary to reduce local HPWL. Set `synth.place.macro.pin_adjust=false` to keep planned contacts. The coordinator publishes the accepted coordinates to all incident homes before another worker starts. Die-anchored nets, contacts at disjoint interfaces, manual edits and current neighbor results retain their agreed positions.

`show` reports each port's net index and current die coordinates. Use
`lhd synth chunk move-io "$id" "$net" --from-macro` to accept the measured local pin centroid
projected onto the chunk perimeter, or supply `--xy X Y`. Each incident home projects that point to its
own perimeter. A change marks completed work on every home sharing that net stale, including the source.
Re-run affected stages to refresh their results.
Macro reruns invalidate the previous final. Results and failure logs are in attempt directories
under `.chunk-work` beside the manifest. The commands report their paths. State persists across processes
and the project switches to the new plan after a successful update. The lower-level `plan` command and `--manifest PATH` are available for standalone exports.

`init --gui` pauses at the floorplan editor. Use `lhd synth chunk update --workdir W --gui`
to reopen the plan with its accepted edits. On finishing the plan, the project publishes the new
worker packets and switches the same browser tab to the chunk placement controls. `lhd synth chunk gui` opens those controls
directly. Add `--gui-no-open` alongside `--gui` to work over the agent socket without launching a browser.

### Live search on isolated workers

Start with `lhd synth chunk init --gui --workers 1 --workdir W` and the design's synthesis arguments. The browser follows the accepted floorplan into its chunk workers. To reopen the search, run `lhd synth chunk gui --workdir W`. The worker limit also applies to concurrent trial branches. Packet export currently loads the connected design once.

From the same socket used by the GUI, try:

```sh
python3 tools/lhd_gui.py --workdir W macro 'ids=["Rocket.middle"]'
python3 tools/lhd_gui.py --workdir W select home=Rocket.middle kind=macro match=csr.__array_view
python3 tools/lhd_gui.py --workdir W trial home=Rocket.middle iterations=100 \
  'hypothesis=Move the CSR memories right or reduce density' \
  'candidates={"right":{"dx":1000},"less_dense":{"density":0.72}}' keep_if=control
python3 tools/lhd_gui.py --workdir W overview
python3 tools/lhd_gui.py --workdir W next objective=wns
python3 tools/lhd_gui.py --workdir W history query=CSR
python3 tools/lhd_gui.py --workdir W place-design
```

With native clock synthesis (`synth.place.pcrs.clock=true`), `tools/lhd_gui.py --workdir W clock` reports the built tree and the page's **clock tree** layer draws it with its estimated arrivals, slews and local skew. See the [PCRS clock notes](pcrs/README.md#viewing-the-tree).

A candidate can move a shared terminal with `{"io":{"net":17,"xy":[1200,2400]}}`. The control and each alternative advance the same incident homes sequentially. An unrelated worker stays frozen.

Moves use die DBU. Add `"fix":"x"` to hold the requested axis. A trial reuses the selected macro or final handoff. It runs each candidate and an unchanged control for the same iteration budget. `scope=assembled` scores physical pins across the exported design. `scope=local` includes virtual cut terminals. Older exports without scoring summaries support local scoring. Re-export to obtain assembled scores. A preexisting final result without a forkable handoff needs one local final rerun before a final-stage trial. `keep` promotes the measured files at their parent revision. `undo` restores that revision. Search results, annotations and frame references persist under the manifest's `.chunk-work` directory. `discard` closes a trial and retains its measured frames for `replay`.

`lhd synth chunk score --workdir W` combines the active net pin bounds with summaries of other chunks and fixed die pins. Each included physical net contributes its unweighted HPWL once. Stage counts identify the homes still represented by export seeds or stale measurements. Estimated setup WNS uses the exported timing snapshot and distributed-wire RC correction for changed pin spans. Missing timing is reported as null. `next --objective hpwl|wns|density` ranks chunks and reports the reason, cost and boundary feedback behind each choice.

The GUI shows accepted, selected, and best metrics for comparable stages. Use the branch selector or open a recorded finding to inspect its measured frame. Density is standard-cell area divided by available bin area after subtracting macro and obstacle coverage. Overflow is excess area above the selected density target divided by total standard-cell area. The GP displacement overlay connects sampled positions before legalization to the returned legal frame. Its movement count and maximum use all owned instances.

**Place design by chunk** runs outstanding local final workers sequentially, validates unique instance ownership and containment, and returns the assembled position manifest. Use `lhd synth chunk assemble --workdir W` to reproduce that manifest from current final results.

In **Floorplan chunks**, select a physical chunk. Under **Physical homes**, select one or more
colors or macros to move to another home or split into a named new home. **Fuse** keeps the
selected chunk ID and takes all members of the other home. Membership edits must fit the worker budget. Under **Move a physical chunk**,
enter the desired width / height and use **Preview shape** or **Apply shape**. Reshaping slides
a complete shared edge on the placement grid and adjusts every chunk along that edge. Locked
aspects, fixed macro locations, macro clearances and available area can rule out a shape.
A single chunk filling the outline has no shared edge. A rejected preview reports the reason.
Preview, Apply and Undo run the planner in the open session. Applying an edit publishes
a new floorplan scene to connected browsers and agents while the GUI stays at the same
editing gate. Finish the plan to export the accepted assignment for macro and final placement.

### Live floorplanning from the socket

Start with `lhd synth chunk init design.v --top top --workdir W --gui` and the usual synthesis,
technology and floorplan options. The browser and `tools/lhd_gui.py --workdir W` share a selection.
At the floorplan gate, click members or select by name. Rank connected colors by the number of planner
nets they share with the selected members. For example, to group the selected memories with two colors:

```sh
gui() { tools/lhd_gui.py --workdir W "$@"; }
gui chunks
gui connected kind=color limit=2 add_to_selection=true
gui split home=A new_id=CSR
gui reshape home=CSR aspect=1.5
gui finish-plan
gui macro 'ids=["CSR"]'
```

Use the actual source home returned by `chunks`. The split and reshape commands wait for the planner's
receipt. `apply=false` previews a change. `undo-plan field=geometry` and `undo-plan field=topology`
undo the most recent edit in the named field. Reopening a source-backed project replays its accepted guidance.

After macro placement, click a memory and Shift-click to add or remove others. Both browser and agent can
inspect the selection. A direct edit recomputes the legal placement and publishes it to the canvas.
Coordinates and gaps are in die DBU. A move gives the placer a starting position. Add `fix=x`,
`fix=y` or `fix=xy` to hold the corresponding coordinates. `release=x|y|xy` releases a hold.
Symbolic edge placements with holds use legal grid coordinates. Infeasible holds return a placement failure.

```sh
gui selection
gui edit align=row side=right gap=3000
gui undo
gui trial iterations=100 metric=hpwl keep_if=checkpoint \
    'left={"align":"row","side":"left","gap":3000}' \
    'right={"align":"row","side":"right","gap":3000}'
```

Each candidate and the unedited control start a fresh local solve from the same immutable physical
handoff, including the accepted cells, memories, contacts and coordinate holds. The reply includes
actual completed GP iterations, HPWL and estimated WNS deltas against both references.
`keep_if=checkpoint` accepts a strict improvement over the starting value. `keep_if=control`
accepts a strict improvement over the equally advanced control. With neither condition, the candidates
stay available for inspection. Use `view trial=ID branch=right` to follow a running or finished candidate,
`compare trial=ID wait=true` to wait, `keep trial=ID branch=right` to accept and `discard trial=ID`
to release the alternatives. Undo restores the accepted checkpoint, including coordinated contacts.
Intervening main edits invalidate acceptance of older trials.

With an SDC and synthesis timing enabled, export ranks the seed placement once with the configured
timer. Each timed home carries the constrained nets' seed slacks. Worker frames estimate the change
in each net's distributed-wire Elmore delay from its horizontal and vertical spans and report the
worst negative corrected slack in ps. This compares local geometry with cell and exterior delays
held at their seed values. It does not propagate slew or accumulate delay changes along entire paths.
The GUI reports the number of timed nets and shows unavailable timing for untimed packets.
The density overlay measures physical cell and macro body area in each of 16 × 16 bins.

The budget is in MiB and applies to worker admission and measured worker peak RSS. The producer still
materializes the flat physical design. To exercise partition creation below the worker's process baseline,
set `synth.place.chunk.require_fit=false` explicitly and inspect measured peaks in the stage results.

For whole-design QoR measurement, pass `--set synth.place.snapshots=true` to
`init` to save `synth/place/chunk-plan.def` with the mapped cells, connected nets
and physical terminals. This DEF contains the initial plan. Apply each completed
final worker's positions by instance name before measuring full-design HPWL or
running placement-based STA. Summing local HPWL includes virtual boundary pins
and can misstate the length of nets that cross chunk boundaries.

GP timing feedback is approximate critical-path ranking. It uses characterized
gate delay at total load and a closed-form Elmore wire estimate, updates the
existing timing graph for changed RC and gate strengths, and exports only pin
slacks and clock flags. Every timed non-clock net gets a weight from its slack rank
(worst `synth.place.timing_max_weight=2`, median exactly 1, best `synth.place.timing_min_weight=0.5`, linear in rank);
the sign and scale of the slack never matter, so designs where every net fails or every net passes are still ordered. The existing smoothing controls how quickly weights
change. This feedback does not claim WNS, electrical validity or a certified
period. Resynthesis obtains detailed timing for its selected placement frame;
physical repair validation and final STA retain the detailed waveform model.
The placement timing statistics separate ranking and detailed evaluations.

The primary GP metrics survive accepted timing-refinement rounds. Each attempted
round records its own GP metrics in `timing_driven.rounds`, and
`gp_total_iterations` includes the primary run and these refinement attempts.
Convergence and HPWL guards run before detailed timing; a rejected geometry
sample records `timing_evaluated=false` and does not update the detailed timer.

## Spatial partitions: grid, region, bin

Placement partitions the core in three roles. Each role has one name, and
options and docs use exactly these names. `grid` and `region` each have one
option prefix and one C++ type. `bin` has two prefixes (terminal
`legalize.bin.*`, SAT `satlegalize.bin.*`) and, until SAT is ported (ruling R5
in todo 3g-partitions), two capacity models.

| Role | Stage | Semantics | Options | C++ |
|---|---|---|---|---|
| `grid` | global GP (FP, macro, final) | Uniform tiles; cells deposit area/demand and read forces. No membership, never gates movement. | `synth.place.grid.*` | native `Options::bins_x/bins_y/auxiliary_bins` |
| `region` | detail final, after final GP and before legalization | Irregular rectangles cut at fixed macro edges and IO bus spans, at most `region.max_cells` movable cells each. Explicit membership; crossings, virtual pins and bus order live on region edges. | `synth.place.region.*` | `Region_mesh` (`region_mesh.hpp`) |
| `bin` | legalization | Uniform tiles with usable capacity, before/after occupancy and a hard density cap. Membership by apportioned area. Terminal: row sites minus macro halos and fixed WELLTAP/SPACER, fixed logic counted as occupancy. SAT: exact row area minus every non-movable component, fixed logic included. | `synth.place.legalize.bin.*`, `synth.place.satlegalize.bin.*` | terminal: `Capacity_bins` (`capacity_bins.hpp`; the legalizer fills capacity from its free row strips); SAT: private `Density_model` in `satlegalize.cpp` |

| Option | Default | Meaning |
|---|---|---|
| `grid.density` | `64` | density-grid bins per axis |
| `grid.density_mode` | `fixed` | `fixed`, `dynamic` (movable-area grid up to `grid.density_max`), or `macro` (32..64 from the macro size) |
| `grid.density_max` | `512` | dynamic-grid cap per axis |
| `grid.route` | `inherit` | the one routing grid for RUDY/RSMT, macro channel pressure, native legalization/detailed placement, terminal bins and SAT route-grid bins; `inherit` follows the density grid in use (`grid.density`, the chosen count in macro mode, and the configured `grid.density` in dynamic mode), or an explicit 2..1024 |
| `grid.local` | `512` | local spreading grid per axis |
| `region.max_cells` | `10000` | movable-cell cap per region rectangle |
| `region.merge_um` | `auto` | merge macro-edge/IO-span cut lines closer than this (auto: 20 row heights) |
| `region.busorder` | `true` | bus order (boundary pins with order); skipped when the mesh has one rectangle |
| `region.boundarypins`, `region.pinalign` | `false` | the other mesh consumers; their sub-options keep their names under the same prefix |
| `legalize.bin.grid` | `route` | terminal legalization bins only: the routing grid (bounded for tiny cores) or an explicit 1..1024 used as given. SAT route-grid bins keep `grid.route` |
| `legalize.bin.max_density` | `0.8` | hard terminal bin cap; must exceed the GP target |
| `satlegalize.bin.size_um` | empty | fixed SAT bins `W,H` µm anchored at the die origin |
| `satlegalize.bin.grid` | `none` | `route` uses the routing grid for SAT bins; excludes `bin.size_um` |

Stage-scoped grids keep their names: `preroute.bins` (64). The pre-3g spellings (`bins`, `seed`,
`bin_max`, `local_density_bins`, `dynamic_bins`, `auto_bins`, `busorder*`, `boundarypins*`, `pinalign`,
`legalize.max_density`, `satlegalize.region_size`, `satlegalize.route_grid`) are retired: `--set` refuses
each one with a usage error whose hint names its replacement.

`Cell_criticality` (`cell_criticality.hpp`) computes per-cell timing inputs
(min own-pin slack, clock contact, untimed pins) for SAT spill eligibility and
its diagnostics, and for the terminal legalizer's per-cell slacks. Each
consumer keeps its own movability rule. Bus order and boundary pins share
`node_weight_criticality()`/`limit_displacement()` instead.

`synth.place.io_resynth=5` enables placement-aware whole-color ABC resynthesis.
At each completed stage, OpenTimer's in-memory pin arrivals/required times are
reduced to region port bits; clock and reset nets are excluded. A near-critical
color is remapped only if an IO constraint on a near-critical path tightened by
more than 5% of the clock period. A scoped fanout-to-Steiner-length wire-load
table supplies ABC's capacitance estimate. Timing must improve under the
same environment and a bounded combinational CEC must prove the replacement.
State and memory instances remain opaque and retain their interfaces.

Resynthesis requests 90% of the actual SDC clock period, giving 10% headroom.
The report records both `required_period_ps` and `requested_period_ps`; the
initial synthesis delay hint does not define the physical setup objective.
A color that missed its previous synthesis target is not tried again. A color
that met the requested target is skipped while the new period and cumulative
IO tightening remain within that saved margin. Target history survives stage
restarts, boundary cell resizing and workdir reloads, and is invalidated by a
fresh color synthesis or incompatible timing engine. A stage meeting the actual
physical setup objective skips resynthesis altogether.

Full synthesis placement shares a cell-count and physical-area growth limit of
5% (`synth.place.growth`) over the original mapped standard-cell netlist. Gate sizing may
use only `synth.place.growth.sizing_share` (default 0.3) of it; buffer insertion may use all, so sizing cannot
spend the headroom a high-fanout net needs for its buffer tree (measured on eyeriss: 3% with sizing first left
0.07 um2 for a 271-load net; 5% with a 0.3 share takes WNS from -11.0 to -6.9 ns).

**Electrical repair.** The placement timer reads every sink the way OpenSTA's dmp delay calculator does: the driver
output is a saturating ramp and the sink is that ramp through one pole of the sink's Elmore delay
(`pass/timing/engine.cpp`, `ramp_through_pole`), so a long, heavily loaded net reports the multi-ns slew OpenSTA reports
at its far sinks (the impulse-variance slew used before was ~2x low there, and a wire's delay is the ramp's crossing,
at most its Elmore delay). Every pin is checked against Liberty `max_transition` (the library `default_max_transition`
for pins without one) and every driver against `max_capacitance`. Tree repair proposes a net whose far sinks break
`max_transition` even when its strong driver does not (reason `sink_slew` in `qor.place.repair`) and spends the growth
allowance on the worst net stress first. Once setup is met, `pass.place.repair_electrical` runs one tree sweep with
`synth.place.growth.electrical` (default 0.3) of extra cell/area allowance on top of `synth.place.growth`, usable only
by nets that break an electrical limit; while setup still fails electrical trees stay within `synth.place.growth`, so
they cannot take the room the timing repair loop needs. Measured on HighTide (virtual buffers on, OpenSTA): litepci WNS
+258 -> +1041 ps, max-slew violations 7.9k -> 5.9k, max-cap 492 -> 158 (cells +12%, HPWL +28%); liteeth +98 -> +152 ps,
slew violations 534 -> 118; litedram slew violations 339 -> 207; sha3 +220 -> +235 ps; minimax +65 -> +2 ps (LiveHD's
own slack now within 10 ps of OpenSTA's instead of 24 ps optimistic).

**Buffer-tree planner API** (`steiner_repair.hpp`, reused by any step that must drive a positioned sink set):
`plan_buffer_choices(root, loads, capacitances, required_ps, drives, driver, wire_r_ohm_dbu, wire_c_ff_dbu, limits,
effort, min_topologies)` returns a `Buffer_tree` (buffers in postorder, each with a position, its loads and a drive
index into `drives`; `root` lists what the original driver keeps). Geometry is DBU, capacitance fF, time ps.
`Buffer_drive` describes a candidate cell (or the original driver): input cap, `max_cap_ff`, `max_fanout`, intrinsic
delay and drive resistance, and area. The planner generates
spatial topologies, picks drives bottom-up by required time with the Elmore delay over each branch's Steiner tree,
splits the root when the original driver would break its own cap/fanout limit, and rejects
trees beyond `Buffer_limits` (cells/area of the growth allowance left). It only proposes: callers apply the tree and
accept it with fresh STA. `LHD_STEINER_TRACE=<n>` explains every rejected candidate on nets of at least n loads, and
`LHD_STEINER_DUMP=<file>` records the inputs of a failed plan for `//pass/place:steiner_replay_test`.

**Repair loop** (`synth.place.repair.iterations`, default 3; 0 = a single pass). After the first sizing and buffering
pass the placement those edits were computed for is stale, so the flow re-places warm with the timing weights of the
repaired result (a new buffer is a timed net: its slack rank pulls it to its loads), then sizes and buffers again. An
iteration is kept only while `timing_improves` holds, and iteration k may use (k+2) times the growth allowance, so
growth is bounded by whether it still helps, not by a fixed cap. Measured (pre-CTS, OpenSTA): eyeriss WNS -6.9 ns ->
-0.65 ns and TNS -25.3M -> -0.24M ps (OpenROAD: -2.6 ns, -1.2M), cnn -8.7 ns -> -1.3 ns and -16.5M -> -0.23M
(OpenROAD: -7.5 ns, -12.1M), for +8..14% cells and roughly 35 minutes more on these 270k-cell designs.
`qor.place.repair_loop` reports every iteration. The inventory precedes
synthesis-buffer cleanup and counts every occurrence of a shared color; macro
area cannot subsidize standard-cell growth. The reset and domino rail tree
buffers (`__place_rsttree*`, `__place_railtree*`, below) are structural fanout
restructuring done before the first cell is read, not growth: the inventory
leaves them out, so a default-on tree does not spend the repair headroom.
Whole-color replacement and boundary
master changes are checked before publication. Placement sizing (including
`synth.place.domino_sizing` variant steps and output buffers, `rejected_growth`),
buffer repair and cone resynthesis check their private candidates before RC/timing evaluation.
Oversized candidates retain the accepted graph. With detailed per-pin electrical
feedback, batch repair visits the largest electrical excess first, then the worst
setup slack. Without that feedback it orders by slack alone. This prevents a
severely overloaded source from losing the limited area allowance to downstream
gates on the same paths. Each complete edit is checked against the remaining
headroom before mutation; an oversized forest does not discard other useful
edits. Its inventory is computed once per batch and updated after successful edits. The
`growth_budget` object in placement QoR (also exported as
`placement.json`) records the baseline, current inventory and rejected proposals.
It also reports the constant-cell subset by Liberty master, including its baseline
and area. These cells remain fully charged: the breakdown exposes required tie
materialization before repair without disguising it as repair growth or raising
the allowance. Tie creation indexes existing cells and prior tie positions once;
per-pin ownership, names and connectivity are unchanged on fresh and reused runs.
The repair diagnostic initializes its baseline from the supplied mapped graph.

One automatic batch is allowed: either before placement using saved final
feedback, or after the first completed stage. An accepted batch restarts that
stage, seeding surviving cells (including SRAMs) and IOs from its placement.
IO coordinates are reused only when the die is unchanged; a resized frame keeps
its newly generated perimeter IO seeds. Fixed and locked IOs are preserved.
These are movable initial coordinates: the restart does not copy fixed status,
add position bounds, or lock unchanged colors. The normal macro lock happens
after macro placement. Saved-feedback resynthesis before placement can use the
previous completed placement as a hint. `restart_seed.json` in the restarted stage reports reused
movable cells/macros and new cells. Under H2D the seed's IOs (same die) are
kept before macro planning, whose frame identity includes every IO, so an
unchanged macro frame is reused; a fresh H2D solve, which already legalized
every cell, supersedes the seeded cell coordinates
(`superseded_by_macro_solve`). Later stages save
feedback and warn when another run could help. The per-color decisions and
used/placed environments are in `synth/place/io_resynth.json`; reusable state is
in `abc_cache/io_timing.json` and `abc_cache/wire_load.json`. Use a distinct
`--workdir` for each major placement configuration. `lhd.incremental=false`
disables reading/writing reusable state but permits feedback within this run.
After a successful full invocation, `physical_cache/` retains the completed
repaired netlist, including instance-specific color edits and inter-color
buffers, its placement/RC attributes and final boundary measurements as one
generation. An unchanged invocation restores that generation without repeating
IO resynthesis, placement or physical repair. The restored IO report records
zero new resynthesis attempts and identical used/measured boundary values;
`reused_final_state` identifies replay, and `pass.place.restore` measures it.
This avoids applying final feedback to the earlier, unrepaired ABC body.
The gate checks compiled graph/attribute bytes, all explicit options, engine
identity, Liberty/LEF, SDC and physical RC/floorplan contents. Missing, changed
or damaged generations fall back to ordinary per-region synthesis and physical
optimization. After an RTL/color edit, matching physical settings and technology
allow the verified completed DEF to seed surviving instance names, including
unchanged colors. `pass.place.seed` records this fallback. These coordinates are
movable hints, not repaired-netlist replay: fixed/locked status comes from the
current input, new cells get fresh seeds, and connectivity, RC, timing and
legality are recomputed. IO hints require an unchanged die. Macro planning can
refine the hints before its ordinary macro/IO freeze and final-stage handoff.
Incompatible settings, changed technology and damaged DEFs skip the hint.
Artifact digests are checked before restoring any files.
The IO timing snapshot records the timing-engine source identity. A timer
change discards its old measured arrivals/required times before the pre-placement
trigger check, while preserving the constraints used to map the body and the
geometric wire-load tables. Fresh placement then supplies current measurements.
Set `synth.place.io_resynth=0` to disable the loop and retain the single-call
non-FP flow. Explicit positive values require a workdir, ABC, full placement,
OpenTimer, and a resolvable SDC clock. No IO timing JSON is exchanged during GP
timing callbacks; the two state files are stage-boundary snapshots.

Before a fresh full synthesis placement whose later `synth.place.resize`
phase rebuilds the buffering, `remove_synthesis_buffers` makes a private
physical copy and bypasses internal, scalar, non-inverting Liberty buffers.
`synth.place.buffer_cleanup` (`auto`, the default) runs it only then: timing-
driven placement and resynthesis do not re-buffer, so without resize (for
example `synth.timing=false`) the synthesis buffers stay; `true` forces the
cleanup and `false` never runs it. Input/output binding cells, fixed cells and clock/reset distribution
cones remain, including controls crossing region boundaries. Logical mapped
definitions and their resynthesis identities/history are unchanged. The cleanup
also runs on the new physical copy after an accepted color restart, but never
on repair/ECO trials. `buffer_cleanup.jsonl` records removed and retained counts.

### Cascaded clock gates (`synth.place.clock_gate_flatten`, default on)

With buffer cleanup, a clock-gating AND cell (an AND whose output reaches clock pins) whose input is another
clock-gating AND's output takes that gate's inputs instead: `AND(AND(clk, q0), q1) -> AND3(clk, q0, q1)`, up to the
widest AND cell of the library. Every gated domain then sits one gate below its root clock, so a downstream CTS
balances it against its sibling domains instead of starting its tree after the first gate's (HighTide coralnpu: 7,651
flops two AND2 levels below `io_aclk`). The function is identical and the enable latches keep their clocks; the inner
gate stays for its other loads and is deleted when unused. Data ANDs, fixed and design-instantiated cells are untouched;
`buffer_cleanup.jsonl` reports `clock_gates`.

### Wirelength-aware cone resynthesis

Full placement with timing enables both wirelength-aware and proved critical-cone
resynthesis by default. Disable either independently with `--set synth.place.wl_resynth=false`
or `--set synth.place.resynth=false`, respectively. After global placement, LiveHD identifies virtual-buffer
roots and long data nets using the technology's repeater length. It measures
where the loads cluster and what ordinary buffer expansion costs. Candidates
can move a driver, copy logic or a resettable register near a load cluster,
factor a gate with USYN, or move a common inversion ahead of the fanout.
For example, one complemented driver can replace a bank of sink inverters.
For multi-input sinks, their distinct side inputs retain their original connections.
Each rewritten term binds directly to an available Liberty cell. An XAG
truth-table check verifies the chosen input order and output polarity.
Register copies require a characterized, single-output flop with one
externally driven asynchronous reset. After reset, the original and copy
have the same initial value and transition function. The cost estimate
includes the extra load on the inputs of copied gates.

Candidates compete for available row area. LiveHD legalizes each trial on a
private copy and times the result, including any remaining buffer trees. An
accepted trial must improve total pin HPWL, or tie with fewer cells, while
preserving the placement timer's setup and electrical checks. Independent
OpenSTA and extracted post-route setup can differ from the placement estimate.
A rejected trial returns the ordinary buffer-expanded placement.

The placement work directory contains `wl_resynth/report.json`. Its `targets`
array gives each eligible root's measured load spread, cone and timing boundary.
The `trials` array records rejected and accepted batches. The `edits` array
identifies the last batch and its proposed transformations. The recipe records
the number accepted and the number of trials. When routability triggers an area
restart, `wl_resynth/history.jsonl` retains the decisions for earlier attempts.
Ordinary buffer counts are in
`virtual_buffers/report.json`.

### Virtual buffer trees (`synth.place.virtual_buffers`, default on)

A fanout buffer tree built at mapping time groups loads before any position
exists, so its buffers' loads end up scattered and global placement cannot
untangle them (HighTide: 1.4-2.5x OpenROAD's HPWL with the trees, 1.06-1.3x
without). With virtual buffer trees the cleanup above converts every tree into
one *virtual node* instead of keeping wide trees whole
(`buffer_cleanup.max_fanout` then does not apply):

- **Representation.** The tree's root driver, its final loads and the mapper's
  fanout cap (`synth.max_fanout`) plus the root buffer's master, keyed by the root
  driver's physical pin label (`Options::virtual_buffer_trees`, also counted in
  `buffer_cleanup.jsonl`). Polarity-neutral inverter pairs -- an inverter whose
  every load is another inverter, from the mapper or an imported netlist -- are
  collapsed onto the first inverter's driver and recorded the same way. Cells the
  design instantiated (`attrs::user_cell`), fixed cells, ports and clock/reset
  cones are never bypassed. Only the private physical copy changes: logical
  definitions, mapper cache keys and warm-run reuse are untouched, and a flow
  without placement keeps the mapper's balanced tree (the fully mapped fallback).
- **Before expansion.** Global placement and its timing checkpoints see the
  root driving every load directly. The fast timer does not time that direct
  drive: a recorded root whose direct net is over the fanout cap *and* over the
  driver's Liberty limits (the expansion's own test: load against
  max_capacitance, or the output slew at that load, degraded through the
  longest sink wire, against max_transition) is timed as the balanced tree the
  expansion will build (`Wire_model::Virtual_tree`, set by
  `Physical_ranker::set_virtual_roots`). L = ceil(log_fanout N) levels of the
  recorded root buffer master with branching b = ceil(N^(1/L)); the root drives b
  buffer inputs plus its share of the net's wire capacitance (split evenly over
  the L + 1 stages, then over each stage's drivers); every sink sees the L buffer
  stages (Liberty delay and slew at each stage's load) and each stage's wire,
  whose Elmore delay is the stage's share of the direct path resistance times the
  whole stage load, capped at its share of the direct wire delay. A root within
  its limits stays direct, as the expansion usually leaves it. Correlation on six
  HighTide designs (estimated against realized worst root-to-sink delay of the
  expanded nets): median ratio about 1.0; about 40% of the expanded nets come out
  optimistic, mostly small ones the expansion's stricter lumped test expanded
  while this model timed them direct. Taking the slower of direct and tree for
  those was tried and rejected: it moved litepci from +872 to -645 ps. The model is cleared before
  the expansion: from there the netlist is timed as it is. The direct drive had
  made post-synthesis timing, and the placement weights seeded from it, far too
  pessimistic (NVDLA/partition_a: 6,968 ps worst delay against +354 ps final).
  `LHD_NO_VIRTUAL_TREE_TIMING` restores the direct timing;
  `LHD_VIRTUAL_TREE_DEBUG` writes `virtual_tree_correlation.jsonl` (per root:
  loads, expanded, the estimated and the realized worst root-to-sink delay).
- **Expansion.** `expand_virtual_buffers()` runs once global placement has
  converged, before timing-driven placement and repair. A recorded root whose net
  still has more loads than the fanout cap *and* is electrically overloaded
  (placed wire RC plus sink pins against the driver's Liberty max_capacitance and
  a lumped 10-90% slew estimate against max_transition) gets a tree from repair's
  timing-aware planner (sink positions, drive ladder, required times). When that
  planner finds nothing to split although the net is overloaded -- a timer that
  misses the far sinks' slew -- the mapper's fanout- and capacitance-bounded
  spatial tree (`plan_buffers`) is built regardless. Nets within limits stay
  direct; timing-driven repair may still split them. Expansion is mandatory, not a
  timed trial. New cells are legalized by the single-cell ECO and the result is
  re-timed; `virtual_buffers/report.json` records roots, expanded nets, buffers,
  and the displacement figures below.
- **Space.** No area is reserved during GP. The bypassed trees' area is
  already in the density budget, and expansion inserts far fewer buffers than it
  removed (minimax 445 vs 1149, sha3 562 vs 701, litedram 416 vs 1610), only on
  overloaded nets. Measured on HighTide: new buffers land 0.4-0.7 um (mean,
  max 3 um) from their planned positions and 5-128 existing cells move, by 0.6 um
  on average. Macro designs show a few long moves where a planned buffer falls on
  a macro or in a full channel; the macro row strip keeps channel headroom.
- **Disable** with `synth.place.virtual_buffers=false` (wide trees stay whole,
  the `buffer_cleanup.max_fanout` cap) or `synth.place.buffer_cleanup=false`.
The synthesis host releases that cleanup graph once the zero-wire timing seed
owns its flattened copy. The seed keeps RC in memory without serializing a second
copy into graph attributes; completed placements still persist their RC for
standalone timing and reload. The synthesis host retires seed and cleanup
libraries as soon as placement has made its last independent copy, before that
stage's GP/RC work. Staged macro placement retains its input until the final
cell stage copies it; the original synthesis netlist remains for resynthesis.
Ordinary GP timing checkpoints borrow the current solver coordinates and the
existing connectivity instead of copying the entire placement view. Exported
graph/DEF poses remain rounded to DBU while RC keeps fractional pin positions.
Legal timing snapshots still capture row orientations and rebuild pin offsets;
returned RC and floorplan snapshots own their data.
The lightweight ranking callback receives graph, RC and the current topology's
control classification through `Ranking_view`. Ordinary GP ranking omits the
unused pin-location map and builds the weighting floorplan after timing retires
the preceding RC generation. Detailed callbacks and selected macro-GP handoffs
retain pin-location data; reset/clock cuts follow the same classification.
The imported-GP diagnostic exposes the same operation through
`remove_synthesis_buffers=true`; its component-count check accounts for exactly
those removals while retaining fixed-cell and master checks.

Reset and domino rail buffer trees reuse the placement technology snapshot for
Liberty cell models. Repeated placement phases do not reopen the library to
identify asynchronous reset pins or choose the smallest placeable buffer.

A supplied core can have too little free area to reach the requested soft density;
a fine density grid can also make individual cells unavoidably overfill bins.
For static cell GP, native placement computes an unavoidable overflow floor from
movable charge and fixed-bin capacity, and from the minimum bin excess of each
cell over all grid positions. The stronger conservative bound controls handoff.
If the floor exceeds `stop_overflow`, it uses excess above that floor to schedule
criticality samples, stop increasing density pressure and settle before legalization. No option default or density
target changes. `gp_target_density`, `gp_overflow_floor`, and the history's
`overflow_floor` expose the distinction; an early handoff reports
`gp_stop_reason="density_infeasible"` and `gp_converged=false`. Minimum iterations,
objective/routing checks and final legality still apply. Strict upper-bound
placement, continuation, moving macros and scheduled/reserved geometry retain
their existing policy.

`synth.place.resize=true` enables timing-checked sizing and buffering in full
placement with OpenTimer. Explicitly disable it for a geometry-only comparison.
The shared synthesis defaults use coarse partitions, state sweeping and register
merging, a delay budget of 0.4 of the SDC clock period when available, ABC fanout
32 with area relaxation off, and flat reset fanout 32. Explicit options override
these defaults.

`lhd synth --set synth.place=true` runs CPU DREAMPlace between mapping and STA.
The default remains false. `synth.lef` is a comma-separated tech LEF and cell/macro
LEFs; its sky130HD fallback is next to the Liberty directory in
`LHD_TECH_DIR`. Locate installed PDKs with `ciel ls --pdk sky130A` and
`ciel path --pdk sky130A VERSION`.

`place.cpp` flattens into a separate library without editing the input, resolves
mapped physical pins, generates/imports a floorplan, runs GP, assigns IO slots,
and runs GP plus legalization/detail. Flattened names keep every instance scope
so they stay unique across separately mapped regions. A named instance keeps
its name. An anonymous one (a `pass.partition` region wrapper) is spelled
`__flat___sub_<id>`: a reserved transparent component that `logical_hier_name`
drops, so a placed register, memory or domino cell carries exactly its source
state name and `lhd lec` pairs it by name. Netlists placed before this used
`sub_<id>`, which hid the source names from the state pairing (NVDLA
partition_m: 2074 unpaired states). Integer DBU positions and orientation live
on the flat graph. Its input node persists placement RC and per-bit IO positions;
standalone `pass opentimer` recovers this data and rejects changed cell positions,
orientations, masters or sizes before cache replay.
The native fork's HHDS adapter expects a graph containing physical cells only.
LiveHD's flat mapped graph also contains Get_mask, Concat and other bus aliases,
so the host resolves these into the fork's public flat-array interface while
keeping the original graph's bus ports and logical connectivity.
Always-transparent latch declarations from fully assigned combinational regs
are bypassed only in the private physical graph; gated or resettable latches
still require cell mapping. DEF input nets retain their port names, and internal
net names reserve every port name, so OpenROAD's structural Verilog writer keeps
input connectivity when exporting a DEF-imported design.

Physical constant drivers are discovered from `synth.liberty`: usable input-free
cells with scalar output functions `0` or `1`, including dual-output ties. The
selection excludes `dont_use`, sequential and three-state cells and requires a
matching row-compatible CORE LEF master with the declared signal pins and output
geometry. Masters with LEF `SYMMETRY X` rank first, then the smallest physical
area wins, followed by cell/pin name for a stable tie break. A tie that cannot
flip turns generated N/FS rows into all-N rows (a frame reused from `cache_dir`
counts as generated, so warm and cold runs agree); an imported floorplan or a
caller-supplied previous floorplan with FS rows rejects it. Supply pins remain physical power/ground pins. No technology, cell or
signal-pin spelling is hardcoded. Both the physical nets and private graph use
the selected constant output; later placement/ECO rounds reuse existing ties.
`synth.place.preserve_imported_ties=true` (helper JSON `preserve_imported_ties`)
keeps characterized imported shared tie cells and their exact graph/physical
endpoints for fixed-netlist geometry replays. Shared sources keep their ordinary
GP/legalization positions rather than following an arbitrary single load. The
default remains per-load splitting; logical constants still materialize real
per-load sources under either setting. The option participates in cache identity.
Missing characterization or geometry produces an explicit error for the required
constant value. Library contents participate in placement-cache identity.

Full mode checks site/row alignment, cell overlap and core bounds independently,
then writes and reads back `synth/place/out.def`. GP mode may overlap, reports
approximate timing and removes the stage's DEF. `synth/place/qor.json` reports
legality separately from GP convergence/overflow, plus HPWL and placed WNS/TNS
when STA supplies them. `--emit-dir report:` exports this as `placement.json`.
`--emit-dir def:physical/` retains `out.def`, copies the ordered technology and
cell LEFs under `physical/lef/`, and writes their relative paths to `lefs.json`
(including a memory proxy LEF when present). These exports are part of the
synthesis invocation; downstream independent timing analysis is separate.
An unmet GP overflow target is reported even when legalization succeeds.

Native unweighted HPWL omits both clock and reset distribution nets. For an
external comparison that omits only clocks and retains physical reset wiring,
`//tools:placement_metrics` reads the exported DEF, ordered LEFs, Liberty and
native `qor.json` control-role metadata. Its `clock_excluded_hpwl_um` uses the
same pin geometry for either placer; `reset_cut_nets` identifies reset wiring
to exclude from external timing before propagation. `--check-native` verifies
the control-net census and native data HPWL (within 0.001 µm). Explicit control
overrides lack the required clock-role metadata and are rejected by this tool.
The tool does not perform STA or modify placement.

For legalization displacement, add `--reference-def before_legalization.def`
while `--def` names `after_legalization.def` (or `after_detailed.def`). Enable
`synth.place.snapshots=true` explicitly when collecting those stage files.
The optional `displacement` object reports instance-origin Manhattan movement in
microns, separately for standard cells, macros and physical-only cells, plus the
all-instance total. Each group includes moved counts, total/mean/maximum
movement, orientation/status changes and movement of previously fixed/locked
instances. Empty groups have null mean/maximum. Matching design, DBU, instance
names and masters are required; unplaced or mismatched inputs fail instead of
silently comparing their intersection. Terminal movement is outside this metric.
All existing HPWL fields and behavior are unchanged without this option.

GP also waits for timing-weighted HPWL and routability after geometry/density
settle. `synth.place.post_density_factor=4` allows up to four times the first
eligible iteration count as **additional** iterations (40 becomes a deadline
of 200); the configured iteration cap still applies. Set it to zero to replay
the previous density-only stop. Two consecutive 20-iteration samples must have
relative weighted-HPWL and inflation changes at most
`synth.place.convergence_tolerance=0.01`, and the active routing estimate must
meet its overflow target when routability is enabled. Measured OpenTimer setup
violation is reported when available; unmet timing does not veto geometric
convergence. Weighted HPWL serves as a timing proxy, not a claim of measured
timing closure. New net weights or topology reset
the stability window but never extend the continuation deadline. Satisfied
density does not keep increasing the density penalty during this continuation.
The GP/FP JSON records the deadline, budget exhaustion, and whether timing was
observed; a legal placement stopped by the budget is not reported as converged.
Static cell solves also stop automatically when six samples spanning at least
100 iterations show less than 1% variation in both HPWL and density overflow,
despite at least a fourfold increase in density pressure. This detects a possible
density grid floor rather than spending the remaining budget on it. The handoff respects the
minimum iteration count, requires satisfied routing and unchanged inflation,
and waits out the last checkpoint. Moving macros, continuation and scheduled
objectives retain their own stopping policies. The report records
`gp_stop_reason="density_stalled"`; legalization still runs, and this stop never
claims GP convergence.
With feasible density, a static cell solve can similarly hand off on a routing
plateau. Six samples spanning at least 100 iterations must keep HPWL and
inflated area within 1%, with less than 1% improvement between the best routing
overflow in each half of the window. All samples must use the same routing
model, follow the last checkpoint, and meet the density target. The same
minimum-iteration and scheduled-objective restrictions apply; dynamic grids and
explicit routing probes retain their own policies. This stop records
`gp_stop_reason="route_stalled"` and does not certify routing or GP convergence.
For the small full-sky130 trial, `synth.place.grid.density=16` and
`synth.place.iterations=1500` reached the overflow target; the earlier 32-bin/500-iteration
trial legalized but did not converge in GP. This is fixture evidence, not a
universal tuning recommendation.

Parasitics use FLUTE with the first horizontal routing layer and the following
vertical layer in LEF stack order. Sheet resistance/width and area/edge
capacitance become ohm/um and fF/um. `synth.wire_rc=R,C` overrides both
directions. RC enters OpenTimer in memory; there is no SPEF file round trip.
This is a placement estimate, with no routing, via, coupling or clock-tree model.
`synth.place.tracks=path/to/make_tracks.tcl` supplies the router's actual grid
before macro alignment. It accepts literal `make_tracks LAYER -x_offset N
-x_pitch N -y_offset N -y_pitch N` records in microns, with comments and line
continuations; it does not execute Tcl. Every record requires all four values.
An explicit file replaces imported tracks. Otherwise an imported DEF grid is preserved.
For an ASAP7 design without tracks, LiveHD uses its platform grid, including the
seven M2 row phases, before legalization and DEF export. Other technologies
without a platform grid fall back to LEF pitch and offset. File contents are
included in placement-cache identity. Tracks are exported in DEF and must be
preserved by the downstream flow rather than regenerated after changing DIEAREA.
This matters when platform track offsets differ from technology LEF offsets.
At final placement, movable IO pins use the registered horizontal/vertical edge
layers when both track grids are present, even without a native power grid.
The handoff sizes and spaces the pin metal for routing; fixed IOs keep their
supplied shapes and locations.

`synth.wire_rc=path/to/setRC.tcl` reads characterized technology data when
RC is not supplied by LEF. The reader accepts literal `set_wire_rc` / `set_layer_rc`
commands and explicit `set_cmd_units` / `set_units`; it parses data without running
Tcl or OpenROAD. Resistance/capacitance units otherwise come from the first main
Liberty header, so appended SRAM libraries cannot change the technology units.
Explicit per-length units normalize to ohm/µm and fF/µm. Unsupported commands,
expressions and corner-specific directives fail with a diagnostic. An explicit
`wire_rc` pair takes precedence. Placement timing and Steiner repair share this
resolver, and `wire_rc.json` records the estimated model, source and resolved
horizontal/vertical values. The RC file participates in input provenance/cache
identity. Missing technology characterization is an error rather than a guessed
material constant.

An unknown (X) constant that reaches a mapped net is refused by name by default: a
physical net must carry a value, and the RTL left it unspecified (`assign y = 'x`,
an `'x` default no branch overrides, an unconnected cell input, an undriven top-level
output or a struct field nothing writes). `synth.place.unknown_const=zero` is the
opt-in refinement: every such bit is tied low through the same tie cell as a logical
0 (any value is a legal refinement of a don't-care; yosys `setundef -zero`, which the
OpenROAD flow applies before its tie cells, makes the same choice), and a
`unknown-const-tied-low` warning reports how many bits it touched. It is part of the
placement cache identity.

Nets beyond FLUTE's 1000-location degree limit use approximate spatial groups
and a representative FLUTE tree. Coordinate coalescing can introduce cycles in
that union; cyclic unions retain a resistance-weighted spanning tree, preserve
pin attachments and recompute retained wire capacitance. Already valid trees are
unchanged. Both the producer and OpenTimer boundary require a connected resistor
tree. Placed timing includes the last wire segment into primary outputs and native
memory captures.

`place_cache/placement.json` follows `lhd.incremental` and a build content salt.
It stores cell positions and block bounding boxes keyed by instance context,
color and physical connectivity. Matching blocks are fixed in the placer;
imported FIXED objects remain fixed if a placement retry releases cache locks.
Changing only wire RC can reuse placement while invalidating STA. A compatible
cached `floorplan.def` keeps the previous core and IO frame. Changed blocks seed
old cells at their previous positions and new cells near their block anchor.
Windowed GP/LG/DP starts around changed, added and removed cells, locks cells
outside the window, and expands the window until the trial legalizes. Independent
whole-core legality is checked afterwards. QoR reports the window, retries, moved
cell count and maximum displacement. Explicit in-process ECO trials use the same
path without requiring a persistent cache hit. New buffer and cone cells retain
the replaced root's `placement_block`, color and synthesis-region provenance.
Displacement is bounded by the chosen window; there is no separate per-cell
radius constraint, and solver momentum is not reused across calls.

Floorplan input requires the same DBU as LEF and N/FS rows on a common site
lattice. Segmented rows and missing row bands are supported: the solver uses
a rectangular frame with hard exclusions for absent sites. A row cut under an originally fixed macro
still excludes standard cells. Macro checks and routing use the macro body and its LEF obstructions there;
the surrounding notch and any explicit hard blockage remain excluded. Exported DEFs,
timing snapshots and repair handoffs retain the original row segments and
blockages. Overlapping segments, inconsistent orientations within a row band,
off-grid segments and rotated rows are refused.
Fixed physical-only WELLTAP/SPACER cells may occupy macro spacing halos, as in
terminal legalization; their actual bodies still must not overlap any cell.
Imported IO pins must be FIXED unless IO adjustment is enabled;
PLACED pins then supply movable initial positions, and UNPLACED pins receive
generated perimeter seeds. Explicit FIXED pins remain fixed.
Mapped standard cells need matching LEF and Liberty models. Explicit hard macros
remain movable and retain exact LEF dimensions, even when their widths/heights
are not integral standard-cell sites/rows. DREAMPlace uses its existing macro
legalizer, which rounds private occupancy envelopes for packing without changing
physical dimensions or pin offsets. Row flips apply only to CORE cells; BLOCK
macros keep their supplied orientation, including when a warm cache hit locks
them.

Real BLOCK macros align their actual origins to signal-pin access on routing
tracks before standard-cell packing. Imported DEF TRACKS take precedence;
explicit LEF layer offsets and pitches provide a fallback. The search transforms
pin shapes for the retained macro orientation and honors LEF MANUFACTURINGGRID,
without changing master dimensions. Invalid fixed macro origins are diagnosed
rather than moved. Synthesized memory proxies carry abstract RC geometry and
are excluded from routing-track alignment. This correction adds no option or
default change.

`synth.place.halo=auto` (the default) reserves ten selected placement-row heights
per side. An imported or previous frame uses its row SITE; a generated frame
uses the selected mapped-cell SITE. The resolved value is recorded in QoR and
rounded outward to site/row pitches before placement and legality checks.
An explicit `synth.place.halo=H` reserves H microns per side around every non-CORE LEF
macro, including native-memory BLOCK proxies. `synth.place.halo=X,Y` selects
horizontal and vertical clearance separately, including an explicit zero. Clearance
rounds up to the technology's site width and row height. CORE standard cells
receive no halo. Physical dimensions, pin offsets and reported physical area
remain unchanged; private padded occupancy participates in density, legalization
and detailed placement. Two macros reserve both halos between them. By default,
the same clearance applies to cache reuse, ECO placement and independent legality checks;
an imported fixed macro with an illegal internal halo cannot silently pass.
For FIXED/LOCKED macros, halo exclusion is clipped at the core boundary while
the physical body must remain inside and all internal clearance is preserved.
Native movable macros keep their complete halo inside the core. Requested
microns and effective DBU clearance are recorded in placement QoR. Floorplan
utilization continues to use physical area. Generated cores also reserve
capacity for padded macro occupancy at the target density, so a large halo
cannot consume the entire frame and strand the standard cells. Explicit
die/core or imported outlines remain authoritative and may still be infeasible.

`synth.place.macro.channel` is the minimum clear spacing between two macro
bodies, separate from the halo (as OpenROAD separates `rtl_macro_placer
-min_channel_size` from the per-macro cell keep-out). `auto` (the default)
derives it from the technology: the largest minimum metal spacing (plain
`SPACING`, or the first `SPACINGTABLE` entry) of the routing layers the placed
macros obstruct, so two macro obstructions stay DRC-clean. Without such a
spacing it is one manufacturing grid. `X` or `X,Y` microns are honoured exactly,
rounded up only to the LEF `MANUFACTURINGGRID` (else one DBU), never to a site
width or row height; for example `0.001` keeps a 0.001 µm gap. Two macros keep
`max(channel, 2 * halo)` per axis; the halo alone still keeps standard cells
out. Macro legalization, routing-track and tile snapping, memory-proxy packing,
padded occupancy for generated cores (half the channel per side when that
exceeds the halo), cache reuse and the independent legality checks all use the
same clearance, and QoR records the resolved `macro_channel` in microns and
DBU. Snapping only accepts origins that keep the channel. With the default
`halo=auto` the halos already exceed the channel, so placement is unchanged.
With an explicit `halo=0`, macros previously could touch; the channel now keeps
them apart. This is a legality fix, not a default change: touching macros were
never legal. The `tools/physical_repair` JSON config takes `"macro_channel"` as
a number or `[x, y]`, and its `"halo"` also accepts `[x, y]`.

After track alignment, the default macro legalizer checks whether a movable macro's halo leaves less than one usable row (top/bottom) or site (left/right) opposite a placed perimeter signal pin. It tries an inward move that preserves the routing phase, sizing local buffer space from the number of pins facing the macro. It skips blocked moves and keeps fixed macros and the selected power-grid alignment. This screens a common source of post-CTS placement failures; detailed placement and routing still require downstream checks.

#### Native power grid

A full placement with `synth.place.pcrs.clock=true` selects the power-grid strategy
from the input LEFs when a registered power-grid profile matches. With other clock
flows, `--set synth.place.pg=auto` requests the native grid. `synth.place.pg=off`
keeps grid construction external. The native grid includes handoff taps and uses the
profile's supply layers, connections and strap dimensions. Setting
`synth.place.handoff.taps=false` suppresses automatic selection with native CTS.

| Technology | Core strap layers | Default pitch on each layer (µm) |
| --- | --- | --- |
| ASAP7 | M5, M6 | 5.40, 5.40 |
| Nangate45 | metal4, metal7 | 56.00, 30.00 |
| SKY130HD | met4, met5 | 27.14, 27.20 |

Pass an ordinary metadata file as a positional argument to tune the grid. This ASAP7
example changes the M5 pitch to 4.32 µm for one design.

```json
[{
  "class": "livehd.physical.power_grid",
  "schema_version": 1,
  "id": "core-supply",
  "design": "lfsr_prbs_gen",
  "stripes": {"M5": {"pitch": 4.32}}
}]
```

A matching record enables grid generation when the command leaves `synth.place.pg`
unspecified. At most one record may match the selected design. Within `stripes`,
each key names a core strap layer from that technology's strategy. `width`, `spacing`,
`pitch` and `offset` are optional dimensions in micrometres. Each omitted dimension
keeps its technology value. The same strategy sets macro clearance, routing obstacles
and the exported metal. `--emit-dir def:DIR` writes the supply metal in `out.def`
and the effective dimensions, supply pins and wire lengths in `power_grid.json`.

#### Power-grid aware macro spacing (`synth.place.macro.pdn`)

`synth.place.macro.pdn` (default `auto`) raises the macro channel per axis to
what the technology's power-grid strategy needs between two macros, so the PDN
can be built in every channel without repair. The strategy is the one named by
`synth.place.pg`, else the one the LEF matches (`Tech_policy::power_grid`); a
technology without a registered strategy, or one whose layers the LEF does not
describe, is unaffected. The rule (`pass/place/power_grid_rules.hpp`) is
derived only from the description and the LEF. A channel runs perpendicular to
its gap axis; a supply shape crossing it on a layer the macros block (OBS,
conservative obstruction or pin geometry of any hard macro master) is cut into
an isolated segment. Following `connect` upward to the first layer that runs
along the channel gives its feed:

- *feeder*: the chain starts at the rails. Generators insert feeder straps
  (pdngen channel repair, the native rail feeders), so the channel needs, beside
  both halos plus the feed layer's spacing, room for three feed straps and three
  spacings (two feeders next to a possible existing strap).
- *lattice*: any other crossing strap. Its segment connects only where a strap
  of the feed layer, at that layer's fixed pitch/offset, lies wholly inside the
  clear band (the gap less, per side, the largest macro-grid halo and the
  via-stack spacing). The band holds one strap of every net for any position
  when it is at least pitch + width (*any phase*); a band as narrow as one strap
  group works only with edges aligned to the strap lattice (*aligned*).
- *none*: crossing layers pass over the macros.

For NanGate45 + fakeram45 (macros block metal1-4): stacked macros (gap along
y) are lattice-limited: metal4 segments need metal7 (pitch 30, VSS/VDD 15
apart, 1.4 wide), margin 2 + 0.27, aligned 20.94 um, any phase 35.94 um;
side-by-side macros (gap along x) are feeder-limited: 6.79 um. For ASAP7
(macros block M1-M4) M5 passes over the macros (y: none) and rails need M5
feeders (x: about 4.7 um), below the default 2 x 2.7 um halo spacing, so default ASAP7 runs are
unchanged.

Values: `gap`: the channel is at least the any-phase requirement,
buildable wherever the macros land; `auto` (default) starts at `gap` and degrades
(below); `align`: at least the aligned requirement
plus a tenth of the feed strap pitch, and the routing-track alignment also
snaps movable macro edges onto the strap lattice (`Pg_edge_lattice`: the band
below each macro edge, down to the core edge, and above a macro closer than the
channel to the core top, must hold a strap group; two manufacturing grids of
tolerance for downstream re-snapping). When no lattice-aligned frame exists the
alignment keeps the routing tracks alone, so `align` is a best-effort placement
preference, not a guarantee: with 133 um tall NanGate45 memories stacked at the
minimum channel the lattice rarely admits a local move, and stacking pitch
effectively grows to about 29 um, near the any-phase 36 um.
`synth.place.macro.pdn_origin` (`core`, or `X,Y` microns) moves the strap
origin for `align` when the downstream PDN uses another frame (the challenge
evaluator centres the proxy canvas in a larger ORFS core). `off` restores the
metal-spacing channel alone. The explicit `macro.channel` is also raised (the
requirement is a PDN legality bound, not a preference); QoR `macro_pdn` records
the mode, per axis the rule kind, source/feed layers and required/aligned/any
phase microns, and with `align` the lattice origin, the hard macros the lattice
does not admit and the alignment fallbacks. `physical_repair` takes
`"macro_pdn"` and `"macro_pdn_origin": "core" | [x, y]`.

`auto` never turns the PDN channel into a hard failure. Levels, strongest first:
`gap`, `align` (requirement and lattice snap as for an explicit `align`), `off`
(metal-spacing channel), with levels of equal channel merged (ASAP7: `gap`,
`off`). Once the solver core is known, the strongest level whose padded macro
footprints (the native planning pad rounded up to sites/rows, fixed macros
clipped to the core) pass a necessary packing check
(`pg_macro_boxes_fit`: every box fits, total area fits, and per axis the summed
widths are at most `core_w * floor(core_h / min height)` and symmetrically) is
chosen; levels that fail it are skipped without a legalization attempt. When
the native macro legalizer still fails (a heuristic: it can fail on a feasible
instance near capacity), the same GP pose is re-legalized at the next weaker
level until one legalizes. With every hard macro fixed (a staged final pass, a
later timing or repair pass on a planned frame) `auto` takes the strongest
level the frame keeps. Legality checks under `auto` require only the
metal-spacing channel; an explicit `gap`/`align` is a hard rule and a failed
macro legalization stops with a message naming the channel. QoR `macro_pdn`
adds `requested`, `used_level`, `degraded` and `attempts` (`level`, channel,
`result`: `infeasible`, `legalization_failed`, `used`, or `frame`); a staged
flow reports the macro plan's record. Example: NanGate45 ariane136 (136
fakeram45_256x16, 1436 um core) at `gap` needs 64.03 x 166.6 um native
footprints, 22 columns x 8 tiers for 136 macros; fp_plan's grouped pose defeats
the legalizer there and `auto` keeps `align` (6.79 x 23.94 um). Sizing fp_plan
groups by the padded footprint does not help: its 12 group circles then no
longer fit the core.

While macros are still movable, the native solver pads each movable hard macro
by its *planning pad* per side: the halo, or half the channel rounded down to
whole sites/rows when that is larger. Macro GP density and the native macro
legalizer therefore already keep (most of) the channel, and the track alignment
only opens the sub-site remainder; without it a dense frame of abutting
macros cannot be opened locally ("no legal macro routing-track alignment").
A channel below one site/row per side adds no padding, and with the default
`halo=auto` the halo dominates, so default runs are unchanged. Once the macro
frame is fixed, only the halo keeps standard cells out: cells (wires, buffers)
may use the channel in final placement, as OpenROAD's macro channel. The
planning pad also keeps a movable macro that far from the core edge.

Track alignment prefers a full wire width inside a signal pin. When snapping one
macro at a time leaves only a partial-width connection, the legalizer tries moving
neighboring macros together onto their best track phases. Fixed neighbors, hard
placement blockages and required channels still take precedence. At perimeter IOs
the legalizer can also leave rows for downstream buffer placement; those moves
preserve both signal-track phase and standard-cell row or site pitch.
If signal pins on both outward faces of a corner macro lie close to the same
corner, the legalizer also tries to open one face for buffers. It tries the
face with more pins first and moves obstructing movable neighbors only as far
as their clearances require. If neither face can open legally, the original
placement stands. This does not reserve sites for clocks inserted later by CTS.

`synth.place.macro.edge_snap` (default `off`) removes slivers between macros
and the core boundary after native macro legalization (ordinary native macro
flow; H2D is unaffected). Each movable, ungrouped hard macro is assigned to the
half of the core holding its center, per axis. Nearest first, it slides toward
that edge until it meets the core edge (minus its planning pad) or the spacing
of an object in its path; it moves only when that object is the core edge or a
macro already anchored to the edge (so a chain of macros packs against the
boundary, while an interior cluster stays), and only when the clear sliver it
removes is positive and at most the limit. x is processed first, then y.
Movement toward the edge never reduces any pairwise clearance; fixed macros and
fixed non-CORE geometry are obstacles; soft blocks and movable cells are not
(cells are legalized afterwards). The routing-track alignment then runs as
usual. Values: `off`; `auto`, one tenth of the core extent per axis (the notch
threshold of OpenROAD's `rtl_macro_placer`, which also pulls macros to the
boundary with `-boundary_weight`); or `X` / `X,Y` microns. QoR
`macro_edge_snap` records the resolved limits, `moved_macros`, `axis_moves`
and `distance_um` (in the macro stage QoR, `macro/qor.json`, of staged runs).
`physical_repair` takes `"macro_edge_snap": "auto" | "off" | X | [X, Y]`.

#### Macro dead space (`synth.place.macro.dead_space`)

Macro planning can leave free core area that standard cells cannot use well:
empty corners, pockets enclosed by macro arrays, and notches between macros or
along an edge. `synth.place.macro.dead_space` (default `off`: nothing is
computed and placement is unchanged) measures it and, optionally, collapses it.
`physical_repair` takes the same text as `"macro_dead_space"`.

*Metric* (`macro_dead_space` in QoR; the macro-stage QoR of staged runs
measures the planned frame, the final QoR the fixed frame). The core minus the
hard macro bodies grown by their halo is rasterized on square cells of a whole
number of placement rows (the fewest rows that keep at most `grid=512` cells
per axis; NG45 1.4 um rows on a 1436 um core give 4.2 um). A cell is free
when its center is outside every obstacle; soft blocks and movable cells are
not obstacles. For each free cell the horizontal and vertical free run through
it (between obstacles or the core edge) classifies it:

- *channel*: a run no wider than the macro channel plus one pitch. Channels
  are free and never dead, but they do not connect two free regions.
- *open*: both runs at least `narrow` (default one tenth of the core extent per
  axis, the notch threshold of OpenROAD's `rtl_macro_placer`, as `edge_snap=auto`).
- otherwise *narrow*: a notch or sliver wider than a channel and narrower than
  `narrow` (between macros, or a macro and the core edge) — **dead (notch)**.

Open cells form 4-connected regions; every other free cell joins the nearest
region through free cells. A region holding less than `pocket=0.25` of the free
area (the largest region always stays) is a pocket reached only through
notches or channels: its non-channel cells are **dead (pocket)**. A connected
free island (channels included) smaller than `cells=200` average movable
standard cells (one row height squared each when the design has none) is
**dead (island)**. QoR records the thresholds, `free_um2`, `dead_um2`,
`dead_pct` (of the free area), the area per kind, `stranded_um2` (below),
`count`, `largest`, and the
64 largest 4-connected dead components with their bounding box and raster
outline (`rects_um`).

*Collapse* (`auto`, or any `key=value` list without `metric`). In the native
macro flow it runs after native macro legalization and the edge snap and
before routing-track alignment (H2D is unaffected). It minimizes the
*stranded* area (`stranded_um2`): the dead area plus the non-channel area of
any kept region other than the largest, so a move cannot "remove" a pocket by
growing it past the pocket fraction. Each round measures the raster and
evaluates two kinds of moves against the 8 largest dead components:

- *translation*: for every movable hard macro whose face borders such a
  component (within one channel), translate into it (a) the macro, (b) its
  aligned line — the macros of equal extent along the move axis chained across
  it at array spacing (a column for x moves, a row for y moves) — or (c) the
  block slice behind that line, each as far as legality allows or just across
  the dead depth. Members move rigidly (internal spacing kept; macros sharing
  a native group id move together; fixed macros never move).
- *relocation*: a single movable macro on the rim of the main region (one of
  its faces sees main open cells within a channel) jumps into a corner of such
  a component that it fits, packed against the walls there.

Legality is the edge snap's: the planning pad (halo or half the channel) stays
inside the core and macro pairs keep `max(channel, 2 * pad)`; moves are whole
manufacturing grids. The move that removes the most stranded area (at least
four raster cells) is applied — ties prefer a translation toward the group's
nearest core edge, then less wirelength — and the loop repeats until no move
improves or `moves=64` moves. A move must keep the wirelength estimate within
budget: the HPWL of the moved macros' nets at the current global cell
positions may grow by at most `move_hpwl=0.005` per move and `hpwl=0.02`
summed over the pass, both as fractions of the initial HPWL of all nets
touching a movable macro. Track alignment then runs as usual. QoR
`macro_dead_space.collapse_report` records the first and last stage-internal
measurement (dead and stranded area), `moves`, `moved_macros`, `distance_um`,
`hpwl_delta_um` and `candidates`. Dead regions that cannot be closed are only
reported; they are not turned into cell blockages.

Values: `off`; `metric` (report only, placement byte-identical to `off`);
`auto` (metric and collapse with the defaults above); or comma separated
`metric`, `collapse`, `narrow=<um>|auto`, `pocket=<fraction>`, `cells=<N>`,
`hpwl=<fraction>`, `move_hpwl=<fraction>`, `moves=<N>`, `grid=<cells per axis>`.

#### Macro refine rounds (`synth.place.macro.refine_rounds`, `refine_iterations`)

`synth.place.macro.refine_rounds=R` (default `0`: unchanged) alternates, after
the native macro-stage GP and its macro legalization hook (edge snap,
dead-space collapse, routing-track alignment), a short GP burst of
`synth.place.macro.refine_iterations=K` steps (default 30) with the macros
movable again, starting from the legalized/snapped/collapsed pose, followed by
the same macro legalization hook; R times. Cells adapt to the moved macros
during the burst, so the next legalization, snap and collapse land on an
updated arrangement; the last round's macros are legal (the hook ends with the
track alignment's channel/halo/core check). The burst is a warm restart, as
the legroute area resume: the previous solve's density weight seeds the
penalty, smoothing starts at its final value, the step is at most 0.05 bin,
fillers start in the free space and the overflow is held where it is
(`hold_overflow`). It is not a `GpState` continuation: that would resume the
optimizer's pre-legalization pose and discard the macro edits. Each round
re-applies the edge snap and the dead-space collapse (each with its own HPWL
budget). It runs only in the ordinary native macro flow (no live legroute
session, no terminal stage). Per round, stderr and QoR `macro_refine.history`
record GP steps, HPWL, overflow, density weight, the collapse's dead area,
and the macros moved with their summed displacement. `physical_repair` takes
`"macro_refine_rounds"` and `"macro_refine_iterations"` (integers).

#### Optional macro planning refinements

The macro edge snap, dead-space collapse, refinement rounds and moving
anchors remain opt-in. To enable all four explicitly:

```
--set synth.place.macro.edge_snap=auto --set synth.place.macro.dead_space=auto \
--set synth.place.macro.refine_rounds=1 --set synth.place.macro.anchor=true
```

`physical_repair` accepts the equivalent `"macro_edge_snap": "auto"`,
`"macro_dead_space": "auto"`, `"macro_refine_rounds": 1` and
`"macro_anchor": true`. Native `Options` and the CLI default to all four
features off. `macro.channel=auto`, `halo=auto` and `area=fixed` are unchanged.
Anchor sub-options apply only after enabling anchors; their controls are
listed below.

`synth.place.cell_halo=H` or `X,Y` optionally selects a separate clearance for
standard cells around fixed macros during final cell placement and subsequent
placed-design repair. Its default is empty: inherit `synth.place.halo` exactly.
Zero explicitly removes this additional cell clearance; physical macro bodies
remain obstacles. Each axis must be finite, nonnegative and no larger than the
corresponding `synth.place.halo` axis. Macro placement and macro-to-macro spacing
retain the original
`synth.place.halo`, including during final legality checks. This lets a staged
flow reserve, for example, 12 µm for macro placement while using 2 µm around the
fixed macros for cell placement. Both clearances round up to the site/row grid.
With movable macros, full placement performs the macro-to-cell handoff before
applying the override. GP-only placement rejects it. Timing-only imports
continue to preserve their input geometry.

The staged handoff fixes macros and IOs and retains the macro pass's movable-cell
coordinates as the starting seed for final GP. Cells remain movable, including
when macro-stage cell legalization or detail was deferred. The seed also avoids
repeating the automatic-IO prototype with already fixed IOs. Coordinate reuse
is recorded in `restart_seed.json`. A converged macro stage also supplies its
normalized density weight, so final GP need not rebuild that pressure from the
cold initial coefficient. The diagnostic `physical_repair` configuration can
replay this coefficient with `density_weight_seed`.

With this override, QoR `halo` describes the effective clearance, `macro_halo`
records the original macro spacing, and `cell_halo` records the requested
override and whether it is active in that stage. These additional keys are
absent when the option is unset. The diagnostic `physical_repair` configuration
accepts `cell_halo` as a number or a two-number `[x,y]` array; omission inherits
its existing `halo` option.

`synth.place.snapshots=true` saves physical GP coordinates before legalization
for visualization. Ordinary placement writes `gp.json`; H2D writes
`h2d_round<N>_gp.json` after each abstract GP is expanded to physical cells,
before group legalization. Use the selected round in `macro_topology.json`
when comparing H2D against its final placement. These diagnostic frames can
overlap and are not legal placement exports. Snapshots default off and do not
change placement objectives or coordinates.
Snapshots are generated by fresh GP calls; cache hits do not regenerate them.
The stage DEFs `before_legalization.def`, `after_legalization.def` and
`after_detailed.def` retain physical component identities. Legal/detail exports
apply physical CORE row orientations while preserving fixed-cell and macro
orientations; the saved GP frame retains its raw orientations. These observers
do not change placement coordinates, priorities or timing.

`synth.place.cohesion=C` (the pull of the color centroids, see Centroids below) enables soft, area-weighted centroid attraction for
mapped cells and SRAMs sharing a synthesis color in the same instance scope.
It defaults to zero. C is the final coefficient in inverse microns, ramped
linearly from zero during the final GP call. The IO-assignment prototype stays
unchanged. Synthesis provenance is retained through mapping, cached/bypassed
regions, and hierarchy flattening; repeated definitions get distinct physical
groups. Unowned nodes and singleton groups get no attraction. The diagnostic
`cohesion_groups.json` records group membership. No group boxes or new hard
constraints are created, and legalization stays unchanged. This initial host
wiring supports ordinary placement (`synth.place.macro=none`).

### Centroids

A centroid is a point that pulls its members during placement. It is a record of `livehd.placement.hints` (`kind: "centroid"`), scoped by the same templates as the other kinds, so a record in a repeated module becomes one centroid per instance (`<record>:<instance path>:<id>`):

```json
{"id":"flops","kind":"centroid",
 "members":["u/f0","u/f1",{"instance":"u/gate","share":0.5},{"centroid":"inner"}],
 "position":{"mode":"at","x_um":120,"y_um":80},
 "layout":{"kind":"row","gap_x_um":0.5},
 "strength":0.1,
 "clock":{"drive":"BUFx4_ASAP7_75t_R","arrival_ps":140}}
```

- `members` are instances of any kind (flops, cells, memories, macros), named relative to the scope, or other centroids (`{"centroid": id}`), which nest to any depth. A member lives in one centroid; `share` (0, 1] scales the part of its area that counts in the pull.
- `position` is `free` (the default: the point is wherever the members are), `at` (pulled to coordinates) or `relative` (to another top-level centroid, at `dx_um`, `dy_um`). A nested centroid takes its position from its parent's layout.
- `layout` is `floating` (the default: members pulled to the point), `row`, `column` or `array` (`columns`): the members are pulled to slots around the point, in member order, `gap_x_um` and `gap_y_um` apart, before final legalization, which may snap them to rows and sites. A nested centroid is a block in its parent's layout and keeps its own shape.
- `strength` is the pull in inverse microns (the unit of `synth.place.cohesion`); a record without one takes `synth.place.centroid_strength` (default 0.1). Zero turns the centroid off.
- `clock` carries the clock decisions for members that are clock sinks: `arrival_ps` (the wanted arrival of their clock pins, as `synth.place.pcrs.arrival_targets` gives it; it needs `synth.place.pcrs.balance=true`) and `drive_steps` (the absolute drive step of the leaf that holds them, as `leaf_schedule` gives it). `drive` (a leaf driver master) is stored but not read: the planner's step depends on the load. A field set on a centroid that contains others applies to all of them, the nearest setting winning, and an explicit file input wins over a centroid. PCRS reads them both when it groups the flops during placement and when it builds the tree.
- PCRS regenerates leaf groups around the instances owned by authored centroids. A clock gate shared by several leaves contributes its area once across those groups. Use `centroid_edit op=adopt` to save a generated leaf as an authored record. Color cohesion uses a separate set of generated groups.

The final placement compiles the set to the native placer's cohesion groups: a layout is the negated slots as member offsets, a nested centroid adds the parent's group over the same members, a relative position adds one group over both blocks, a position `at` is the group's target, and the hint report (`livehd.placement.hint_results`) lists per centroid the members bound, those that did not resolve, the strength, whether it compiled and why not, and the largest distance of a member from its layout slot. The GUI and the agent socket edit the same records (`centroids`, `centroid_edit`), and dragging a centroid in the page writes its `at` position. Centroids are compiled in plain placement; in the macro stages (`synth.place.macro=h2d`, the floorplan) hint scopes are not bound and centroid records are not read.

`synth.place.macro=h2d` opts into native hierarchical macro placement in full
mode. The default `none` preserves the existing flow. The host reads hierarchy
before flattening, passes physical macros and LEF symmetry to DREAMPlace, then
fixes the selected legal macro frame for standard-cell placement, cache reuse,
ECO and timing repair. The zero-wire post-synthesis timing snapshot skips H2D.
Under `macro=h2d` the controls that belong to the plain flow default off, so the one flag is enough: the movable-macro GP
controls (`macro.pin_adjust`, `macro.align_weight`, `macro.snap`) and the native repulsion experiment controls
(`scale_macro_density`, `exact_macro_density`, `adaptive_density`, `routability`, `macro_overlap`, `macro_jitter`,
`io_iterations`, `schedule_iterations`); setting one of them on explicitly is still an error. H2D runs macros and cells in
one native solver call, so `lhd synth --gui` has no fp/macro/final GP gates for it (the page says so) and shows only
edit mode on the result; the default `macro=none` flow is the one with a macro phase to stop at and move macros in.
Every native parameter is exposed as `synth.place.macro.<name>`; use
`lhd describe synth.place.macro.<name>` for each option. For example:

```sh
lhd synth design.sv --top top --workdir W \
  --set synth.liberty=cells.lib --set synth.lef=tech.lef,cells.lef,ram.lef \
  --set synth.place=true --set synth.place.density=0.5 \
  --set synth.place.macro=h2d --set synth.place.halo=1
```

H2D combines hierarchy/master macro arrays, anchors, relative order, continued
GP, legalization, HPWL compaction and optional lite/SA topology search. Dataflow
pseudo-nets are removed before the final physical cells pass. Optional
`synth.place.macro.routability=true` inflates standard-cell density occupancy,
while retaining physical dimensions for legalization and export. Existing GP
and filler defaults are unchanged outside H2D.

`W/synth/place/macro_topology.json` records stable names, resolved parameters,
selected coordinates/orientations, constraint diagnostics and per-round phase
times/HPWL/overflow. `h2d.png` shows the final macro/cell layout, halos, anchors,
order arrows and IOs. The persistent macro frame lives alongside the placement
cache and follows `lhd.incremental`. It is restored before ordinary block cache
locking; standard-cell edits can reuse it. JSON is cache/output only.
Future HHDS constraint attributes are not implemented yet.

Native `delta`, `min_channel` and `pitch_x/y` use DBU; the existing halo option
uses microns. Coarse pitches must be whole site/row multiples. RUDY track
capacities derive from routing LEF pitches, or require explicit
`route_cap_h/route_cap_v` in tracks per DBU. RUDY and GP convergence are placement
proxies; synthetic legality tests do not establish routed QoR against OpenROAD.

In the ordinary native FP/macro flow, `synth.place.routability=true` also enables
direct macro channel pressure. `synth.place.routability_macro_weight=1` scales
this force relative to the ordinary macro wirelength/density force; zero
disables it. `synth.place.routability_macro_range=2` sets its reach in density
bins (allowed range `(0,16]`). RUDY updates every 20 iterations after density
overflow falls below 0.3. The force uses squared channel utilization, including
utilization below 1, and tapers to zero at the configured reach. Facing macro
edges push apart; fixed macros and die edges can supply the opposing boundary.
Other macros occlude non-neighboring edges. Halos remain routing space and
legalization still enforces them. Once all macros are fixed, this force is off.

`fp_place.json` includes `fp_routing_history`; `gp_history.json` and the live
JSONL samples include `route_peak_utilization`, `macro_routing_force`, and
`macro_routing_channels`. The latter two are maxima since the preceding sample,
so a brief force is visible even if the gap opens before the next sample.
`macro_routing_seconds` records the pressure calculation time. This is a local
channel heuristic, not the derivative of a routed-wire objective; compare
timing and routed congestion before making QoR claims.

The ASAP7 Eyeriss geometry trial with `synth.place.halo=5`, utilization 0.30 and
density 0.5 places all 1040 supplied SRAMs and 303436 standard cells legally.
OpenROAD placement checking and a separate expanded-box audit pass with unchanged
physical cell counts and sizes. Effective halos are 5.022/5.130 microns; padded
occupancy is 57.04% versus 30.00% physical utilization. GP does not converge;
the padded-area lower bound on overflow is already 0.123 at the requested density.
The native macro LP preserves both axes on infeasible heuristic constraints so
the existing full Hannan-grid fallback can run. Intermediate grid attempts can
log failed macro positions before the final accepted legal placement.

Native memories use physical-only square
BLOCK proxies, while their state and interfaces stay native:

```
area = (word_bits * depth) * smallest_flop_area * pow(1.1, read_ports + write_ports)
side = ceil(sqrt(area) * DBU) / DBU
```

The smallest positive-area usable `ff` cell comes from the same `synth.liberty`,
including either clock edge; `dont_use` cells are excluded. Address bits occupy
the left edge, read/write data the adjacent bottom edge, and control pins the
right edge. Proxies are deterministically packed and fixed before standard-cell
placement, preserving the square instead of rounding it to standard-cell rows.
Proxy edge pins use the first routing layer whose preferred direction escapes
that edge: horizontal on the left/right and vertical on the bottom when those
layers exist. After floorplan/track import, pin centers use the actual track
patterns (including interleaved patterns), or the LEF lattice when none are
imported. Their widths and the square body formula above are preserved. An edge
with too few distinct tracks is rejected instead of emitting overlapping pins.
Newly packed proxy origins are aligned before becoming fixed obstacles; explicit
FIXED/LOCKED origins remain immutable and are diagnosed if inaccessible.
For an OpenROAD handoff with platform-specific tracks, pass its literal
`make_tracks.tcl` through `synth.place.tracks`. Preserve those imported DEF tracks
in the handoff rather than adding the same patterns a second time.

The generated `memory_proxies.lef` accompanies `out.def`, including `def:` export.
QoR reports `estimated_macros`; OpenTimer reports
`memory_timing_model=clock_boundary_zero_internal_delay`. Read data launches at
zero and data/address captures at the clock boundary, with external wire RC and
no internal memory delay. This does not provide characterized memory timing.

Full placement uses slack-weighted placement by default
(`synth.place.timing_driven=true`). Set it to `false` for geometric/debug
experiments, including runs with `synth.timing=false`. Synthesis-only and
GP-only commands do not enter this refinement loop by default. Full timing-driven
placement requires OpenTimer and constrained data nets (normally from
`synth.sdc`). OpenTimer
first ranks the mapped netlist with zero wire RC to seed the initial net weights.
The seed prepares native memories and tie cells without running placement or
publishing a placement cache or DEF. Every timed data net gets a rank-based weight (worst 2, median 1, best 0.5)
with 0.5 smoothing; clock and reset networks are excluded.

`synth.place.timing_rounds` allows 1–8 refinement attempts (default 3). They are
skipped when setup is already met; electrical repair remains a separate phase. Each attempt uses
the current coordinates and normalized density weight, fixed IOs, the configured
final wirelength smoothing scale, and at most 100 GP iterations. Physical repair,
resizing, region refinement and final legalization retain the full-GP density
coefficient, so a later warm refinement does not restart density from the default. A candidate
must stay within 3% of the initial HPWL and be density-feasible before detailed
timing is evaluated; routability flows also require solver convergence. The
resulting area must stay within 3%. Accepted candidates must improve timing
without degrading the guarded electrical metrics. A candidate rejected by the
geometric checks ends refinement and hands the accepted placement to repair. Reports are in
`synth/place/timing_driven.json` and placement QoR. Detailed feedback passes from
the completed placement stage into refinement, and from final legalization into
IO reporting, when it describes exactly the same state. Ranking-only feedback
cannot replace these detailed checks.

The production GP callbacks update weights without inserting, removing or sizing
cells; electrical repair (buffering and sizing) runs after legal GP. The
GP-time buffer repair and its `synth.place.resize.gp*` controls were removed. Macro and speculative A/B callbacks also leave topology
unchanged. `synth.place.final.timing_start=200` delays final-cell ranking samples
until warm-up completes. The interval samples density, with ranking updates at
overflow 0.2 and 0.1; thresholds crossed together produce one update. Short runs
still obtain the selected-frame timing required for legalization and resynthesis.
Synthesis timing supplies the initial weights. The `physical_repair` diagnostic
accepts the same warm-up as `final_timing_start`.

The `physical_repair` diagnostic refuses its former `gp_resize`,
`gp_buffer_margin`, `gp_buffer_trials` and `virtual_timing` keys.

`physical_repair` accepts opt-in `gp_ab=true` for final fixed-macro cell
placement, with timing feedback, RSMT routability, virtual fillers, and
`local_density_jitter=0`. It is incompatible with moving boundary terminals,
cell-jump propagation and macro-placement forces. Its optimizer snapshots assume
fixed topology, which GP keeps (post-placement resizing remains available with
`resize=true`). Existing defaults are unchanged.

After iteration `gp_ab_start` (220), every `gp_ab_interval` (40) iterations,
the native solver branches from one full GP checkpoint. `gp_ab_rounds=0`
(the default) keeps adapting until the final settling window; a positive value
optionally caps the number of decisions. Each branch executes `gp_ab_steps` (3, range 1–3) steps. Candidates
are unchanged/lower/higher, alternating effective filler charge and local
repulsion. Changes are `gp_ab_filler_step` (0.2) and `gp_ab_local_step` (0.1),
clamped to [0,1]; duplicate boundary candidates are omitted. Filler particle
count/size stays constant. All branches share cells, filler coordinates, routing
inflation, density normalization and timing weights; momentum restarts equally
for all candidates at the objective boundary. Only the winner's full state is
retained, including its tuning values. Smoothing follows the whole-run schedule.
The final 20 iterations are reserved for settling, and normal legalization runs
once at the end. With vanished memories, search starts only after full opacity.

Native trials run concurrently within the configured thread budget. Timing
scoring runs serially through the host timer, without updating shared net weights
or committing losing-trial placements. Normal timing feedback resumes on the
selected trajectory. Selection filters near-best candidates in this order:
RSMT-RUDY overflow (1% relative tie band), WNS (1%), unweighted active-net HPWL
(0.5%), TNS (1%). Exact ties retain the unchanged candidate. Physical density is
reported separately. Clocks/resets use the same preCTS exclusions as ordinary GP.

`initial/gp_ab.jsonl` records every parent and candidate score, parameter values,
selected candidate, comparison tolerances and decisive metric. It is a preCTS
proxy experiment, not a routing-success claim. Candidate timing is in ns and
HPWL in µm. Normal GP history and snapshots contain only the chosen trajectory.

Trial scores also report `route_unroutable_nets`, `route_blocked_pins`,
`route_measured_nets`, `route_congestion_overflow`, `route_peak_utilization`,
and `route_detour_wirelength_um`. The congestion-only term subtracts the
existing (unroutable nets + blocked pins)/routing-bin-count penalty from
combined RUDY overflow. These counts describe the estimator's opaque-macro
free-space model, not detailed-route failures; pre-legalization overlap can
cause them. They are diagnostic fields, not an additional ranking term.
Trial scoring does not overwrite the main trajectory's routing-history counters.

For a separate macro-frame experiment, `physical_repair` supports
`macro_only=true` together with `floorplan_place=true` and `final_cells=false`.
Use `fp_place=true` to run centroid FP before macro planning, or `fp_place=false`
to run macro planning alone. This exports legal macros with provisional cell
positions; it does not certify final cell legality.
Final-cell controls (cell halo, cell jumps, memory transparency, GP A/B search)
are rejected in this mode. Rebuild row cuts and physical tapcells around the new
macro positions before preparing a separate fixed-frame final-cell solve.

### Moving macro anchors during macro GP (`synth.place.macro.anchor`, opt-in)

`synth.place.macro.anchor=true` (default `false`; byte-identical when off) turns
the end-of-plan discrete macro decisions into moving soft anchors inside the
macro-stage GP, so cells settle around the macros' final positions and the
final hard pass becomes a correction. It reuses existing machinery:

- **Target generator.** Each refresh copies the current GP snapshot and runs
  the stage's own end-of-plan macro legalization on the copy: the native
  macro-only legalizer on the halo/planning-pad padded design, with the macro
  snap (`macro.snap`) constraints when enabled, then the host
  `after_macro_legalize` chain (`macro.edge_snap`, routing-track snap, and any
  later pass added to that chain). The chain receives a `late` flag: only
  refreshes inside the last `dead_space_refreshes` refresh windows before the
  GP iteration cap are late, and only late target runs execute expensive
  passes such as the `macro.dead_space` collapse (slow on IBM-size designs);
  earlier targets skip them (dead-space QoR counters, like the edge-snap
  counters, describe only the final pass). `macro.refine_rounds` bursts run
  after the anchored GP without pulls. The legal macro
  centers are the targets; nothing is applied to GP and the final hard pass is
  the same code. Edge-snap counters in QoR describe only the final pass. A
  failed target run (e.g. no legal track alignment) keeps the previous pulls.
- **Anchor force.** Targets are the native virtual macro pulls
  (`macro_pull_update`: optimizer-only absolute centers,
  preconditioner-compensated, relative to the mean ordinary macro force;
  momentum restarts at every refresh). Each movable hard macro's weight is
  `weight / (1 + gate * max(0, g.u) / median|g|)`, where `g` is its HPWL
  subgradient over its nets (each net weighted `1/(degree-1)`; a macro that is
  not the strict extreme of a net on an axis feels no pull from it) and `u`
  points to its target: a macro whose move toward the target lengthens its nets
  is held more weakly, a macro inside its net boxes is free to follow.
  `macro.anchor` and `macro.magnet` are exclusive.
- **Schedule.** The first refresh happens at GP overflow `start_overflow`
  (native progress samples, every 20 iterations), then every
  `refresh_interval` iterations and when overflow crosses each `milestones`
  value, until fewer than `stable_fraction` of the macros move their target
  by more than `stable_distance` of their smaller side (and no swap/lock is
  pending), or `max_refreshes`. The weight ramps to `weight` over `ramp`
  refreshes. With `final_weight > weight`, refreshes continue after the
  targets stabilize and the weight ramps to `final_weight` over `ramp` more
  refreshes, so macros converge onto their targets. GP keeps at least
  `settle` iterations after each refresh, and no refresh starts within
  `settle` iterations of the cap.
- **Swaps** (`swaps=true`). For each macro, the `swap_neighbors` nearest
  macros of the same width and height (within `swap_tolerance`) whose HPWL
  descent directions point at each other are candidates; the exact HPWL change
  over only the nets incident to the two macros, with both at the exchanged
  targets and everything else at its GP position, decides. Accepted pairs
  (best gain first, disjoint) exchange their *targets*; GP carries macros and
  cells. A swapped pair holds its exchanged targets for `swap_lock` refreshes,
  and a pair is never exchanged twice. With `swap_jump=true` (default) the
  exchange also moves the two macros to each other's GP position at that
  refresh (DREAMPlace `MacroPull::jump`); with `false` only the targets swap
  and GP must carry the macros across each other, which in practice rarely
  happens: the next refresh re-derives the old targets and the pair churns
  (hundreds of swaps per run, worse proxy on ibm03/ibm12).
  Orientation flips are not attempted: the challenge's evaluator placement
  carries no orientation, so they cannot change its score.
- **Inflation** (`inflate=f`). Until the first refresh, each movable macro's
  density footprint is its padded size (planning pad = half the channel) grown
  by `f` per side, with the margin ramped linearly with GP overflow from the
  first sample down to exactly zero at `start_overflow` (the first refresh).
  It uses the DREAMPlace `macro_density_margin` hook (density only; geometry,
  overlap and legalization unchanged).

| `synth.place.macro.anchor.*` | Default | `physical_repair` key |
|---|---:|---|
| (enable) `macro.anchor` | false | `macro_anchor` |
| `weight` (0..8, x mean macro force; 0 = measure only) | 0.5 | `macro_anchor_weight` |
| `final_weight` (0..8; <= weight: off) | 2 | `macro_anchor_final_weight` |
| `gate` | 1 | `macro_anchor_gate` |
| `start_overflow` | 0.3 | `macro_anchor_start_overflow` |
| `milestones` | 0.15,0.1 | `macro_anchor_milestones` (array) |
| `refresh_interval` | 30 | `macro_anchor_refresh_interval` |
| `ramp` | 3 | `macro_anchor_ramp` |
| `max_refreshes` | 40 | `macro_anchor_max_refreshes` |
| `settle` | 20 | `macro_anchor_settle` |
| (internal) `stable_fraction`, `stable_distance` | 0.05, 0.05 | `macro_anchor_stable_fraction`, `macro_anchor_stable_distance` |
| `swaps`, `swap_tolerance`, `swap_lock` | false, 0.05, 2 | `macro_anchor_swaps`, `macro_anchor_swap_tolerance`, `macro_anchor_swap_lock` |
| `swap_jump` | true | `macro_anchor_swap_jump` |
| (internal) `swap_neighbors` | 8 | `macro_anchor_swap_neighbors` |
| `inflate` (0..0.25) | 0 | `macro_anchor_inflate` |
| `dead_space_refreshes` (-1 = all) | 2 | `macro_anchor_dead_space_refreshes` |
| `select` (`edge+random` \| `edge+stable` \| `all`) | edge+random | `macro_anchor_select` |
| `edge_weight`, `random_weight` (0..64) | 4, 1 | `macro_anchor_edge_weight`, `macro_anchor_random_weight` |
| `random_fraction` (edge+random only) | 0.25 | `macro_anchor_random_fraction` |
| `edge_chain`, `edge_gate` | true, false | `macro_anchor_edge_chain`, `macro_anchor_edge_gate` |
| `spring_length` (0..4; 0 = native 1) | 0.1 | `macro_anchor_spring_length` |
| `target_hysteresis` (0..1) | 0.5 | `macro_anchor_target_hysteresis` |
| `final_targets` | true | `macro_anchor_final_targets` |

**Anchor strength.** The native pull's preconditioned step is
`weight * reference * min(1, distance / length)`: `reference` is the mean
preconditioned ordinary (wirelength + density) force of the pulled macros and
`length` the mean macro `max(width, height)`. The DREAMPlace preconditioner is
`pins + lambda * area`, and on the challenge designs `lambda * area` stays
below 0.1 x pins through the anchor phase, so it is effectively the pin count;
the pull multiplies by the preconditioner and is size-independent. A macro's
own preconditioned density force grows with its area (and with lambda) and
dominates its wirelength force 2-15x late in GP. With `select=all`
(`weight` 0.5..2, `length` = one macro), macros settle where the softened pull
balances that force, typically a third to a whole macro length (30-60 um) from
their targets, which the final hard pass then has to cover. The round-6
controls fix it:

- `spring_length=f`: the pull saturates at `f` x the mean macro side
  (e.g. 0.1) instead of a full macro length, so it stays at its cap until the
  macro is close.
- `select=edge+random`: each refresh classifies the targets. *Edge* macros
  (target box within one channel + halo + one site/row of a core side, or, with
  `edge_chain`, facing an edge macro of that side across at most the macro
  spacing, in that half of the core, as `macro.edge_snap` chains them) get
  `edge_weight` (ungated unless `edge_gate`); a fresh deterministic
  `random_fraction` sample of the other macros gets `random_weight` (gated);
  the rest are free for that window. Weights ramp over `ramp` refreshes;
  `weight`/`final_weight` are not used, and refreshes continue every
  `refresh_interval` (fresh sample). `select=all` keeps the round-4 behavior
  (every macro, `weight`/`final_weight`). With legalized, edge-snapped targets most
  macros sit in edge-chained arrays (all on ariane, 90-100% on IBM), so this
  mode is mostly "strong anchors on every arrayed macro".
- Size-proportional pulls (weight x macro area / median area, full or sqrt,
  on all or only the random anchors) and a per-macro far cap at the macro's own
  ordinary force were tried in round 6 and removed: area-scaled strong anchors
  made big IBM macros yank and their targets stop converging (IBM macro-flow
  final proxy 1.59 vs 1.39 unscaled), the own-force cap was no better (1.49).
- `target_hysteresis=h` legalizes targets from GP centers moved `h` of the way
  toward the previous targets, so a macro that has reached its target keeps
  it (targets stop flipping between edge positions).
- `final_targets=true`: the final hard macro pass starts from the last legal
  target set (the generator's output at the last successful refresh, before
  swaps/locks) instead of the raw GP pose, and after the native macro
  legalization keeps those targets instead of rerunning the host chain (edge
  snap, dead space, track snap) on them, since they are already its output.
  With strong anchors the GP pose sits within a few µm of the targets, but that
  residual overlap between abutting macros made the hard pass fail (routing-track
  alignment, macro LP) on 1-2 of 17 IBM designs, and rerunning the chain on
  its own output is not idempotent (edge snap re-slides macros the track snap
  shifted). `final_snap` still measures GP pose vs legal result.
  GP keeps running after the last refresh (up to `settle` iterations after it,
  or for the rest of the budget once `max_refreshes` is reached: ~300 iterations
  at the defaults), so the last legal set can be stale. Before the hard pass
  the generator legalizes the final GP pose once more (same chain, the same
  hysteresis toward the last targets) and the pass keeps whichever of the two
  legal sets has the lower net-weighted HPWL against the final cell pose
  (`final_choice` = `last` | `fresh`, with both costs, in `macro_anchor.json`).
- `select=edge+stable`: as `edge+random`, but the weak (`random_weight`)
  anchors go to the non-edge macros whose legal target held (moved at most
  `stable_distance` x its smaller side) since the previous refresh, instead of
  a fresh random sample; a macro whose target still jumps stays free until its
  GP position settles. Deterministic, no `random_fraction`.
- Per-macro ramp: in both selective modes a macro's pull reaches its full
  weight after `ramp` consecutive refreshes with an unchanged target; a target
  jump restarts that macro's ramp (instead of one global ramp by refresh count).
- Start gate: the first refresh waits for `start_overflow`. When the GP
  overflow never rose above 2 x `start_overflow` (sparse macro stages such as
  HighTide cnn, eyeriss, liteeth, litepci and NyuziProcessor start below it),
  overflow says nothing about convergence; the first refresh then also waits
  until the RMS macro motion per refresh window (center displacement over the
  window / the macro's max side) is within `spring_length` (0.1 when 0), at
  most until half the iteration budget. Targets legalized from a pose that
  still moves more than the spring length per window are stale before the
  next refresh (NyuziProcessor: first refresh at iteration 21-61 without the
  gate). `motion_trace` (iteration, overflow, RMS motion, median motion in
  mean macro sides) and each refresh's `motion` are reported.

`macro_anchor.json` then also records per refresh the edge/random anchor
counts and, per anchor kind at the last refresh, the mean final-snap
displacement (`final_snap_by_kind`: free, edge, random, all).

Weights above one lift the native pull cap (`Options::macro_pull_max_weight`).
`<stage output>/macro_anchor.json` (macro stage: `macro/macro_anchor.json` in
staged runs) records every refresh (iteration, overflow, reason, changed
targets, swaps, locked macros, base/mean weight, mean/max macro-to-target
distance, seconds), the accepted swaps, and the final hard-pass displacement:
macro centers entering the final legalization versus the legal result
(`final_snap` mean/max/total µm) and the final distance to the last targets.
`weight=0` runs the targets and this report without changing GP (a baseline
for the final-snap displacement). The final-cell stage never carries anchors.

### Soft blocks (LEF `CLASS BLOCK SOFT`)

A LEF 5.8 master with `CLASS BLOCK SOFT` is a soft block: the region of a
standard-cell cluster represented as one movable rectangle, as RePlAce and the
Circuit-Training/ICCAD04 macro-placement evaluators model soft macros. The LEF
reader keeps the class text (`ph::soft_block`); `CLASS BLOCK` and `CLASS BLOCK
BLACKBOX` stay hard macros (`ph::hard_macro`). Designs without soft masters are
unchanged.

- Global placement (FP and macro planning) moves a soft block with its full
  LEF area in density and its pins at their LEF offsets in wirelength; a
  soft-master PIN defined without PORT geometry connects at the block center. It is a native non-macro node, so it takes
  no macro legalization, routing-track or tile snapping, rigid/alignment or
  magnet terms, halo, macro channel or routability inflation, and it does not
  set the automatic channel.
- In centroid FP it is an ordinary movable logic node of that area: it
  contributes its area to centroid groups and placement colors like a cell.
- It is not row-legalized. The pose written to the graph/DEF is its GP
  position rounded to DBU and clamped inside the placement core, `PLACED` in
  its supplied orientation. Soft blocks may overlap each other, hard macros
  and cells; legality checks require only that a soft block is placed inside
  the core, and hard-macro halo/channel/overlap checks ignore it. FIXED soft
  blocks count in density but are not placement exclusions or obstacles.
- Final placement (the staged flow with `final_cells=true`): the macro stage
  automatically defers its cell legalization/detail (`macro_legalize_cells` and
  `macro_detailed_place` become false when the design instantiates a soft
  master; `flow.json` records it). The fixed-macro handoff fixes only hard
  macros; soft blocks stay movable and are seeded at their macro-stage poses.
  Final GP moves them like movable cells of their full area (density and pin
  wirelength). Terminal legalization ignores them (`Frame_role::ignored`: not
  packed into rows and not obstacles), so standard cells legalize as usual and
  may overlap a soft block; the soft block keeps its final GP pose clamped in
  the core. Native legality (`qor.legal`) is checked without the soft blocks.
- Any other call that would legalize cells with soft blocks — the macro stage
  with cell legalization, a single-pass run with movable macros, or legroute —
  still rejects them (`std::invalid_argument`). Macro-only runs use
  `physical_repair` `macro_only=true`, `final_cells=false`
  (`completed_stage=macro`, `macro_legalize_cells=false`). Timing seeds,
  retained placements and GP-only runs accept them.

## Placement RC pessimism

`synth.place.rc_pessimism` (diagnostic JSON: `rc_pessimism`) defaults to `1` and
must be finite and at least 1. Wire resistance and capacitance each scale by the
square root of this value, so `2` doubles the wire RC product. Liberty pin loads
and cell timing tables remain unchanged. The scaled wire model is shared by
placement STA, timing weights, buffer planning, sizing and IO timing.
`wire_rc.json` records the multiplier and effective coefficients. Compare
variants with a common nominal RC model or extracted post-route timing; their
pessimistic optimization slacks are not directly comparable.


### Imported placement masks and SRAM-channel repair

Fixed-frame replay distinguishes reserved pinless BLOCK instances by name:
`__lhd_frame_mask_soft_*` are initial-placement-only soft blockages;
other `__lhd_frame_mask_*` instances are hard missing-row/site exclusions.
The adapter must split overlapping soft regions and row cuts into disjoint
rectangles after subtracting real fixed footprints. Historical adapters that
merged both into hard masks must be regenerated; the original semantics cannot
be recovered from those rectangles alone.

Both mask types contribute GP density, including the vanished-memory schedule,
but neither blocks RSMT/RUDY routing or macro routing-channel pressure.
Soft masks release their sites for native detail, ECO buffer repair, SAT and
terminal legalization. Hard masks and physical SRAMs remain obstacles.
Masks receive no additional macro halo. Reserved mask names require fixed,
pinless, non-CORE geometry in N orientation, and masks never enter the physical
netlist handed back to OpenROAD. The native API carries an explicit
`node_placement_mask` type through topology replacements and rigid remapping.

GP buffer-repair traces include `rejection_reasons` so timing rejection can be
distinguished from absence of legal placement sites. Counts of inserted buffers
alone do not show whether repeaters can occupy SRAM channels.

A repair trial is kept when it does not lose setup WNS or TNS, hold WNS, delay or electrical margin, and gains something.
Placement-time hold is an estimate before the clock tree, and the violations already there are repaired later, so one
exception applies: when hold was already violating and setup is failing, a trial may lose up to 50 ps of hold slack if its
setup WNS gain is at least four times the loss and total negative slack does not worsen. A hold that was clean and turns
violating is still refused. Each trial records the hold WNS it measured (`hold_wns`), so a `hold_wns` refusal shows what it
cost. Before this rule, a trial that took litepci from -7.3 ns to -0.2 ns of setup was refused for a hold loss in the noise,
and whether that happened depended on the placement to the picosecond.

### Partial-size memory starts

The diagnostic replay accepts `vanished_memory_initial_scale` (default 0.6),
`vanished_memory_growth_iterations` (default 25), and `vanished_memory_start`
(default 70) with `vanished_memory=true`. The initial scale is a fraction of
both width and height, centered on the fixed memory; 0.6 therefore reserves
36% of its full area. Pins and physical dimensions remain fixed. Hold this
footprint through the configured start, then grow linearly to full size over
the configured duration. The default reaches full size at iteration 95.
Allow at least 20 fully opaque settling iterations afterward. Local-density
decay begins on the same step as memory growth. Routing obstacles, density
charge and filler accounting follow the same footprint. Vanished memory
remains disabled unless requested.

Diagnostic `physical_repair` replays accept `gp_trace_interval` (default 0,
disabled) and `gp_stop_iteration` (default 0, no early stop). The trace uses a
separate timing session and records WNS/TNS, HPWL and fresh RSMT-RUDY every
interval in `gp_observations.jsonl`, without updating placement weights or
triggering repair. Routing is measured with both the current memory footprint
and full opaque physical macros. Clock/reset exclusions match normal timing.
A positive stop must be a multiple of the trace interval within `iterations`;
it exits at that GP step, before legalization or post-GP repair, writing
`gp_stop.json` and an explicitly unlegalized `gp_stop.def`. Keeping `iterations`
unchanged preserves the original late timing schedule during short traces.
The diagnostic timing cost is recorded separately in `observation_profile.jsonl`.

`vanished_memory_start` controls the held initial-memory phase (default 70).
Growth and the existing local-density decay both begin on the following GP
iteration; the footprint reaches full size at start + growth iterations.
The setting is diagnostic and requires `vanished_memory` when nondefault.
It does not move the independent final-placement timing warm-up.

### Route model in reports and the routed-truth scorer

The resolved estimator of every stage is written to `route_options.json`. The
model the final stage ended on, its switch iteration and its probe result are in
`gp_history.json`; QoR records the model. Final legalization preserves the optimizer's routing
model and incremental reuse accounting in QoR; its geometry and RC metrics
describe the final legalized placement. The prior report is also retained as
`before_final_legalization`.

To compare an estimator with what a detailed router did on the same layout, use
the [routed-truth scorer](../../tools/routetruth/README.md): it extracts
utilization, DRC and detour labels from a routed DEF and scores RUDY, RSMT or
any other map against them. The learned third estimator was removed;
[2r-route](../../todo/livehd/2r-route.html) records why.

### Legroute plan (`synth.place.legroute`, on by default)

With placement the legroute plan runs by default: after global placement converges, plan rounds route the
placement on a tile grid, widen the cells of congested nets and resume GP; the best measured round goes to the
regular terminal legalizer, and leftover overflow is reported (`legroute_plan_selection.json`, `qor.json`), never
fatal on a fixed die/core. The grid takes its routing layers from the technology profile, else from the LEF stack
above the second routing layer; a two-layer stack routes on both layers (one horizontal, one vertical, pins landing
on them). Fewer than two routing layers, or no horizontal/vertical pair, is an error. On a generated core the
plan's leftover overflow may grow the core at most `synth.place.legroute.area_restarts` times, counted across the
driver's restarts. Under an interactive GUI session the main GP stays in the native GP owner (snapshot, fork and
optimizer continuation), the plan rounds start their retained optimizer from its converged positions, and
edit-mode moves are replayed on the final placement. `synth.place.legroute=false` turns the plan off.

Each plan round's global route seeds nets with FastRoute-style L/Z patterns on the remaining edge capacity
(`synth.place.legroute.global_pattern`, on by default) and falls back to the negotiated maze only where a pattern
does not fit. On HighTide asap7 this routes the long multi-pin nets the bounded maze gave up on (litepci 85 and
coralnpu 160 unresolved nets -> 0), measures less overflow on the same placement (sha3 1374 -> 330 tracks, minimax
369 -> 0) and halves the probe time. A round whose residual has no cell to widen (only unresolved nets, or overflow
on timing-protected cells only) keeps the measured placement instead of spending a GP slice without a routing
objective.

## Timing-closure observation controller

`//pass/place:timeclosure` is the independent observation/controller foundation
for 2t-timeclosure. It has no timer,
placement, thread-pool or graph dependency. Existing placement defaults and
callbacks are unchanged. Production `synth.place.timeclosure` integration is
pending the native proxy; the current interface is an offline replay:

```
bazel run -c opt //tools:timeclosure_replay -- settings.json samples.jsonl decisions.jsonl
```

Settings are the typed fields in `timeclosure::Settings`; an empty JSON object
selects the new controller's experimental values (800 iterations, cadence 10,
five-sample windows, evaluations every three samples, horizon 50, range fraction
0.03, noise multiplier 2). Unknown settings fail. Optional resynthesis is not
admitted in this first controller. `endpoint_overlap` records the intended
resynthesis threshold; the no-resynthesis gates do not use it.

Each input line has an increasing `iteration`, on the configured sample cadence,
and optional `period_ns`, `window_tns_ns`, `hpwl_um`, `opaque_route_overflow`,
`density_overflow`, `endpoints` (transition-qualified identities), unsigned
`topology`/`geometry`/`timing_model` generation IDs and `virtual_timing`.
`period_ns` must include launch/capture, setup and IO requirements; do not convert
WNS to a period until single-period timing coverage is established. Fixed-SDC
`wns_ns`/`tns_us` from older diagnostics are not aliases for these metrics.
Missing metrics or scheduled samples break stability evidence. Missing endpoint
sets differ from empty sets. Raw and virtual-buffer histories reset on changes.

Replay emits metric trends and gate events with reason, remaining budget,
dwell deadline, generations and density overflow. Memory/RUDY gates latch
independently. Deadlines cannot skip memory settle or timing response, and a
budget-limited finish is explicitly incomplete. Sampling continues through
dwells, with histories restarted afterward. Corrections use density overflow,
not routing overflow, and have a maximum age. These are decisions for a future
GP integration, not evidence that calibration or repair actually ran. A short
input trace remains unfinished; replay never invents later observations.

The initial acceptance gates below are frozen before candidate comparisons.
Use the same OpenROAD preCTS signal netlist, fixed macro/IO frame, Liberty/LEF,
SDC and RC for CNN and litepci, with synthesis/resynthesis disabled:

| Check | Initial acceptance gate |
| --- | --- |
| Reference serialization | Same instance/master, signal connectivity, macro/IO geometry; OpenSTA WNS/TNS deltas at most 0.001 ps after DEF reload |
| Analytic proxy cases | Correct rise/fall, unateness, clock/setup/IO semantics and missing-coverage diagnostics; analytic values within max(1e-9 ps, 1e-9 relative) |
| Full versus incremental proxy | Common transition results within max(1e-8 ps, 1e-8 relative), including topology edits and rollback |
| Matched-RC independent timing | Common endpoint arrivals within max(5 ps, 5%); slew within 10%; true/predicted near-critical-set Jaccard and Kendall tau each at least 0.9, reported separately by path style |
| Held-out routed prediction | Near-critical-set Jaccard and Kendall tau each at least 0.9 per style; style bias relative to local nets within 10%; no unreported electrical/coverage failures |
| PreCTS QoR | Median achievable period no more than 0.1% above the matched control; no additional electrical violations; independent legality/connectivity pass |
| Routed QoR | Same corner/constraints, period no more than 1% above control; no new DRC/unrouted nets or increased hold residuals; report per-clock coverage before any Fmax claim |
| CPU/runtime | Three matched single-CPU trials; median native placement time at most 2x control, peak RSS at most 1.5x; report timing/FLUTE/DP/GP costs separately |

Reference preparation must preserve the saved OpenDB signal/clock routing-layer
bounds as well as LEF geometry and wire RC. DEF does not serialize those bounds.
The evaluated OpenROAD estimator uses them to average pin access cut resistance;
fresh LEF/DEF timing can therefore differ with identical cell and pin positions.
Restore the original bounds before estimating either representation's parasitics.
OpenDB `LOCKED` macro placement serializes through DEF `FIXED` and reloads as
`FIRM`; compare fixed placement semantics in addition to exact geometry.

Window TNS, fixed-SDC TNS, area/leakage, buffer churn, gap demand, HPWL and
congestion remain diagnostics. Incomplete, unsupported, failed and timed-out
runs stay in the matrix. Proxy-only observation or a phase-7 ablation cannot
satisfy the finished timing-closure delivery. Optional synthesis/resynthesis and
post-route multi-corner hold ECO remain separate later milestones.

### Native late timing observations

`//pass/place:late_timing` binds a mapped, flat LGraph to immutable native
Liberty tables (`//pass/liberty:timing_model`). It records the actual HHDS pin
handles and declaration-based physical identities; optional pin-name attributes
are not required. The HHDS ordered traversal supplies the bindings, followed by
explicit clock/state cuts and a validated pin DAG. A combinational cycle is an
error even if HHDS returns a cycle tail. Topology edits require a new binding;
each result carries topology/model generations.

The binding table uses interned pin identities rather than copied hierarchical
strings. RC evaluations index nets and tree nodes by the already-bound numeric
pin IDs, and check sink membership through the driver-ID array. They do not
rebuild string-keyed RC-node maps. Star-model feedback uses the submitted net
and sink order to carry those IDs through evaluation too.

The timer carries rise/fall arrival, slew, required time, load and worst
predecessor separately. It supports positive/negative/non-unate arcs, ideal
positive-edge clocks, clock-to-Q, setup and explicit IO boundaries. Named clocks
retain separate launch domains, positive edge offsets and asynchronous capture
cuts. Related clocks use the minimum positive edge separation on their supported
time grid. Forwarded same-waveform aliases require a traced positive buffer path.
Per-clock summaries retain absent objectives for clocks without timed endpoints;
a scalar achievable period requires one active, independently variable capture
clock. True per-domain required times remain available alongside the aggregate
pin observation. Exact Boolean
truth tables (up to six scalar cell inputs) propagate constants, evaluate `when`
conditions, disable functionally inactive arcs and refine timing sense after
case analysis. A true conditional arc overrides its matching unconditional
default. Waveform calculation remains separate from feasible setup arrivals,
as in the offline reference. Characterized output-to-output propagation groups
in the shipped ASAP7 Liberty are retained with recorded warnings, matching
OpenSTA. A related output with no external net is bound through its real instance
and IO declaration, without creating a pin or changing LGraph connectivity.

An instance without a Liberty timing model (a native memory's proxy macro,
`uncharacterized cell`) stays unbound and is listed as unsupported, which leaves
the evaluation uncertified. A physical RC net it drives or loads is skipped or
keeps that pin as wire capacitance only, rather than refusing the whole run.

Zero-wire evaluation still includes actual pin/output loads. Optional in-memory
`physical::Parasitics` supply connected resistor trees with wire-only capacitance;
pin loads are added once per transition. `rc_tree.{hpp,cpp}` computes per-sink
Elmore time constants and a pi model matching three input-admittance moments.
Explicit node-capacitance inputs include wire and actual pin loads once.

`gate_waveform.{hpp,cpp}` fits a native driver ramp and effective capacitance
using the NLDM equivalent-C crossing constraints and charge matching against the
pi network over a bounded horizon. The fitted pi response supplies output slew;
convolution with each sink's Elmore pole supplies its wire delay and slew.
Repeated-pole, zero-near-capacitance, flat-table and unit-scaling limits have
analytic regressions. Numerical failures retain an explicit total-load
observation and block certification. The background reference is
[Dartu, Menezes and Pileggi (1996)](https://users.ece.cmu.edu/~pileggi/performance-computation-for-pre-characterized-cmos-gates-with-rc-loads/).
This implementation calls no external timing engine.

Flat delay tables and electrically unresolved distributed models retain their
characterized lumped boundary. Driver-fit, distributed, lumped and numerical-
failure counts expose these choices. `delay_only_wire_drivers` separately counts
load-independent models. An experiment using a lumped boundary for nonmonotone
extrapolated curves increased CNN's numerical failures and was removed; its
frozen source and results remain in the matrix. The declared model approximations
still require the independent fidelity gates.

Prepared NLDM load slices preserve the two-dimensional table's interpolation and
boundary extrapolation while fixing the input slew. A safeguarded charge solve
uses analytic derivatives, with bracketing retained. Arrival and required-time
propagation reuse the delay computed during waveform calculation. All graph
walks remain full walks. An optional calibrated continuous-star model is described
below. Incremental timing invalidation, virtual buffers and GP feedback remain pending.

`Result::unsupported` prevents certification; absent timing remains absent.
Constant pins carry an explicit logic value, without a transition arrival.
An SDC adapter must affirm complete supported-constraint coverage before a
period can be certified. `Result::feedback()` rejects observations without a
certified achievable period, so legacy scalar consumers cannot mistake missing
coverage for zero delay. This certification describes supported constraint/model
coverage; it does not assert measured agreement with a reference timer. The
separate numerical and ranking acceptance gates must pass before GP use.
With named clocks the ideal clock also passes inverters, integrated clock-gate
cells (`clock_gating_integrated_cell`) and recognized clock gates: a cell the
clock enters on one input whose other inputs are enables driven, through
single-input cells, by state that runs on the same clock (checked after
propagation), and that mask the output while the clock rests at one level
(an AND gate at 0, an OR gate at 1; read from the cell's truth table). An
inversion reaches the opposite edge of the same waveform as an inverted twin
clock (phase on the declared falling edge, else half the period) that reports
under its source clock; half-cycle pairs scale the period by their edge
separation (`Pin_result::capture_share`), and a clock pin reports
`clock_inverted`. Each gate enable gets a clock-gating setup check against the
edge that leaves its masking level, with zero setup (`Pin_result::clock_gate`,
`gate_clock`); an ICG keeps its Liberty setup on the enable, and its latch is
not a borrowing latch. A design without these structures times exactly as
before (the characterization golden is unchanged). Divided generated clocks,
other gating, latch borrowing and general Tcl/SDC execution remain unsupported. Timing forces and the opt-in production policy remain
unfinished. A separate read-only GP observation driver is available below.

The legacy single clock and named clocks share one timing implementation; their
published differences are explicit "Legacy policy" branches in `time_setup()`.
`late_timing_test` pins both models bit-exactly against
`tests/late_timing_characterization.txt`. After an intended timing change,
regenerate that image with
`bazel test -c opt //pass/place:late_timing_test; cp bazel-testlogs/pass/place/late_timing_test/test.outputs/late_timing_characterization.txt pass/place/tests/`.

An offline exact-netlist observation uses:

```
bazel run -c opt //tools:late_timing_probe -- config.json
```

The JSON names `liberty` (array, original `.lib`/`.lib.gz` groups with independent
units), `lefs`, `def`, and a fresh `output` path. Optional `inputs` map physical
port names to `arrival_ps`/`slew_ps` rise/fall pairs; `outputs` map names to
`required_ps`/`load_ff` pairs. `clock` supplies `period_ps`, `uncertainty_ps`,
`slew_ps` and physical `roots`. Alternatively, `clocks` maps names to clock
specifications, with optional `phase_ps`, `clock_aliases` and `asynchronous`
clock pairs; IO boundaries name their `clock`. `include_domains:true` includes
per-launch-domain pin states. `constants` maps physical pins to Boolean cases;
`cuts` excludes control-net arcs. `unsupported_constraints` records unmodeled
SDC behavior; `constraints_complete` defaults false. A saved `sdc` file with
explicit `sdc_units` (`time_ps`, `capacitance_ff`) replaces those JSON constraints;
mixing the two constraint sources is rejected. `parasitics` names serialized
physical RC, and `pins` can select output identities. The shared
`physical::mapped_graph` importer preserves redundant/dangling cells and exact
connectivity, with no HDL reader or optimization pass.
`include_predecessors:true` expands selected pins to their rise/fall arrival
predecessor closure for per-arc diagnostics. It does not change propagation.

Reports separate waveform, arrival and required-time pin visits, waveform/arrival
arc computations, library/import/binding/evaluation time, warnings and coverage.
All walks are full walks in this first implementation. Original DEF hard/soft
rectangular placement blockages now survive import, export and round-trip checks.
The placer clips and unions their geometry, subtracts existing fixed occupancy,
and adds pinless placement masks outside the signal graph. IO terminals retain
their native suffix and topology replacements preserve the mask boundary.
Hard blockages constrain row legalization and ECO whitespace; soft blockages
guide GP while permitting final row packing and repair. Component associations
must remain fixed during placement. Hard blockages with temporary macro opacity
changes are rejected until native mask opacity is independent. Routing, partial-
density and polygon blockages remain unsupported. Earlier `timing_only.def`
derivatives, which removed BLOCKAGES only for read-only timing, remain historical
artifacts and must not be used as GP inputs.

An input boundary can also specify `driving_cell` with `cell`, `output`, optional
`related_pin`, and `input_slew_ps`. The native model subtracts the zero-load
intrinsic delay and applies only the load-dependent increment on outgoing port
wire arcs, while retaining the characterized output slew. This preserves the
SDC port arrival itself. Ambiguous, missing or conditional driver models remain
unsupported rather than silently becoming ideal sources. The native
`sdc_subset.{hpp,cpp}` reader checks every command before affirming late-mode
coverage. It covers both original saved benchmark SDCs: exact selectors, ideal
clocks and positive offsets, divide-by-one aliases, asynchronous groups, clocked
IO delays, driving cells and pin loads. Unknown commands, wildcard/variable
substitution and nontrivial clock transforms are rejected. This is a strict data
reader, not a Tcl interpreter. Independent tiny OpenSTA comparisons check
asynchronous cuts, related clock phases and unequal related periods.

The frozen CNN/litepci evaluation in `repros/timeclosure_20261024/` keeps
zero-wire and identical-SPEF comparisons separate. The finite common zero-wire
endpoint transitions pass the declared tolerances, with maximum arrival errors
below 0.00026 ps. The current identical-RC model still fails endpoint arrival and
slew gates in some attempts. Shared-RC reports retain every numerical outlier,
unsupported constraint and fit failure. The current matrix and frozen binary,
source, configuration and validation hashes are in `progress.json` and
`comparison.html`. Model agreement on that cohort does not establish exhaustive
reference endpoint coverage, placement improvement or post-route fidelity.

`//tools:timeclosure_gp_probe` runs a bounded, unweighted final cell GP from an
original DEF with fixed macros and IOs. It uses the original native saved-SDC
reader and evaluates the native timer on actual GP checkpoint RC. Its JSON uses
`liberty`, `lefs`, `def`, `sdc`, `sdc_units`, a fresh `output` directory,
`density`, optional `iterations`/`observe_interval`/`bins`, and explicit `wire_rc`
(`resistance_ohm_um`, `capacitance_ff_um`). Threads are fixed to one; there is no
timing feedback, synthesis, resizing or buffering. Every checkpoint verifies
the original instance/master inventory, connected signal partition, fixed macro
and IO geometry and placement blockages. The final legal DEF restores original
net names and isolated-net records after those checks; final RC, observations,
source constraints and placement statistics remain available for offline STA.
Positioned IOs marked `PLACED` are temporarily frozen in a saved stage frame;
their exact geometry and original statuses are restored on export. The original
input DEF is unchanged, and all placement blockages remain in that stage frame.
Segmented rows are converted to a rectangular solver grid with vertically
merged hard masks for every missing site. Available sites and their row
orientations are unchanged. Original row segments and blockages are restored
on the final DEF; the saved solver frame exposes the exact transformation.
Observed period and constraint/model certification do not imply that the
predeclared numerical acceptance gates passed. This driver does not advance the
timing-closure controller or establish a routed improvement.

The frozen `r7` controls use 20 GP steps and one thread. Both export legal DEFs,
and independent OpenDB audits match every signal connection and instance master,
fixed macro geometry and IO shape/location against the originals. Original-SDC
OpenSTA reports CNN setup WNS of -3716.387 ps (3808.899 ps above the saved
placement), with positive hold WNS of 1.232 ps. Litepci setup WNS is 1271.470 ps
(358.449 ps below the saved placement), with hold WNS of -0.726 ps and hold TNS
of -1.192 ps. These bounded unweighted controls stop before convergence and are
not accepted optimization results. At each final placement, identical-SPEF
native/OpenSTA comparisons retain one arrival-tolerance failure, despite exact
critical-cohort membership/order; litepci also retains a driver-fit coverage
failure. The original-placement and changed-placement comparisons remain
separate in the report. No CTS/routed acceptance has run.

Exact-path diagnostics trace both changed-placement arrival outliers to the
half-adder's default sum arc. Reference debug logs show a failed nonlinear
parameter solve and a total-load table fallback. The native safeguarded fit
satisfies the charge equations for those loads and remains distributed. These
measured reference differences stay in the fidelity results; the native model
does not reproduce the reference solver's numerical failure.

`rc_cache.{hpp,cpp}` adds atomic wire-tree reuse across complete geometry
snapshots. It compares individual absolute pin positions and membership, routing
RC/DBU/provenance and geometry revisions. Load/master or timing-generation changes
retain a valid wire-only tree while marking its timing dirty. Failed updates leave
the previous generation intact; removed nets are discarded and retained snapshots
remain valid. The GP probe's opt-in `reuse_steiner_rc:true` applies this cache at
checkpoints, with `verify_reused_steiner_rc:true` optionally comparing every cached
tree, identity and pin position against fresh FLUTE construction. Observations
report inspected pins/nets, rebuilt/reused trees, dirty timing and removed nets.
All pins are still inspected and all timing walks remain full walks. These
caches alone do not supply continuous wires or forward/backward incremental timing.

RC geometry and cache keys use dense physical-pin IDs scoped to one immutable
connectivity layout. Rebuilding the layout invalidates those bindings even if
the new graph reuses the same ordinals. RC stores numerical Steiner trees and
hashes only numerical geometry/electrical data. The existing design binding
supplies the layout fingerprint once; RC never reads hierarchical names.
The existing named timing interface binds its endpoint labels after RC using
the pin-ID table, without string joins or name lookups inside RC. Numerical
RC fingerprints have a new version, so older internal RC identities get a
one-time cache miss. No placement or timing defaults change.

`star_rc.{hpp,cpp}` now implements an optional continuous driver-to-sink wire
model. A calibration stores the ratio of Steiner wire capacitance to the raw
star's wire capacitance, plus rise/fall compensation for each sink's Elmore
delay. Actual Liberty/IO pin loads are added once after wire scaling. Coincident
anchors retain any reference detour capacitance at the root and use an additive
delay correction where a multiplicative correction would divide by zero.
Movement retains the anchor and changes star branch R/C continuously; changed
loads recompute compensation at the frozen anchor without reconstructing its
Steiner geometry. Membership, geometry-policy revisions and explicit refreshes
create new anchors. Complete-frame failures leave the prior bank intact.

`late::Proxy::evaluate(rc, &star_context)` consumes this model with actual
per-transition loads from the native timing binding. The GP probe enables it
with `continuous_star_wire:true`; `compare_continuous_star:true` also runs the
ordinary native Steiner model on the same checkpoint and reports all-pin
arrival/required/slew/load discrepancies. The diagnostic refreshes at the first
observation and after each 0.1 decrease in physical density overflow, separately
from routing overflow. It does not yet connect the phase-3 controller. Its model
identity is `wire_scaled_star_elmore_v1`. The star's input-admittance pi is an
approximation even at calibration; matching wire load and sink Elmore does not
establish matching gate waveforms or endpoint timing. Numerical/ranking
acceptance, virtual-tree handling and incremental timing remain unfinished.

`star_admittance_correction:true` explicitly selects the v2 experiment. It
calibrates the pi far-capacitance fraction through a bounded odds-ratio transform
and scales its resistance, matching the native reference pi at the anchor while
preserving nonnegative R/C after movement. Electrical rebinding recomputes these
coefficients with the actual new pin loads. The original v1 selection remains
the default when only `continuous_star_wire` is enabled.
`star_max_pin_displacement_fraction` optionally refreshes each net when any
individual pin's L1 displacement exceeds that fraction of the anchor bounding
box half-perimeter (with a one-DBU denominator for coincident pins). It also
detects interior motion with unchanged extrema. Observations retain the maximum
measured fraction and number of displacement refreshes. Neither option establishes
held-out accuracy or enables optimizer feedback.

`star_branch_relative_displacement:true` strengthens that optional guard by
normalizing each sink/driver movement by its own anchor driver-to-sink length,
with the same one-DBU floor. It catches a short branch moving inside a large net
whose total span changes little. This is an explicit experiment; the net-span
guard and v1 defaults remain available for comparisons.

`timing_comparison.{hpp,cpp}` scores native observations by physical pin
identity. Arrival/required differences use the declared max(5 ps, 5%) limits;
slew uses max(1e-8 ps, 10%). Reports retain counts and bounded examples of every
failed metric, coverage disagreements, and the worst prediction/reference values.
Load differences are measured without inventing an additional acceptance limit.
Endpoint critical sets use the same 10%-of-observed-period window as the frozen
comparison protocol, with Jaccard and tie-aware Kendall tau-b gates of 0.9.
Single-period ranking is unavailable for mismatched/multiple active periods,
missing endpoint coverage or insufficient distinct ranks; absent tau never passes.
The comparison utility aligns pins once and computes tau in O(k log k), checked
against an independent pairwise oracle with ties. Supported model/constraint
coverage remains separate from measured comparison gates. These diagnostics do
not enable optimizer feedback or establish routed fidelity.

The full-floorplan handoff audit adds an important limit to the earlier r7
legality results: those outputs are legal within the exported signal-only frame.
The original databases also contain 213,570 CNN and 39,288 litepci locked welltaps.
Restoring native r7 signal poses into those full databases fails OpenROAD's
placement overlap checks. Fixed physical-only occupancy must enter native GP and
legalization before a matched routed candidate can be accepted.

The probe now accepts an explicit `fixed_occupancy_def` containing only
unassociated hard placement rectangles. Its design, DBU and die must match the
original signal DEF; components, nets, IO, rows, tracks and soft masks are
rejected. This physical-only input adds density and legalization masks without
adding graph instances or timing models. The probe saves the occupancy frame,
checks it through observations/legalization, retains its blockages in the final
signal DEF, and records the input rectangle count. Existing rows, original
blockages and signal inventory remain preserved. Without this option, the
ordinary probe behavior is unchanged.

Large blockage inventories use a coordinate sweep with fixed-body precedence
over hard masks and hard precedence over soft masks; the output has disjoint
rectangles and merges adjacent equal spans. Small inventories retain the
historical partition. Final hard-blockage legality uses row interval indices
and checks exact vertical overlap, including unaligned macros and nested
intervals. Independent raster and sparse-obstacle regressions check the union;
the GP smoke verifies physical-only exclusion/export and frame mismatch rejection.
Benchmark full-frame handoff checks apply exact native signal poses to private
copies of the original OpenDB, preserving every physical body and PG connection,
without external placement or repair. These checks and preCTS setup/hold scores
remain separate from convergence, optimization and routed acceptance.
The r14 CNN and litepci controls now pass that original full-frame placement
check with all 213,570 and 39,288 locked welltaps respectively. Both independent
signal/master/connectivity/fixed-macro/IO audits pass. They remain bounded,
unconverged unweighted controls; litepci still regresses setup and violates hold.

Cached reference
post-route scores now use `6_final` SPEF and propagated clocks; they do not prove
a LiveHD routed result or DRC/hold acceptance.

For an ordinary probe run, `min_iterations` may explicitly be smaller than
`iterations` to permit native density/objective convergence before the cap.
It defaults to the cap, preserving the existing bounded observation controls.
The summary records both bounds; the placement history records the actual
iteration count and convergence decision. Capture-only requires equal bounds
so its final observation remains guaranteed. A continuous-row smoke fixture
verifies actual early stopping; the exclusion fixture retains its separate
geometry checks and makes no convergence claim.

The matched downstream evaluation now captures each cached CTS/GRT/route/final
stage's exports through the same ORFS Makefile and platform defaults. Reference
and native environments match after normalizing private output paths, with the
same four-thread setting and library identities. Private serialized preCTS
seeds retain full physical/PG contents and pass exact signal-connectivity, IO
and expected-pose audits. The runner invokes physical Tcl stages directly;
normal downstream CTS/repair changes are recorded separately from native GP.
Stage errors remain failures even when ORFS writes failure artifacts and returns
success. Checkpoints, source hashes, failed attempts and verified process snapshots
are retained under `repros/timeclosure_20261024/`.

Those runs expose a litepci CTS timing discrepancy: stock live CTS reports
infinite worst slack and no constrained paths, while fresh original-SDC OpenSTA
on the saved databases reports real setup/hold violations. Arrival invalidation
alone does not recover finite timing. A fresh process before the unchanged stock
timing-repair step reproduces the independent pre-repair values within 1e-8 ps
and performs real repair. The initial split omitted its placement-argument
variable; that failure is retained, and its saved post-repair checkpoints are
continued with the original argument setup restored. Recovered CTS passes
placement and reports finite in-flow and independent timing, with violations
still present. The precise invalidation defect and full matched routing,
DRC/unrouted, extracted timing and hold acceptance remain unfinished.

`save_timing_checkpoints:true` saves each observation's wire-only RC, exact pin
coordinates, physical signal frame, normalized wire policy and measured density
and HPWL in `checkpoints.json`. `capture_only:true` stops after the final GP
observation, before legalization, and reports `legalized:false` without a final
DEF export. It requires saving and a final iteration divisible by the observation
interval; ordinary placement options and the GP trajectory are unchanged.
`replay_checkpoints:".../checkpoints.json"` evaluates those inputs against the
original exact signal binding without running placement. Replay validates signal
inventory/fixed geometry and the wire policy, and uses explicit full-precision
RC/coordinate readers. Capture and replay are diagnostic modes with no legal
output claim. The hermetic smoke compares captured/replayed timing, and confirms
ordinary legal exports stay unchanged when observations alone are enabled.

Routed evaluation checks extracted electrical connectivity separately from
placement legality, wire presence and router DRC. The streaming
`tools/spef/connectivity.py` auditor compares exact final-ODB net/pin sets with
detailed SPEF and requires resistor connectivity between terminals. Coupling
capacitors never connect terminals; either endpoint may belong to the current
net. Missing multi-pin extraction, disconnected terminals and orphan local RC
components fail. Nets independently known to connect by abutment are explicit
exceptions. This checks the extracted network, not geometric pin contact or
foundry signoff DRC. Its tests use only synthetic SPEF inputs.

The cached litepci final database passes the extracted audit (73,886 nets,
240,071 terminals), wire-presence check and zero final router markers. Fresh
OpenSTA with the original SDC still reports hold WNS -271.403 ps. Its sys_clk
minimum period including port paths is 2,557.461 ps; clocks without relevant
paths retain unavailable periods rather than infinite-frequency success.
These cached reference checks do not establish native routed acceptance.
The same cached CNN audit passes 260,578 nets / 738,466 terminals, wire
presence and zero final router markers. Its extracted setup / hold WNS remain
-7,356.805 / -1,358.767 ps, with a setup-derived clock limit of 119.663 MHz.

The longer unweighted litepci control r15 reports native GP convergence after
1,420 iterations and legal output, preserving all original signal identities,
fixed macros, IO and 39,288 locked physical bodies. Its last sampled timing
frame remains uncertified with 221 driver-fit failures. Numerical agreement
between two native wire models does not establish model coverage or OpenSTA
fidelity. Fresh original-SDC OpenSTA on the final poses in the original full
physical frame passes placement checks but reports setup WNS -3,388.686 ps
(5,018.604 ps worse than the reference), with hold WNS +4.617 ps. It is not
accepted as an optimization result. A checkpoint-400 inverter diagnostic
establishes an infeasible bounded-Thevenin crossing target over the admissible
capacitance interval; the load lies beyond the last NLDM load knot. That driver
has a known constant output and no feasible setup arrival, but physical waveform semantics still
include constant logic. The failure evidence is retained without waiving
certification or changing production timing behavior.
The recovered r14 litepci native downstream control fails global routing after
all 30 extra iterations (GRT-0116, total reported congestion 5,183). ORFS writes
failure artifacts and exits zero; the evaluation runner retains the error and
marks the stage failed. Its repaired CTS success does not imply routability.

`objective/period.{hpp,cpp}` implements the shared achievable-period objective
and multiplier state on an explicit feasible pin/transition/domain DAG. Launch
and required-time offsets contribute to T*, independently of raw maximum
arrival. The hard 10% near-critical window and checkpoint temperature select a
stable log-sum-exp objective. Backward adjoints give dimensionless delay-arc
weights; endpoint mass sums to one, and boundary/incoming/outgoing flow is
conserved at every vertex, including reconvergence and internal endpoints.
Temperature and window are fixed when interpreting these weights as gradients.
Physical required times remain separate from the multiplier cost. Scaling all
times from ps to ns preserves weights and window membership.

The period guard compares hard T* under the same topology, timing, calibration,
density stage and legality regime. It requires tolerance above measured
repeatability noise, retains the actual best period across small tolerated
changes, and shrinks a bounded dimensionless temperature after regression.
Unsupported observations and incomparable epochs cannot replace the retained
best. The caller must reevaluate the retained placement before rebasing an
epoch. Twelve hermetic checks cover a 1 ns request on a 6 ns network, an actual
smooth-objective improvement rejected for worsening T*, an enumerated path and
finite-difference gradient oracle, rank swaps, signed characterized delays,
zero-delay paths, extreme offsets, conservation and invalid/unconstrained DAGs.
This is the shared objective kernel for phase 3 and later buffering/sizing;
proxy-to-DAG export is opt-in through `Evaluation_options::capture_feasible_graph`.
The captured graph preserves separate launch domains, physical pin/transition
identities, every eligible cell/wire arc and endpoint seeds before the backward
required-time walk. `objective/proxy_bridge` verifies every reconstructed hard
arrival and T* against the observation. It rejects missing graph capture,
unsupported timing and unsupported independent-period coverage. Six bridge
checks cover reconvergence, named/legacy clocks, constants, asynchronous cuts,
serialized wire RC and malformed/inconsistent observations.

The offline native timing probe accepts `period_objective:true` to report
period/window metrics, conserved endpoint/boundary mass and per-pin rise/fall
weights. With the flag absent, both original CNN/litepci zero/shared-RC results
remain exactly equal to the previous frozen build apart from resource timings;
capture-on results also retain identical observed pin states. Original zero-RC
objectives reconstruct 954.921 ps for CNN (1,312,920 vertices / 2,412,384 arcs)
and 1,164.961 ps for litepci (436,304 / 792,772). Shared-RC observations retain
their existing driver-fit coverage failures and withhold weights. These are
offline proxy observations; production GP force/rollback integration remains
pending. Native coverage and independent fidelity gates are not waived.

`forces/pin_weights` turns a captured feasible graph into per-connection timing
weights. It max-normalizes LR mass on each physical driver-to-sink arc and a
ranked endpoint path term. Paths contribute `max(delay,0)/k^2` to the sink for
wire arcs and to the influencing input for cell arcs. Domain-specific worst
predecessors remain authoritative. The initial hybrid is `0.5*graph + path`;
per-pin adaptive blending uses eta0=0.4, a=5, b=2 and clamps eta to [0,1].
Pins leaving the window disappear immediately. Changed driver identities and
topology/model epochs reset their smoothing history. Validation failures leave
the bank intact; value copies support a retained-state rollback. Eight hermetic
cases cover ranking swaps, physical attribution, time-unit invariance, stale
pins, domain predecessors, invalid paths and extreme adaptive parameters.

The owned DREAMPlace checkout now has an additional typed `PinTiming` objective
and checkpoint update. It uses the existing two-pin weighted-average smoothing,
pulls only the selected sink and driver, validates net membership and preserves
fixed geometry. Empty arrays clear weights; topology replacement clears obsolete
pin identities. Saved GP state includes the objective, and wire-only feasible
point selection cannot undo an active timing objective. Eight focused native
checks, the existing native placement suite and the LiveHD GP smoke pass with an
explicit local repository override. The pre-existing sibling filler changes are
preserved and separately archived. These capabilities are now consumed through
the published DREAMPlace pin in `MODULE.bazel`; a local override is optional
for co-development. The old pin's filler-fraction fixture failure is resolved
by the new native range (positive multipliers through 2).

`forces/gp_timing` now binds this objective to the opt-in fixed-footprint native
GP checkpoint through `Options::native_gp_timing`. The probe enables it with
`native_pin_timing:true`, requiring continuous-star observations and comparisons.
The published dependency supplies the typed API and its capability macro.
Older dependencies reject unsupported activation explicitly. Every update
uses current physical pin coordinates and native Steiner/continuous timing,
checks coverage plus numerical/ranking agreement, and guards hard period with
the stationary Steiner reference. Unsupported timing or a period regression
aborts the trial. Three session tests cover real nonzero-RC timing, rejection
and failure atomicity; a hermetic co-development smoke runs weighted GP through
two checkpoints, verifies stale-force replacement and emits a legal DEF.
It also rejects a tied-ranking fixture without producing a candidate.
An additional explicit `native_timing_rollback:true` enables fixed-footprint
continuing transactions with the owned API. A successful checkpoint retains
all physical and filler coordinates, weights, momentum/old gradients, density
normalization and convergence state. A rejected hard period restores that
state while spent iterations and smoothing schedules remain monotonic.
The timing session keeps its accepted wire/multiplier history, reduces the
bounded softmax temperature and never commits the rejected trial. Unsupported
coverage still aborts. Applied restore events and selected versus spent
iterations are saved in `placement/native_gp_transactions.json`.
Four owned checks compare an independently resumed physical trajectory with
virtual fillers, verify objective restoration, repeated-rejection budgets and
failure atomicity. Four timing-session cases pass. A real RC smoke improves
through iteration 20, rejects iterations 30/40, restores identical accepted pin
coordinates and still produces legal output at the 40-step cap. Mutable
auxiliary policies/topology/footprint changes are explicitly rejected by this
initial transaction interface. Seven-phase control, full mutable-policy
rollback, reservations, buffer materialization, sizing and true incremental
timing remain unfinished.

A frozen same-binary 20-step CNN experiment now compares this force with an
unweighted control using identical original cached signal netlists, full fixed
physical occupancy, models and settings apart from activation/output paths.
Independent full-OpenDB handoff, connectivity, masters, IO and fixed geometry
checks pass for both legal outputs. Fresh original-SDC OpenSTA reports setup
WNS -3,868.750 ps for control and -3,178.469 ps for weighted, an improvement of
690.281 ps. Hold WNS is +1.945 / +13.021 ps. Weighted HPWL grows from
5,194,423.091 to 6,161,221.463 um (18.61%). Native iteration-20 WNS was instead
15.145 ps worse than control; checkpoint and final legalized frames are distinct.
The weighted checkpoints replace 746 initial weights with 419 weights and
remove 704 stale entries. Heavy wire refresh remains, so no speedup is claimed.
Litepci's weighted trial aborts at iteration 10 on a distributed driver fit
coverage failure and produces no final candidate; its control remains recorded.
The frozen local dependency also contains unrelated RUDY changes, so only the
same-binary pair is matched. Initial failed configs and all trial artifacts are
retained under `repros/timeclosure_20261024/gp_force_progress_r1.json`.
These bounded experiments remain unaccepted: CNN setup fails, litepci has no
weighted legal result, and neither supplies a matched routed optimization win.

The recovered matched litepci reference r4 has completed actual CTS, global and
detailed routing, filling and final extraction. Fresh original-SDC final timing
reports setup WNS -429.945 ps and hold WNS -44.147 ps, with a setup-derived
sys_clk limit of 248.142 MHz. Exact extracted connectivity passes all 104,516
nets / 301,332 terminals, wire presence passes and final router DRC markers are
zero. This is a completed matched downstream control, not timing closure or a
native optimization win; the paired native r14 control failed global routing.


`buffering/candidate` now provides the common expanded-buffer/tree evaluator.
It adds actual rise/fall pin loads once, runs the existing native GaTe/pi and
receiver waveform models forward with actual NLDM input slew, then propagates
physical required times backward. Electrical limits, body/routing exclusions
and explicit integer resource capacities remain independent of dimensionless
multiplier cost. Unsupported models, numerical fit failure and work exhaustion
cannot become successful candidates. Twelve hermetic checks cover all 120
fanout 1–4 assignments to direct connections/two buffer types, cascades,
critical isolation, negative-unate drivers, units, slew/cap crossings, shared
capacity, cycles and physical geometry. This is a local evaluator prerequisite;
discrete buffering searches, geometry/gap negotiation, full-graph validation,
virtual density and materialization remain unfinished. See
[buffering/README.md](buffering/README.md).

The later frozen same-binary CNN r3 pair completes 40 steps and legal output,
with exact original full-frame/master/connectivity/IO audits passing. Fresh
original-SDC OpenSTA setup WNS is -3,497.242 ps for control and -3,632.367 ps for
weighted, a weighted regression of 135.125 ps. Hold WNS is +8.635 / +6.190 ps;
HPWL is 4,155,859.730 / 4,844,521.433 um. TNS improves with weighting, while
neither run reaches GP convergence. Weighted native period improves at all four
checkpoints, so no benchmark rollback is exercised; the real-RC synthetic smoke
still provides that integration evidence. The earlier 20-step win is not a
reliable longer-run optimization result.

A separate same-frozen-timer/reader diagnostic on each candidate's own native RC
finds that legalization worsens native WNS by 2,262.228 ps for control and
1,852.605 ps for weighted. Both final native observations retain supported
model coverage with zero driver-fit failures. These before/after frames expose
legalization damage; their RC is not OpenSTA's identical-SPEF fidelity test.
Density/legality compatibility and final output timing need acceptance guards;
no native GP-only improvement or either bounded pair is an accepted result.


The native feedback experiment now accepts two additional, independently opt-in
acceptance controls. `native_timing_max_overflow` defines a fixed physical
density-overflow bound for retained GP timing points. Before any compatible
point exists, over-bound checkpoints defer without a timer observation or weight
update. Later crossings restore the retained optimizer state when continuing
rollback is enabled, or abort otherwise. Spent iterations remain monotonic;
missing timing is explicitly null in transaction records. This bound does not
prove final legalization quality or GP convergence.

`native_timing_final_guard:true` requires an imported complete legal floorplan
and full legalization. It validates the imported reference with the real geometry
checker and evaluates its native Steiner period before GP. The legal reference
is immutable and independent of the overlapping-GP period guard. After actual
final legality checking and fresh final RC construction, native period must not
regress against that compatible legal reference, and feedback must have had a
supported response interval. Missing model coverage, an illegal reference,
missing response or final regression prevents final DEF export. The audit is
`placement/native_legal_timing.json`; a passing native comparison alone still
does not imply overall optimization, OpenSTA fidelity or routed acceptance.
Eight native session cases and the expanded hermetic placement smoke pass,
including actual RC legalization damage and an independently illegal baseline.

Strict original-reference admission currently fails for both benchmarks despite
passing reference geometry. CNN's first reported fit failure is
`__muladd_madd_573.madd/_1205_:SN`, related B, conditional `when=A`, rise; litepci
reports `_084111_:Y`, related A, fall. Their distributed-driver numerical failures
remain explicit, so no legal reference period or final output is accepted by the
strict policy. A separate frozen same-binary CNN control/density-bound feedback
pair is running. The strict cases are admission checks, not matched 40-step
optimization ablations. All start from the exact cached signal graphs and
original physical occupancy; no synthesis/resynthesis runs.


The completed density-bound CNN r4 pair applies actual restores at iterations
30 and 40, returning to iteration 20 at the 40-step cap. Saved GP pin coordinates
match the retained point exactly, and its final DEF matches the previous
20-step weighted output byte for byte. Independent full-frame/netlist audits
pass. Fresh original-SDC OpenSTA setup WNS is -3,497.242 ps for control versus
-3,178.469 ps for density-bound weighted (+318.773 ps); hold WNS is
+8.635 / +13.021 ps and HPWL is 4,155,859.730 / 6,161,221.463 um (+48.25%).
The original cached OpenROAD CNN preCTS setup WNS is -7,525.285 ps, so the
weighted result improves that measured baseline by 4,346.816 ps. Setup remains
negative and the HPWL tradeoff remains material; neither a certified Fmax nor
routed closure follows. Strict original native legal admission still fails.

`driver_scan_r3` isolates both exact strict-reference failures in an instrumented
copy of unchanged production waveform code. The fixed-resistance positive-ramp
model fails its necessary width bound throughout the permitted equivalent-C
range. CNN's half-adder query is inside its characterized slew/load axes, so the
issue is broader than extrapolation. A separate synthetic exact-pi OpenSTA
probe (`reference_driver_pi_r4`) observes DMP solver failure followed by the
characterized total-load table boundary on these contexts; litepci additionally
clips extrapolated negative slew to zero. Production timing/certification is
unchanged. An explicit, validated boundary-model policy is needed before these
reference behaviors can support native admission; the evidence does not waive
coverage or fidelity gates.


The explicit `characterized_driver_boundary` probe option now admits a distinct
`table_boundary` driver model (off by default). Only a failed positive-ramp fit
whose necessary crossing-width bound is strictly violated at both effective-load
bounds and every interior load-table knot can use it. The original
`no_positive_ramp` cause remains attached; threshold, bracket, residual and
numerically ambiguous failures stay unsupported. This boundary uses total-load
NLDM delay/slew, raw Elmore wire delay and characterized threshold conversion;
it does not claim a fitted Pi waveform. Its policy is a separate epoch component
for native period guards and pin-weight history. Reference, continuous and final
legal timing use the same explicitly selected policy. Buffer candidate evaluation
continues to use its strict default and still applies its electrical/resource
checks independently.

Ten waveform cases and nine native GP session cases cover the proof, interior
knots, ambiguous widths, unit scaling, default preservation, separate epochs and
policy-consistent legal/feedback guards. `driver_boundary_r1` reproduces the exact
prior CNN/litepci Pi queries using frozen production sources: strict mode still
fails, while the opt-in boundary matches the observed OpenSTA table fallback.
The paired original-design zero/shared-RC matrix uses the same frozen binary and
unchanged inputs/tolerances. Both designs' two original fit failures become
explicit boundary observations; selected arrival/required/slew values and WNS/TNS
are identical. A modeled achievable period becomes available under that selected
policy. CNN still fails one arrival and seven slew tolerances. Litepci passes the
finite-cohort numerical/ranking gates, with its existing 92 unavailable reference
fields retained. Neither modeled coverage nor this cohort proves full-design,
electrical or routed acceptance.

Guarded diagnostics in `driver_boundary_progress_r1.json` show that both original
legal references now receive native modeled periods. Litepci's fixed 0.1 density
bound defers both GP checkpoints (overflow 0.2503/0.3364); final legalization then
hits a residual-fit failure on `_120149_:SN`, which the boundary policy correctly
leaves unsupported. A separate fixed 0.4 regime accepts GP10 and computes 232
physical pin weights, then rejects GP20 because a weighted pin lacks a physical
data-net driver. No final DEF is exported for either rejected weighted trial.
This exposes a separate force-binding coverage issue; it is not an accepted
optimization result. CNN is restricted to a one-step reference-admission
diagnostic because its independent numerical gate still fails; terminal callback
weights can be computed, with no subsequent GP step consuming them. Production
phase control, true incremental timing, buffer searches/reservations/materialization,
sizing, displacement repair and routed acceptance remain unfinished.


The original litepci matched routed reference r2 has now completed final
extraction. Its independent original-SDC/propagated-clock audit reports setup
WNS +1,067.783 ps and hold WNS -209.868 ps. All 87,004 extracted nets / 266,308
terminals pass exact connectivity and wire-presence checks, with zero final
router markers. Setup passes at the original constraints; hold fails. The paired
native control failed global routing, so no native routed improvement is
established. The earlier recovered r4 reference is a separate physical-flow
experiment with different measured timing; neither result replaces the other.
CNN admission also finishes with an explicit final legal-period rejection:
9,967.636 ps versus the immutable original legal reference 8,518.849 ps, with one
supported update and no complete response interval. It exports no final DEF.


Related-output timing arcs now preserve their ranked path contribution through
the same cell until the influencing physical input is reached. The bank checks
actual declaration direction/name/port and graph/occurrence ownership; unresolved
launch boundaries or ownership/direction crossings remain unsupported. This
covers real Liberty CON-to-SN dependencies without inventing an input net for an
output or creating an unused graph pin. Positive arc costs are accumulated
individually, so negative delay cannot cancel another arc's contribution. Eleven
weight cases include chained outputs, independent rank/normalization arithmetic,
unit scaling, transactional rejection and a native unconnected-output fixture.
The saved litepci GP10 comparison preserves all 232 default-window weights
byte-for-byte; a valid broader diagnostic window now produces 143,740 physical
weights instead of rejecting CON.

The frozen same-binary litepci output-binding r1 pair now reaches GP20. GP10/20
periods are 2,101.556 / 1,892.887 ps with 232 / 629 weights; GP20 records four
related-output path contributions and a completed response interval. Original
signal inputs and locked physical occupancy remain unchanged. Final actual legal
timing still rejects a residual fit on `_120372_:SN`; no accepted weighted final
DEF is exported. Strict model coverage is retained.

A diagnostic comparison applies only the rejected post-detailed component poses
to a private original full OpenDB. The raw internal debug DEF fails canonical
net/IO identity and is not treated as a final netlist. Fresh post-transfer
signatures prove original connectivity/IO, exact component poses, fixed geometry
and full-frame legality. Diagnostic setup WNS is +1,297.909 ps versus the matched
control's +1,270.086 ps (+27.823 ps), hold WNS -0.744 versus -0.353 ps, and HPWL
1,673,675.870 versus 1,745,801.909 um (-4.13%). The rejected pose remains 332.009 ps
worse in setup than the original cached OpenROAD input, and hold worsens. This is
position-only diagnostic evidence, not accepted native output or routed closure.
See `output_binding_progress_r1.json` in the retained evaluation artifacts.

Exact residual-driver diagnostics distinguish numerical convergence from model
fidelity. A private bracket refinement solves the current residual equations,
but gives driver slew 49.480 ps versus independent OpenSTA's 25.296 ps (and
66.830 versus 33.238 ps for the prior failed frame). The reference reports output
crossing failure and uses a partial-Ceff table boundary; matching conditional
arcs are checked by independent native table lookups at that Ceff. More iterations
alone therefore do not pass the existing slew tolerance. Production waveform
code, failure admission and fidelity thresholds remain unchanged.


An additional `bounded_driver_boundary` probe option is independently disabled by
default. It selects two distinct characterized approximations only when Pi wire
resistance is at most 1% of native driver resistance. `full_load_boundary` retains
total-load NLDM delay/slew when the characterized full-load lower-to-middle width
is strictly below its pure-capacitance step minimum. `crossing_boundary` retains
the fitted effective load and table delay, but uses table slew when the fitted
Pi cannot reach its upper threshold by ramp duration plus twice total capacitance
times driver-plus-wire resistance. Both use raw Elmore wire delay with the normal
receiver threshold/derate conversion. They report separate `full_load_ramp` and
`crossing_window` causes and separate fit counters; neither claims a solved
physical waveform or electrical feasibility. Other fit failures remain explicit.

The bounded policy has its own timing-result, comparison, pin-weight-history and
retained-period epoch component. Native reference, calibrated continuous and final
legal evaluations receive the same immutable selection. This prevents comparing
strict and bounded observations or smoothing weights across their model boundary.
The earlier `characterized_driver_boundary` proof and its default are unchanged;
its whole-effective-load infeasibility test remains a different model family.
Buffer candidate evaluation continues to use strict defaults.

Private model development used exact original CNN/litepci signal graphs and
identical serialized wire RC in native timing and offline OpenSTA. An unrestricted
two-time-constant crossing rule introduced 98 new CNN slew tolerance failures and
was rejected. Restricting both rules to the declared resistance regime introduced
no new failures in the complete development cohorts (4,250 CNN / 766 moved litepci
pins); existing internal timing discrepancies remain. A separately predeclared
unweighted litepci final-control pose passed all finite arrival/required/slew
checks across 16,853 original endpoint and affected-driver/source/sink pins.
These are development and control-frame checks, not disjoint routed calibration
or overall optimization acceptance. The existing CNN endpoint outliers, broader
internal coverage/fidelity, production phase controller, actual incremental timing,
buffering/materialization, sizing, repairs and native routed comparison remain
unfinished. Enabling this model does not waive those gates.


Native timing priorities during legalization
--------------------------------------------

The diagnostic native GP probe accepts `native_legalize_timing:true` to observe
fresh native timing at the retained GP frame, after legalization and after
detailed placement. The independent opt-in strengths
`native_legalize_displacement_strength` and `native_legalize_net_strength` default
to zero and must be finite in `[0,99]`. Observation or either positive strength
requires `native_pin_timing:true` and `native_timing_final_guard:true`.

The handoff evaluates actual retained coordinates with fresh physical wire RC
and the same driver model policy. A fresh pin-weight bank uses unsmoothed raw
criticalities at the current timing temperature; it does not advance GP history,
rollback state, response intervals or iteration budgets. Maximum-normalized
criticality protects both movable sink and driver owners. Cell displacement costs
and detailed-placement net costs multiply existing costs by
`1 + strength * criticality`; fixed-cell costs and disabled zero-cost nets remain
unchanged. Displacement anchors are the actual retained GP coordinates. No new
cells, connectivity changes or synthesis are involved.

`placement/native_legalize_timing.json` records the fresh handoff period and
priority counts. The accompanying cell/net TSV files preserve identities, costs
and anchors. `placement/native_legalize_stages.jsonl` records geometric legality,
period and the imported-reference nonregression check separately after
legalization and detail. These observations do not waive the mandatory final
legal guard or independent original-SDC OpenSTA and physical audits. A zero-strength
observation control performs the same timing measurements as positive-strength
trials, allowing legalizer effects and evaluation overhead to be measured
separately. This is an experimental handoff; production phase control,
displacement limits, electrical repair and routed acceptance remain unfinished.

Rejected native timing checkpoints
---------------------------------

A failed continuous/Steiner comparison now saves
placement/native_gp_rejected_<iteration>.comparison.json, .rc.json and .pins.json.
The synchronous read-only rejection observer runs before calibration,
weight history or retained timing is committed. It cannot authorize a failed
comparison. Existing driver coverage, numerical, critical-set/ranking and hard
period guards remain unchanged.

The report records individual numerical/coverage/ranking gates, tolerance counts,
worst examples, unavailable ranks as null, calibration statistics, both complete
pooled pin/transition observations, predecessor identities and active-clock data.
The sidecars preserve the actual wire-only RC and precise pin positions at the
rejected frame. The diagnostic_only flag and optimization_state_committed:false
identify the files; they are neither accepted placement exports nor independent
OpenSTA fidelity results. The saved timing observations reproduce the comparison;
reconstructing a prior star-cache history still requires its original trajectory.
No extra timing evaluation or recovery is hidden in the diagnostic writer.

The separate native_timing_recalibrate_on_mismatch probe option defaults to false.
When enabled, a supported but inaccurate continuous observation first records
its failed comparison, then retries once with fresh native calibration at the
same RC/pin frame. Coverage, numerical and ranking gates are all checked again;
the stationary Steiner hard-period guard still decides retention/rollback.
A second comparison failure aborts. Missing model coverage is never repaired by
this mechanism, and no external timer is called.

Attempt metadata distinguishes the initial surrogate rejection from a failed
retry. A successful comparison refresh is logged in
placement/native_gp_recalibration.jsonl with its single extra native evaluation,
periods, calibration counts and whether the hard-period guard rolled back.
A calibration from a rolled-back candidate is not committed. The first report
describes state before that attempted retry, even if a later comparison succeeds.


The optional explicit-input `//tools:buffer_geometry_probe` now supports bounded
cross-net allocation through `pass/place/buffering/negotiation`. Finite physical
alternatives are reevaluated in their saved native contexts and proposed against
one shared whole-site/resource ledger, with critical/window priority, congestion
history, retained protected ownership, explicit unmet demand and bounded actual
RC-path gap-entry pulls. This does not activate production density/forces or
materialize buffers. See [buffering/README.md](buffering/README.md) for scope and
checks. Synthesis/resynthesis remains outside the matched-netlist evaluation.


Negotiated stage-zero gap-entry pulls now have an owning native force kernel and
`buffering/gap_forces` physical-pin binding. Critical/window conflicts are frozen
at the checkpoint; bounded target weights, stale-target clearing, fixed-driver
protection and complete optimizer rollback are checked. The published
DREAMPlace pin supplies the gap API; no local override is required. The optional
explicit-input probe validates CNN/litepci cohort forces at saved component poses. Production density accounting, checkpoint allocation
refresh and buffer materialization remain pending.

The `buffering/density` checkpoint adapter now converts negotiated, ledger-checked
physical buffer footprints into owning native reservation density. Virtual charge
enters the ordinary density kernel once and displaces equal filler charge;
physical bindings transfer it to ordinary node occupancy. Native rollback carries
reservation payloads and density shape/ratio arrays. The published dependency
now supplies this API; reservation accounting remains opt-in, with no change to
its activation default. Production phase/LR refresh, graph/pool
transactions and full timing/rank/routed acceptance are still pending; see
`buffering/README.md` for the exact test and diagnostic boundaries.

The internal opt-in `Options.native_gp_buffering` now shares one native checkpoint
transaction for freshly classified LR/window/source context, finite negotiation,
gap pulls and reservation density. Physical preparation precedes timing policy
commit, and the actual callback supplies exact physical master metadata. Public
placement and repeated native GP/session tests exercise nonempty joint updates.
This requires gap API 3 and reservation API 2, both supplied by the published
DREAMPlace pin; older dependencies report unsupported. Production phase actions,
scalable candidate generation, whole-graph virtual-buffer timing and graph/pool
transactions remain pending. See
`buffering/README.md` for exact proof and replay boundaries.

### Fixed startup timing hint (native experiment)

`Gp_timing_settings.startup_coefficient` enables one initial native timing walk
using placement-estimated Steiner RC and the existing characterized technology
wire model. `startup_path_fraction=0.30` ranks all supported constrained endpoint
transition/domain paths by fixed-SDC setup cost and selects the top 30 percent,
rather than using a 30-percent slack window. Equal total mass per selected path
is accumulated on physical wire sinks and normalized to [0,1]. The coefficient
is bounded by the later timing-force coefficient; trials use 0.025 versus 0.25.
The hint stays fixed until the normal checkpoint replaces the pin payload.
No later LR history, calibration or hard-period guard is advanced by startup.
`native_gp_startup.json` records coverage, selected paths, force strength and
startup wall time. Missing timing or physical bindings fail explicitly.

The explicit diagnostic JSON keys are `native_timing_startup_coefficient` and
`native_timing_startup_path_fraction`, requiring `native_pin_timing=true`.
The startup hint remains disabled by default while matched CNN/litepci preCTS
and runtime ablations are evaluated. Initial geometry RC is an estimate and
these path weights do not establish routed timing acceptance.


The optional diagnostic key `native_timing_startup_average_wire_um` replaces
initial placement wire geometry with a wire-only star of the specified mean
branch length, using characterized technology R/C. Pin loads remain in the
library exactly once; original ideal/zero-wire nets (including clocks) stay
ideal. The mean can come from prior saved wire capacitance divided by technology
capacitance per micron and the nonzero-wire receiver count. This is an explicit
approximate startup prior, never the later RC calibration or acceptance model.
If initial native period coverage is unsupported, the optional hint emits no
weights and records its reason; later timing gates remain mandatory. Successful
startup metadata records the prior type, mean length and coverage status.

The first geometry-based CNN hint failed initial coverage; its failure is
retained. A mean prior from earlier GP20 snapshots has certified native coverage
on CNN and litepci without changing driver or later timing policies. Replacement
matched trials preserve the existing feedback start (iteration 0, first sample
at 10) in both controls, isolating the startup hint from cadence changes.


With no explicit mean length, startup now estimates one global branch length
from current wire-only capacitance/resistance and technology coefficients, then
freezes a uniform wire-only prior for ranking. Native 1e-9-ohm pin contacts are
excluded from length estimation; ideal networks retain their original contacts.
An explicit `native_timing_startup_average_wire_um` overrides this automatic mean
with a cached or characterized prior. Both choices stay separate from later
physical-RC calibration and guards. The replacement CNN/litepci on/off matrix
keeps feedback start at iteration 0 in both variants; the earlier delayed-cadence
and geometry-based attempts remain separately labeled diagnostics.

The geometry diagnostic `//tools:signal_route` accepts `--patterns N` and
`--budget-ms N` for explicit larger search experiments. Their defaults remain
10,000 trials and 1,000 ms; they do not change the placement flow's per-net or
stage budgets. Diagnostic DEF output is not timing/capacity acceptance.

For an RC diagnostic, also supply `--rc-file`, `--liberty`, `--driver instance:pin`
and `--diagnostic-rc output.json`. This uses the selected physical pin accesses
and production routed-RC estimator; the output remains explicitly approximate
and does not perform setup/hold or capacity acceptance. Widened maze runs use
width-only DEF `NONDEFAULTRULES` and per-path `TAPERRULE` references so downstream
path-based extraction sees the actual width. Rule definitions are evaluated in
a private trial frame and installed only when the route passes acceptance.
Existing rule names are reused only for identical definitions; otherwise the
router chooses a distinct name. Ordinary-width routes keep the existing path.

Geometry decoding resolves the rule on each routing layer, including after a
via, and routed RC unions overlapping widths. Unsupported rule constraints
are rejected instead of discarded. Width-dependent resistance and effective
ground capacitance remain approximate; extraction using a table characterized
only at nominal widths does not validate wider-metal electrical accuracy.

Channel accounting reports `freed_signal_tracks_by_layer` as a conservative
lower bound on distinct preferred-track centerlines that gain an ordinary-width
corridor inside a requested channel. Each corridor spans the channel extent
with both ends inset by the layer's maximum rule clearance. The count checks
rectangle legality and spacing against all pins, blockages, signal wires, PG
wires and via metal in both generated grids. Overlapping patches count a
centerline once per layer. Its obstacle indexes retain only requested channel
layers, including via landings and conservative blockages on those layers;
ordinary routing occupancy still indexes every layer by default.
This measures available corridor geometry, not via
access or end-to-end routability; short or obstructed gaps may count zero.
Unsupported rules or tracks leave the value null with an explicit
`freed_signal_tracks_unmeasured_reason`. No-channel flows avoid the comparison.
The `//tools:power_grid` audit accepts `--report PATH` with `--def-output` to
write the same accounting report used by the placement flow.

Accepted signal-net records include `timing_at_acceptance`, with candidate-net
setup/hold slack and the minimum setup/hold slack including previously locked
pins, all in ps. These values describe the routed RC at that acceptance step;
later accepted routes can change them. They are not final-stage timing or
post-CTS extraction. The diagnostic uses existing admission queries, does not
change margins, and is transient report data rather than a graph attribute.
Records without an acceptance timing check omit this object.

The top-level `timing_cut_pins` array records the final candidate's non-clock
net endpoints excluded by the placement timer. Labels use `instance:pin` for
cell pins and the port name for top-level ports. Matched timing audits can use
these exact identities to reproduce the optimization exclusions, including
internal reset nets. The report does not itself change downstream constraints;
an audit must distinguish this timing view from unrestricted recovery/removal
checks and validate endpoint bindings before applying exclusions.
`livehd::preroute::apply_timing_cuts` in `preroute_handoff.tcl` accepts this
array as a Tcl list. It resolves all port and cell-pin identities exactly,
rejects missing, ambiguous, or clock-network bindings before changing constraints, and installs
an explicit through-pin false-path exception. Apply it consistently to all
matched arms before CTS/repair, then save the SDC for subsequent stages.
Sourcing the helper alone leaves timing constraints unchanged.

`preroute_tree` is the internal all-or-nothing routing entry point for a
privately proposed repeater tree. It requires every branch binding to pass the
ordinary geometry, capacity and setup/hold gates. A failed branch returns only
rejection diagnostics, marks successful trial branches as rolled back, and
publishes no partial frame or RC state. The caller must retain its original
netlist until the entire trial succeeds. The final-flow host uses the scheduler
described below to publish an accepted tree with its private graph.

When the native pre-router resumes a frame with fixed signal wires, it builds
cell and power-grid occupancy once. Capacity uses that static index, and a copy
receives the existing signal shapes for net-aware route searches. Existing wires
are then reserved separately in the capacity model, preserving the distinction
between static blockages and consumed routing capacity. Static construction
omits signal-wire decoding directly while retaining pin ownership, avoiding a
full floorplan copy merely to clear its wire lists. This also avoids decoding
the same static geometry twice. When repeater candidates are eligible, the
stage first decodes the unchanged special-net wiring into an immutable shared
index. Each trial rebuilds cell/pin ownership and adds its own geometry in a
private overlay; rejected trials cannot modify the shared index or another trial.
Reuse requires the same technology object, DBU, via definitions, width rules and
special-net wire paths. New supply connections are allowed, since repeaters join
existing rails, but changed supply geometry fails explicitly. The technology must
remain immutable while the index is alive. The one-time build is included in the
stage deadline and reported as `power_grid_occupancy`; per-net budgets and all
routing/timing gates remain unchanged. Accepted and rejected repeater trials
both retain routing phase times under `repeater_route_*`; rejected net records
also carry the corresponding `route_*` breakdown. These are subsets of
`repeater_trials`, not additional runtime to add to that total. The disjoint
`repeater_initial`, `repeater_proposal`, `repeater_capacity_seed`,
`repeater_snapshot` and `repeater_tree`
phases measure attempted-tree preparation and execution, including rejected
proposals and exceptions; they are also subsets of `repeater_trials`. The
`repeater_route_*` phases are nested inside `repeater_tree`. Setup-deadline
failures retain the work spent before cancellation. Deferred-PG routing does not reuse this
index because that mode changes the supply geometry.

For nondeferred PG, the first legal repeater proposal also builds a static
capacity seed. Subsequent rejected proposals reuse that seed only while the
existing components, tracks, terminals, supply wiring and capacity configuration
remain unchanged. Each trial clones the seed, subtracts capacity blocked by its
added cell pins and obstructions, then reserves its current fixed signal wires.
Reservations and acceptance proofs belong to the individual trial. An accepted
tree invalidates the seed; incompatible geometry falls back to full construction.
Seed preparation is lazy, bounded by the trial deadline, and reported under
`repeater_capacity_seed`. Candidates with no legal repeater placement do not pay
that preparation cost. This optimization does not relax remaining-net demand,
DRC, pin-access or timing checks.

The pre-route repeater site reservation helper, `reserve_repeater_sites`, searches
cut N/FS rows for single-height CORE buffers and leaves every existing component
in place. It reserves whole sites, respects master flip symmetry and die bounds,
and returns no partial placement if a tree cannot fit or its deadline expires.
Requests use desired buffer centers in DEF DBU; the deterministic greedy search
minimizes Manhattan displacement for each request in order. Existing signal wires
are indexed once per reservation; blocked candidates are skipped in favor of the
next nearest site. The shared clearance filter reserves the body and transformed
pin/OBS overhang on layers occupied by the cell, and caches bounds by master,
orientation and dimensions. Unsupported clearance rules fail closed. The final
snapshot repeats this check before routing. Reservations enter a private
netlist/timing snapshot and are published only after successful routing.

`preroute_repeated_tree` joins the temporary timing edits to that routing
transaction. Its caller supplies a private frame/RC/demand snapshot with legally
reserved FIXED repeaters while the timer still holds the original netlist. The
transaction verifies exact endpoint preservation and a connected, acyclic buffer
tree, checks margins, temporarily adds the buffers, routes every branch, and
checks the actual routed RC against the original timing state. Rejection exposes
only diagnostics; both success and failure restore the caller's timer mark.
The cached `detail::Repeater_proposer` generates a private graph with the existing
Steiner repair planner using explicit routing-layer RC. It preserves existing
buffers and all existing placements, and reserves only new buffers on legal free
sites. A cell fixed by an earlier pre-route may participate through a different,
unrouted pin; its master, placement and previously routed pins remain unchanged.
Already-wired pins and unrelated fixed or locked cells remain ineligible.
`detail::repeater_snapshot` rebuilds the matching DEF, RC and routing-demand
views, adds the buffer supply connections, and conservatively rejects sites that
could invalidate existing signal wiring.
The stage reuses the fixed-wire site filter across rejected snapshot trials and
rebuilds it after each accepted tree. Shared filter construction is reported as
`repeater_snapshot_wire_filter` and counts against the stage deadline; it is
outside individual `repeater_trials`. Standalone snapshot calls still construct
and validate their own filter.

`detail::preroute_stage` first routes the short-net batch, then tries long nets
in length/slack order. The length threshold comes from the permitted Liberty
buffer and characterized routing RC. Planning, site reservation and routing share
the per-net deadline; all trials also share the stage deadline. Clock/reset and
output-drive characterization is loaded once, only if the short-net batch leaves
time for a long-net trial; this shared setup counts against the stage deadline.
Rejected trees
leave the original net untouched. Accepted trees install their RC in the stage's
temporary timing journal so later trials account for them. Every stage exit
restores the caller's timing mark; an installation failure aborts the stage.
The host publishes a private graph with matching FIXED geometry and RC, refreshes
the cell/net counts and signal HPWL, and records final `preroute_timing` in ps.
Reports distinguish `source_mapped_nets` from branch records and include
`repeater_trees` and `repeater_cells`. Full routed acceptance of automatic
repeaters remains pending.

When pre-routing resumes from a frame with FIXED signal wires, it matches each
wire's complete endpoint set to the demand snapshot and removes that net's
estimated RSMT demand. Capacity is rebuilt against static obstacles (pins,
macros and PG), then the existing signal geometry restores its track reservations.
This preserves the cumulative pre-route fraction limit across repeater trials;
restarting a routing transaction does not grant another capacity allowance.

The PG audit tool additionally accepts `--reserve-signals true` (default false).
It derives whole-stripe skips from existing ordinary-net wire geometry and the
metal spacing oracle before building the grid. Followpin rails remain fixed;
a signal conflict with a followpin fails. The resulting grid must pass the
existing attachment/connectivity checks and a final signal-versus-PG check
including emitted via landing pads, patches, and cut spacing. The input signal
wires remain unchanged. The report records the generated channel patches;
`automatic_channel_count` in the plan distinguishes them from explicit patches.
This is a reservation experiment, not automatic strap relocation or an IR-drop
proof. The generator still requires an unwired PG frame. Existing DEF-local signal
via definitions are preserved; generated PG vias receive distinct names on
collision. The SAT host can enable it with `synth.place.satlegalize.route.pg_after=true`
(default false; requires `synth.place.satlegalize.route=true`). It uses the
final legalized positions and signal IOs, keeps followpin obstacles during
signal search, and rebuilds PG around each trial before acceptance. Existing signal and clock IO rectangles are checked in placed orientation
against the rebuilt PG without moving them. Capacity
is recomputed against the rebuilt grid, including every previously accepted
signal. A rejected trial preserves prior routes, placement, and timing; an
empty accepted set returns the reference grid. The report records
`pg_after_sat_routes` and per-attempt `power_grid` time. Baseline PG is still
constructed during placement for final IO legalization. This experimental
mode skips conflicting stripes; it does not relocate them or certify IR drop.

For a non-emitting physical routing diagnostic, `//tools:signal_route` accepts
`--probe true`, with `--straight true` for common-track span search. Add
`--clear-routes true` to clear signal/PG wires only in its private in-memory
frame, retaining pin ownership and the input DEF unchanged. This reproduces
pre-PG access checks; it is not a flow handoff or a way to bypass legality.
The result prints the route rejection reason and search/deadline counters.

### Comparing physical chunk placements

Use `lhd synth chunk init --budget 2GB --producer-budget 12GiB --workers 3 --set synth.place.chunk.target=4` with the design's normal sources, physical libraries, clock constraints and synthesis options. The worker budget applies independently to each home. The producer currently builds the mapped physical view for the whole design, then exports indexed incidence data by home. It records process-group peak resident bytes and enforces its separately supplied limit during mapping and export. Set `synth.place.snapshots=true` to retain the physical reference DEF for independent validation.

The planner preserves hierarchy near memory-balanced cuts and ranks feasible rectangle assignments by crossing-net span. The exported `chunk_plan.shape_candidates` lists distinct assignments in rank order. Run `shapes --screen` after placement to project measured pin spans and test boundary slots for each frame. Its Pareto ranks identify tradeoffs between missing slots and projected wirelength. With a positive target, `synth.place.chunk.area_weight` from 0 to 1 trades the predicted worker load against occupied area at each hierarchy cut. The worker byte limit applies at every weight. Create a new project from the saved recipe with `lhd synth chunk init --from-workdir W --workdir W_alt --set synth.place.chunk.shape_rank=1`. This copies mapping caches, inherits the budgets, preserves reader arguments and starts an independent placement journal. An area-weight or target override can compare a new ownership partition through the same command.

From an initialized project, these commands run the independent homes and compare local hypotheses. The accepted placement and each trial are visible in `chunk gui`. Add one `--compare-workdir W` per alternative floorplan or design to switch between them in the same viewer. Each tab keeps its selected project and all projects share the worker admission limit. The socket lists the project IDs with `projects` and accepts a `project` field on requests. Running `tools/lhd_gui.py --workdir W explore kind=density iterations=300` returns a trial ID immediately. Use `history`, `compare`, `view` and `watch` on the same socket to inspect the branches and their progress.

```sh
lhd synth chunk macro --workers 3 --iterations 120
lhd synth chunk final --workers 3 --iterations 300
lhd synth chunk explore macro --workers 3 --iterations 120 --final-iterations 300 --keep
lhd synth chunk explore density --workers 3 --density 0.45 --iterations 300 --keep
lhd synth chunk interfaces
lhd synth chunk explore interfaces --workers 3 --iterations 300 --keep
lhd synth chunk shapes --screen
lhd synth chunk routing --summary
lhd synth chunk resources
lhd synth chunk assemble --def accepted.def
lhd synth chunk validate --openroad /path/to/openroad --route M2 M7 --capacity 0.75
```

Each search uses the same starting handoff and frozen exterior for its control and edited branches. Acceptance compares the selected objective (assembled physical pin HPWL by default, estimated worst slack with `--objective wns`) against both the equally advanced control and the current placement with matching stage coverage. Macro and density trials can combine results from different homes while keeping the same interface. A matched control can be the lowest-HPWL placement observed. Use `restore-best --objective hpwl` to explicitly recall the complete best comparable placement in the current immutable plan, including its contacts and poses. The prior accepted state remains available to `undo`. The IO coordinator reserves anchored contacts, allocates shared slots and versions every participant in a changed interface. A newer export with track patterns can report preferred-layer assignments. Set `synth.place.chunk.route_layers=M2:M7` or the relevant technology range to match the downstream routing stack. Pin access and detailed DRC require downstream validation.

At the live FP gate, select adjacent homes and use **Preview stepped edge**. The socket takes the same operation in DBU. A positive depth extends the first band toward increasing X on a vertical edge, or increasing Y on a horizontal edge. A second band gives the same area back. Optional `band_dbu` and `offset_dbu` select the strip width and starting offset along the shared edge. Geometry snaps to the site's X pitch and the row's Y pitch. Reapplying the pair replaces its depth. A zero depth restores the straight edge. Different pairs can be edited together when they have disjoint owners. A physical macro footprint plus its halo or a fixed terminal can rule out an exchange. The worker receives the bounding frame and excluded strips. Local placement, legalization and final stitching check the actual footprint.

```sh
tools/lhd_gui.py --workdir W chunk_edit field=geometry 'op={"step":"HOME","peer":"PEER","depth_dbu":1000}' apply=false
tools/lhd_gui.py --workdir W export what=chunk_transfers chunk=HOME peer=PEER limit=5
lhd synth chunk interfaces --workdir W --spread --bus-order
lhd synth chunk explore interfaces --workdir W --nets NET0 NET1 --spread --bus-order --iterations 120 --workers 3
```

The transfer query ranks whole colors by their change in shared nets, then by their change in cuts across all homes on the inter-color graph. Preview a transfer to measure the full physical floorplan. Compare the color-center routing overlay with the boundary-contact overlay, which includes the actual taps and the local logic density. Both are wiring estimates. Independent routing results appear alongside estimated HPWL and WNS after validation.

Set `synth.place.io.spread=true` to reserve extra track spacing and corner clearance for automatic package pins and chunk IOs. The chunk producer snaps its exterior seeds to legal slots in perimeter order, preserving the order of each bus. Imported and fixed package pins retain their locations. A boundary trial can enable or disable the same spacing policy independently. At a stepped interface, both owners receive the same coordinate and preferred layer. The allocator relaxes spacing as slots fill. `--bus-order` orders named logical bits along the connected segments, choosing the direction with the smaller pin-span cost. The result reports ordered groups, groups with anchored or ambiguous bits, and signals that could not obtain a slot. Test complete named buses as a unit when comparing a subset.

**Follow active design** in a multi-project GUI tracks current workers, fork branches and independent validation across the registered workdirs. Choosing a project pauses following for that browser tab. The `activity` socket command returns the same cross-project feed. The accepted placement and each completed experiment remain available for inspection.

For separate machines, issue `lhd synth chunk dispatch BUNDLE macro HOME`, transfer that directory, run `lhd synth chunk worker BUNDLE` with the identical executable, and return the directory for `lhd synth chunk collect BUNDLE`. The coordinator validates the bundle and its home dependencies. Independent replies may arrive in any order. Repeating a delivery is idempotent. On a Linux worker with a delegated cgroup v2 subtree, `worker --cgroup-root DIR` applies a kernel memory limit to the native worker process group.

Routing estimates report crossing, internal, capacity and predicted excess tracks in horizontal and vertical directions. The floorplan uses a tile spanning 128 technology track pitches and reserves capacity for logic density. Each local placement estimates internal demand from actual pins using a 32-pitch tile. Reports aggregate the native tiles for display. The optional OpenROAD validation stitches local placements, checks site legality and measures placement-RC timing. With a layer range it also reports all-net global-route wirelength, overflow and route-RC timing. Use `--segments` to retain the independent route geometry for spatial checks. Add `--trial ID --branch control` or the candidate name to `validate` or `assemble` to measure an archived placement without changing the accepted design. When the timing delta matters, run `verify-wns --trial ID --branch NAME --openroad /path/to/openroad --keep` on a fully final-placed experiment. It checks placement-RC WNS at the frozen checkpoint, control and candidate, records the finding, and publishes the candidate only when both comparisons improve and the checkpoint is still current.
