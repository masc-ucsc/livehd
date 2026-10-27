# Physical placement stage

With ABC, OpenTimer and an explicit workdir, the default runs macro placement,
fixes the macros, and then places the cells (`synth.place.fp=false`). Without
macros it uses a single placement stage. Macro snapping remains enabled.
Enable `synth.place.fp=true` explicitly for centroid floorplanning and the separate
macro-refinement/final-cell passes. The legacy `r8h2`/`r8h3` macro modes and manual
macro-centroid diagnostic have been removed.

GP timing feedback is approximate critical-path ranking. It uses characterized
gate delay at total load and a closed-form Elmore wire estimate, updates the
existing timing graph for changed RC and gate strengths, and exports only pin
slacks and clock flags. `synth.place.timing_fraction=0.2` selects the worst-ranked
20% of timed data nets (`0.1` selects 10%); weight magnitudes follow rank rather
than picosecond differences. The existing smoothing controls how quickly weights
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
| `grid.route` | `inherit` | the one routing grid for RUDY/RSMT/URoute, macro channel pressure, native legalization/detailed placement, terminal bins and SAT route-grid bins; `inherit` follows the density grid in use (`grid.density`, the chosen count in macro mode, and the configured `grid.density` in dynamic mode), or an explicit 2..1024 |
| `grid.local` | `512` | local spreading grid per axis |
| `region.max_cells` | `10000` | movable-cell cap per region rectangle |
| `region.merge_um` | `5` | merge macro-edge/IO-span cut lines closer than this |
| `region.busorder`, `region.boundarypins`, `region.pinalign` | `false` | the mesh consumers; their sub-options keep their names under the same prefix |
| `legalize.bin.grid` | `route` | terminal legalization bins only: the routing grid (bounded for tiny cores) or an explicit 1..1024 used as given. SAT route-grid bins keep `grid.route` |
| `legalize.bin.max_density` | `0.8` | hard terminal bin cap; must exceed the GP target |
| `satlegalize.bin.size_um` | empty | fixed SAT bins `W,H` µm anchored at the die origin |
| `satlegalize.bin.grid` | `none` | `route` uses the routing grid for SAT bins; excludes `bin.size_um` |

Stage-scoped grids keep their names: `<stage>.warpspace.bins` (16) and
`preroute.bins` (64). URoute resamples the routing grid through its fixed model
resolution; that is not a partition option. The pre-3g spellings (`bins`,
`bin_max`, `local_density_bins`, `busorder*`, `boundarypins*`, `pinalign`,
`legalize.max_density`, `satlegalize.region_size`, `satlegalize.route_grid`) are
accepted for one release: lhd rewrites them to the names above before anything
reads them and emits a `deprecated-option` warning. `dynamic_bins` and
`auto_bins` merge into one `grid.density_mode` the same way. When both are true,
or when one of them is given together with `grid.density_mode`, they stay as
they are and the placement driver reads them. Setting either one to true with
`grid.density_mode` is a usage error. `lhd describe` redirects an old name to
its new name. `physical_repair` JSON keys follow the same names
(`grid_density`, `region_*`, `legalize_bin_*`, `satlegalize_bin_*`); its old
keys are still accepted, and giving both spellings is an error. Report JSON
keys stay unchanged until the aliases are removed.

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
3% over the original mapped standard-cell netlist. The inventory precedes
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
example `synth.opentimer=false`) the synthesis buffers stay; `true` forces the
cleanup and `false` never runs it. Input/output binding cells, fixed cells and clock/reset distribution
cones remain, including controls crossing region boundaries. Logical mapped
definitions and their resynthesis identities/history are unchanged. The cleanup
also runs on the new physical copy after an accepted color restart, but never
on repair/ECO trials. `buffer_cleanup.jsonl` records removed and retained counts.
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
capacitance become ohm/um and fF/um. `synth.place.wire_rc=R,C` overrides both
directions. RC enters OpenTimer in memory; there is no SPEF file round trip.
This is a placement estimate, with no routing, via, coupling or clock-tree model.
`synth.place.tracks=path/to/make_tracks.tcl` supplies the router's actual grid
before macro alignment. It accepts literal `make_tracks LAYER -x_offset N
-x_pitch N -y_offset N -y_pitch N` records in microns, with comments and line
continuations; it does not execute Tcl. Every record requires all four values.
An explicit file replaces imported tracks; without it, imported DEF tracks and
then LEF pitch/offset data retain their existing precedence. File contents are
included in placement-cache identity. Tracks are exported in DEF and must be
preserved by the downstream flow rather than regenerated after changing DIEAREA.
This matters when platform track offsets differ from technology LEF offsets.

`synth.place.tech_rc=path/to/setRC.tcl` reads characterized technology data when
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
a rectangular frame with hard exclusions for absent sites. Exported DEFs,
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

`synth.place.cohesion=C` enables soft, area-weighted centroid attraction for
mapped cells and SRAMs sharing a synthesis color in the same instance scope.
It defaults to zero. C is the final coefficient in inverse microns, ramped
linearly from zero during the final GP call. The IO-assignment prototype stays
unchanged. Synthesis provenance is retained through mapping, cached/bypassed
regions, and hierarchy flattening; repeated definitions get distinct physical
groups. Unowned nodes and singleton groups get no attraction. The diagnostic
`cohesion_groups.json` records group membership. No group boxes or new hard
constraints are created, and legalization stays unchanged. This initial host
wiring supports ordinary placement (`synth.place.macro=none`).

`synth.place.macro=h2d` opts into native hierarchical macro placement in full
mode. The default `none` preserves the existing flow. The host reads hierarchy
before flattening, passes physical macros and LEF symmetry to DREAMPlace, then
fixes the selected legal macro frame for standard-cell placement, cache reuse,
ECO and timing repair. The zero-wire post-synthesis timing snapshot skips H2D.
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
experiments, including runs with `synth.opentimer=false`. Synthesis-only and
GP-only commands do not enter this refinement loop by default. Full timing-driven
placement requires OpenTimer and constrained data nets (normally from
`synth.sdc`). OpenTimer
first ranks the mapped netlist with zero wire RC to seed the initial net weights.
The seed prepares native memories and tie cells without running placement or
publishing a placement cache or DEF. The worst 20% of timed data nets get weights
from 1 to 5 with 0.5 smoothing; clock and reset networks are excluded.

`synth.place.timing_rounds` allows 1–8 refinement attempts (default 3). They are
skipped when setup is already met; electrical repair remains a separate phase. Each attempt uses
the current coordinates and normalized density weight, fixed IOs, the configured
final wirelength smoothing scale, and at most 100 GP iterations. A candidate
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
cells. `synth.place.resize.gp` is retained for compatibility; electrical repair
runs after legal GP. Macro and speculative A/B callbacks also leave topology
unchanged. `synth.place.final.timing_start=200` delays final-cell ranking samples
until warm-up completes. The interval samples density, with ranking updates at
overflow 0.2 and 0.1; thresholds crossed together produce one update. Short runs
still obtain the selected-frame timing required for legalization and resynthesis.
Synthesis timing supplies the initial weights. The `physical_repair` diagnostic
accepts the same warm-up as `final_timing_start`.

The following GP-buffer controls remain available to the explicit experimental
repair helper; production GP does not invoke that helper.

`synth.place.resize.gp_margin=0.01` is a dimensionless slack dead band, a fraction
of the shortest constrained clock period. Without a clock it uses estimated
maximum path delay; without either positive scale it skips GP repair. There is
no absolute-time floor. Insertion requires slack below the negative margin or
an electrical load/slew ratio above 1.1. Removal requires slack above the positive
margin on the upstream driver, buffer output and sinks, and fresh timing must
retain the margin on the surviving upstream driver. Accepted edits must improve
existing timing/electrical/area criteria without worsening setup or electrical
violations. Edited drivers and new buffers are protected for the next two GP
repair checkpoints. Insertion retains existing chains so it cannot bypass the
removal hysteresis. Traces `gp_buffer_repair_<iteration>.json` record the relative
margin, timing scale, proposals and acceptance. The diagnostic `physical_repair`
JSON equivalents are `gp_resize` and `gp_buffer_margin`; its former
`virtual_timing` clone-and-discard mode has been removed.

`synth.place.resize.gp_trials=4` bounds the attempts in each insertion/removal
phase, including rejected-batch bisection (range 1–16). The replay spelling is
`gp_buffer_trials`. A budget of 1 tests the full candidate batch once per phase;
it preserves timing/electrical acceptance but gives up useful partial batches
when the full proposal fails. The default remains 4. Rejected trials restore
the accepted physical view and reuse its unchanged RC and pin locations.
Trial traces separate graph preparation, physical/RC refresh, timing, and
restore seconds so runtime experiments can distinguish those costs.

`synth.place.final.timing_schedule=late` is an opt-in cadence experiment; the
default `fixed` keeps the current schedule. After the timing warm-up, `late`
uses four times `macro.timing_interval` during the first half of the remaining
GP budget, twice that interval in the next quarter, and the normal interval in
the last quarter. The first eligible measurement and forced/final timing are
never skipped by this schedule; the existing movement/density gates still
apply. With warm-up 200, interval 50 and 1000 iterations, ordinary checkpoints
are 200, 400, 600, 700, 800, 850, 900, 950 and 1000. Buffer repair starts on the
second measurement as before. Replay spelling: `final_timing_schedule`.

The initial weights are copied into floorplan GP, macro GP and final cell GP.
`synth.place.timing_fraction=0.2` selects the worst 20% of constrained non-clock
nets, rounded up to at least one net. It applies to the initial zero-wire weights,
FP updates and post-placement refinement. Values must be in `(0,1]`; for example,
use `0.1` for 10% or `0.29` for 29%. A smaller fraction concentrates on the worst
nets, while a larger fraction covers more endpoints; measure WNS/TNS to choose.
For selected nets, let `s` be a net's worst
pin slack, `s_worst` the worst selected slack, and `s_cutoff` the last selected
slack. With `W = synth.place.timing_max_weight` (default 5), the target is
`1 + (W - 1) * (s_cutoff - s)/(s_cutoff - s_worst)`; tied selected slacks get
weight W. Current noncritical nets immediately receive weight 1, and obsolete
driver identities are removed. `synth.place.timing_smoothing` (default 0.5) is the
new-target share for selected nets at updates: `(1-alpha)*old + alpha*target`.
Initial zero-wire weights use the target directly. These weights multiply both
the smooth wirelength cost and its gradient before GP preconditioning. They do
not directly change the configured centroid or density coefficients, or size
cells. The native RUDY estimator also uses net weights, so critical nets count
more toward routing demand. Detailed placement also runs with timing weights: k_reorder,
independent_set_matching and global_swap score moves with weighted HPWL (net
weight times net HPWL), and a pass is kept only if weighted HPWL does not
increase and the placement stays legal.

`synth.place.satlegalize=true` enables experimental local pin-access alignment
at the selected final-cell GP checkpoint, before legalization/detail. It requires
fresh OpenTimer feedback (`synth.place.macro.timing_interval>0`). It leaves macros,
fixed cells, IOs and cells outside each window unchanged. The existing legalizer
remains the default and the zero-prefix fallback. Multi-height cells currently
remain obstacles because the SAT model covers single-height row/site placement.

Native signal routing exposes a non-mutating `Signal_router::try_route(net,
request)` API. The request supplies shared work/deadline limits and optionally
an aligned corridor and layer-comparison flag. It returns candidate geometries,
physical pin-layer identities, work counts and failure/timeout status. Surviving
candidates retain baseline-first ordering; callers own timing ranking, capacity,
logical eligibility, escape-reservation commits and atomic publication. A
proposal never fixes a net itself.

SAT legalization is one consumer of this API, not part of its contract.
Other callers may probe a net, try alternative routes, or move a cell after a
rejected trial. Technology policy is supplied separately through the routing
profile and rule database. Improving SAT legalization's placement decisions or
outperforming another router is separate from the API's correctness contract.

```cpp
Signal_router router(tech, frame, occupancy, platform_profile);
auto proposals = router.try_route(net, {.limits = limits,
                                       .corridor = retained_alignment,
                                       .compare_layers = critical});
```

Omit the corridor and set `compare_layers=false` for general bounded routing.
All returned candidates share the request's search budget; timeout/work counts
remain visible even when an earlier baseline candidate succeeded.

`try_route` and `probe_route` preserve initially legal access for supported
unrouted standard-cell pins, including CLOCK/POWER/GROUND pins in special nets,
wildcard connections, and unassigned control/supply pins. A partially wired
signal net remains protected until full-net connectivity is established.
`try_hard` never bypasses this check. Repeated routing borrows the host's
`Pin_escape_guard`; standalone requests build a temporary guard against current
occupancy within the request deadline. Final SAT alignment verification shares
one index for the completed frame and probes actual two-pin nets, avoiding
whole-frame setup inside each pair’s search budget. The host still commits the escape trial
only after all final acceptance gates. Exact escape enumeration currently covers the profile's M1-to-M2 stack.
Unresolved clock/supply pins conservatively reserve local volume across
layers, expanded by per-layer spacing and single-cut LEF via-overhang bounds.
The three-width escape extension applies only along the escape layer’s preferred
direction; unused generated PG via definitions do not enlarge this reservation. Known
rectangular supply rails reserve their footprint rather than the entire cell.
Straight-span search continues to later tracks after an access rejection. This can reject
feasible nearby routes; it does not claim exact access on unsupported stacks.
Verified same-net supply metal exempts an already attached supply pin; a LEF pin
rectangle alone does not count as a connection. Search-budget exhaustion fails
closed.

The standalone `//tools:signal_route_bench` benchmark samples driver-pin nets
from a supplied mapped, placed DEF. It resolves LEF OUTPUT pins and DEF INPUT
ports, keeps nets with exactly one driver and at least one sink, and draws
uniformly with replacement using a reproducible seed. This measures the physical
`try_route(net, request)` API after driver-to-net resolution, not graph lookup.
Macros, IOs, high-fanout nets and unsuccessful requests remain in the sample.
It does not publish routes or change occupancy. Every successful proposal gets
an independent full-net connectivity check outside the timed call.

```sh
bazel build -c opt //tools:signal_route_bench
taskset -c 14 bazel-bin/tools/signal_route_bench \
  --def placed-with-tracks.def \
  --lef technology.lef --lef cells.lef --lef macros.lef \
  --samples 2000 --seed 17 --access 10000 --patterns 10000 \
  --budget-ms 1000 --guard shared > route-results.jsonl
```

The DEF must contain the platform's actual routing tracks; a placement-only DEF
without tracks mostly measures immediate access failures. JSONL reports separate
read/occupancy/guard setup times, each request's outcome, latency and geometry,
and overall rates and success/failure latency percentiles. Rates exclude setup,
JSON emission and independent connectivity validation. Both success and failure
rates use **total request time** as their denominator, so they sum to the attempt
rate. `--guard shared` constructs one guard before measuring requests;
`--guard request` includes standalone guard construction within every deadline.
The shared mode represents an unchanged physical frame. A host that commits
routes or moves cells must maintain/rebuild the corresponding occupancy and
escape state; this benchmark does not measure that update cost.

On the 161,074-cell, 161,855-net coralnpu placement, with platform tracks added
using OpenROAD, three alternating unprofiled repeats on one
Ryzen Threadripper PRO 5955WX CPU gave the following medians. Both builds used
the same 2,000 driver draws, 10,000 access/pattern budgets and 1 s deadline.
The baseline is `fc383160c` plus the benchmark; the optimized implementation
filters unrelated access-via templates before geometry decoding and memoizes
up to 1,024 identical branch-via legality checks per request/template. Cache
keys include origin and both complete landing rectangles; no cached verdict
survives a request. Search order, budgets and legality rules remain unchanged.

| Measurement | Baseline | Optimized |
| --- | ---: | ---: |
| Attempts/s | 551.4 | 631.4 |
| Successful routes/s, mixed workload | 16.26 | 18.63 |
| Failed attempts/s, mixed workload | 535.1 | 612.8 |
| Successful-call median / p95 | 0.149 / 0.456 ms | 0.143 / 0.442 ms |
| Failed-call median / p95 | 0.086 / 12.152 ms | 0.076 / 9.409 ms |
| Guard setup | 14.525 s | 12.466 s |
| Total setup | 16.342 s | 14.284 s |

All draws, success/failure reasons, work counts and proposed geometries matched
exactly across these runs: 59 successes, 1,941 failures, no timeouts. Peak RSS
was approximately 3.78 GiB for both. Other builds were active on the machine;
observed rates ranged from 550–555 baseline and 625–641 optimized attempts/s.
The synthetic fixture has no PG wiring: 1,581 failures involve unresolved-pin
protection, 220 have no legal access, 87 fail maze search, 34 have non-cell
endpoints and 19 block remaining pin access. These are workload-throughput
measurements, not a claim of full-design routability or timing improvement.
A second independent seed (29) preserved all 2,000 outcomes and geometries
(57 successes, 1,943 failures, no timeouts), improving 781 to 856 attempts/s
in one paired run. Three optimized standalone requests with `--guard request`
all reached the 1 s guard-setup deadline before route search; including cleanup,
these achieved only 0.85 attempts/s.

Sampling the optimized request window at 199 Hz attributed about 75% of samples
to pattern search and 22% to maze search. Via legality checks appeared in 76%
and occupancy queries in 27%; these are nested inclusive costs, not additive.
Repeated spatial queries, spacing checks, string comparisons and geometry
conversion remain the principal targets. Whole-frame pin-escape construction
still dominates startup. Repeated consumers should borrow a guard with a clear
update lifecycle rather than reconstructing one for every dpin.

`try_route` also screens long parallel exposure to foreign metal on the same
layer. For elongated shapes within two candidate wire widths, it sums parallel
overlap divided by edge-to-edge separation. The dimensionless
`Signal_route_request::max_parallel_exposure` limit defaults to 100; rejected
candidates report a parallel-exposure reason. This is a conservative geometric
proxy, not extracted coupling capacitance or a timing guarantee. Occupancy can
include pin/blockage rectangles as well as wires, so the screen may reject
conservatively. Upper-layer alternatives are screened independently.
`try_hard=true` bypasses only this screen; geometric legality and the caller's
capacity/timing acceptance remain mandatory. Accepted geometry reports its
`parallel_exposure`, including in try-hard mode. These are C++ request fields,
not new CLI options. The screen is non-mutating and respects the request deadline.

`probe_route(net, request)` returns a routability witness and work/timeout
status without publishing geometry or ranking candidates by timing. It honors
an explicit `compare_layers` request, so a clean upper-layer witness remains
available when the lower-layer route fails the parallel-metal screen. Set
`straight_span=true` to discover a common access track without falling back
to patterns or maze routing. SAT legalization uses this mode for via-assisted
alignment checks. A negative probe means no witness was found within the
budget, not that routing is impossible. Moving a cell and probing again is a
normal caller operation.

Committed physical geometry belongs in persistent graph attributes. Search
frontiers, trial candidates, occupancy indexes and cached checks are transient
vectors or maps keyed by graph node/pin identities; they must be rebuilt or
invalidated after geometry or topology changes. The current router still uses
the physical floorplan adapter. `inou/lefdef/route_store` provides committed
route save/load on driver-pin attributes: typed dense geometry records plus a
companion string table. It preserves points, endpoint extensions, vias and
orientations, patches, path boundaries and width rules through graph-library
save/load. Loads check DBU, net connectivity and a caller-supplied context
identity covering technology and placement. Graph reconstruction invalidates
these records. Accepted-route publication writes them on the private candidate
graph before the graph library is saved. Completed-placement cache replay checks
the saved routes against the restored DEF using the LEF-content identity and
endpoint poses; a mismatch fails closed. The physical-name index is transient and
built once at this boundary. The storage API does not perform DRC/timing acceptance,
and the DEF remains the downstream handoff for the complete frame and power grid.
Route identity uses the master and LEF contents for body dimensions, which DEF
does not carry, and treats LOCKED/FIXED status identically across DEF replay.
Duplicate physical nets resolving to one graph driver are rejected before
replacing the stored route generation.
Temporary width rules are passed to geometry/connectivity checks as read-only
overlays. Maze candidate validation reuses the component index without cloning
the floorplan; conflicting installed rule definitions still fail validation.

`Signal_route_profile` (an alias of `physical::Route_policy`) separates technology
policy from route search: pin and escape layers, access cut, branch layer/cut, and
ordered allowed signal layers. `signal_route_profile(tech_profile, tech)` copies
`Tech_profile::route` (`inou/lefdef/tech_profile.hpp`; ASAP7 uses M1 access and
M2–M7), stopping the signal layers at the first one the LEF lacks.
Explicit profiles can use different layer/cut names; LEF supplies geometry and
DRC, the floorplan supplies tracks, and `Route_technology_rc` supplies electrical
characterization. `Preroute_options::tech_profile` selects the technology profile
(`auto` detects it from the LEF); its policy is also used by the pin-escape guard. The current access/pattern algorithms
require a horizontal escape and vertical branch, with modeled single-cut vias.
This API separation is not certification of another PDK: its rules, power-grid
profile, capacity/RC configuration and independent validation are still required.

`synth.place.satlegalize.route=true`, together with `synth.place.satlegalize=true`,
enables the experimental straight-metal handoff after final placement. It uses
only retained SAT alignments whose two pins form an entire signal net, rechecks
the corridor at the final coordinates, and first proposes one same-layer segment.
SAT component identities are remapped by name after DEF sorting. Pin-layer
segments may use M1; their capacity is accounted alongside M2–M7. A coarse track
already unavailable due to static metal remains unavailable; the route reserves
any additional usable tracks it consumes. Exact geometry checks still reject
foreign-metal conflicts. This mode
selects SAT nets instead of running the general maze/repeater search, even when
`place.preroute=true` is also supplied.
`synth.place.satlegalize.route.vias=true` (default false) additionally lets the
terminal legalizer select M2 corridors from M1 pins with legal V1 endpoint
access. It requires `synth.place.satlegalize.route=true`. The candidate index
includes conservative M2 bounds; each surviving pair is checked at its proposed
poses against installed obstacles using the shared pin-access and metal oracle.
Each via predicate is bounded to 1024 access examinations, 256 patterns, and
5 ms, and reuses compiled rules. Final routing rechecks access and keeps one
horizontal trunk inside the retained corridor: it cannot switch tracks or detour
around a blocked witness. The original same-layer search supplies the baseline.

`synth.place.satlegalize.route.critical_fraction=0.10` compares metal choices for
the worst setup-slack fraction of eligible retained alignments (rounded up;
`0` disables comparison, `1` compares all). Ranking is taken before routing;
clock/reset/tie/protected and insufficient-margin nets remain excluded. For
these nets, the router tries same-direction upper trunks through M7 with the
same endpoint centers. Only local endpoint landings and adjacent single-cut
via stacks are added; no cell movement, track shift or maze detour is allowed.
Each candidate must satisfy metal/via/track rules, occupancy, connectivity,
pin-escape preservation, capacity and setup/hold/electrical checks. Actual
layer RC and via resistance are evaluated in the incremental timer, including
driver loading. Candidates are tried in decreasing candidate-net setup slack;
the original route wins ties. If a faster candidate fails another acceptance
gate, the next candidate is tried. Search shares the existing per-net budget.
Unsupported rules/characterization are not approximated. This model includes
effective ground capacitance, not extracted coupling or via capacitance.
Reports include `critical_layer_search`, `layer_candidates`, selected per-layer
wire lengths and `layer_selection` time. The `route.vias` switch controls SAT
pin-access alignment generation; critical layer comparison can add endpoint
stacks to an already valid same-layer alignment independently of that switch.

With the default terminal legalizer, routing consumes that legalizer's final
validated critical/local SAT alignments; it does not rerun the legacy local
legalization pass. In routing mode, candidate corridors use the signal-metal
rule oracle during SAT selection, including spacing to installed foreign pins
and obstructions. Accepted corridors also constrain subsequent cell placement.
Supply-pin metal is an obstacle even without LEF OBS records, including
abutment rails extending outside the cell body. Final routing still revalidates
against the complete exported floorplan and generated power grid. Terminal placement controls (`place.legalize.*`) continue
to govern their selection and placement. The old local `satlegalize.*` controls
remain specific to the non-terminal placement flow.

The routing stage requires native PG and taps (`synth.place.pg=auto` and
`synth.place.handoff.taps=true`), final timing/RC and final cell placement. The
usual `place.preroute.*` timing margins and budgets apply. Clock/reset/tie nets,
unsupported rules, foreign-metal conflicts, inadequate capacity or timing margins
reject the proposal; a SAT alignment alone is not a routing certificate. Accepted
nets and endpoints become FIXED with updated RC, using the normal pre-route
OpenROAD protection handoff. `preroute.json` reports `satlegalize_route`,
`satlegalize_selected_nets`, and per-net acceptance/rejection reasons. Both new
routing behavior and SAT legalization remain disabled by default.

`synth.place.satlegalize.backend=cadical|cms-cnf|cms-xor` selects incremental
CaDiCaL CNF, CryptoMiniSat CNF, or CryptoMiniSat native XOR. Candidate encoding
uses at-most-one clauses plus an odd-parity exactly-one constraint; native XOR
replaces that parity's CNF expansion. `encoding=binary` instead allocates bounded
binary **x/y DBU coordinates and orientation**, with a selector table admitting
only enumerated legal positions. This is a bounded legal-position domain, not
an unrestricted arithmetic placer. Pin offsets, displacement, overlap and access
compatibility still require geometric tables; parity alone is not coordinate
equality. The backend and encoding default to `cadical` and `candidate` only
inside this opt-in feature; no measured backend winner is assumed.

Optional `synth.place.satlegalize.max_pairs_per_window=N` caps alignment
trials in each region so one hard bin cannot consume the entire global
`max_pairs` budget. Its default `0` retains the global-only budget.
`check_packing=true` first probes the complete candidate menu with legal-site
and non-overlap constraints, using the same per-solve limits. An `unsat` probe
means this menu cannot pack; `unknown` means its budget expired. Both skip
that region without shrinking it or installing a partial model. A `sat`
probe still requires all alignment, spill-density and timing guards. The
default is `false`. Per-window telemetry records `candidate_packing` and
alignment trials, accepts, conflicts and timeouts separately.

The route guard compares full post-legalization geometry with common pre-SAT
weights. `synth.place.satlegalize.route_peak_floor=1` experimentally allows
peak-utilization changes below estimated routing capacity; `0` (the default)
requires strict peak non-regression. Values in between set that comparison
floor explicitly. Overflow, blocked pins and unroutable nets must still not
increase, and alignment preservation, setup/hold and electrical guards remain
active. `sat_validation.json` records those acceptance gates and both timing
snapshots. Native congestion estimates remain screening evidence; completed
OpenROAD routing and extracted timing decide routed success.

When placement snapshots are enabled, `sat_proposal_positions.json` and
`sat_baseline_positions.json` preserve the two geometries evaluated by the
native guard, including a rejected proposal. They contain component/master
poses and terminal poses in DBU, not a netlist or an accepted placement. A
separate routing experiment must bind these to `before_legalization.def`,
verify topology, masters, fixed macros and IOs, and retain the native rejection
reason. The final `out.def` continues to follow the full acceptance guard.

For already legal post-repair placements, the optional `max_moved_cells=1..128`
adds a whole-region cardinality bound on cells whose position or orientation
changes. Endpoints and neighbor spills both count. The default `0` is
unbounded. This controls arbitrary movement in a feasibility-only SAT model;
it is not a displacement optimum. It retains every region member and does not
shrink a region or retry a smaller alignment prefix. A small bound can make
packing an overlapping raw GP region infeasible, so this experiment targets
post-repair seeds. Per-window proposed movement counts and Manhattan
displacements are reported separately from final retained alignment counts. Accepted proposals also distinguish
corridors already aligned at trial entry from newly aligned corridors. This
classification uses the complete geometry after prior accepted trials; it
prevents an existing track corridor from being reported as a new alignment.

The remaining `synth.place.satlegalize.*` controls are `max_cells=24`,
`max_candidates=12`, `max_windows=8`, `max_pairs=32`, `move_sites=4`,
`move_rows=1`, `conflicts=10000`, `milliseconds=50`, `skip_conflicts=true` and
`boundary_growth=0.05`. Windows follow nearby timing-ranked driver/sink pairs;
movement and separation limits derive from site/row dimensions. All window
cells receive legal candidates and non-overlap constraints, including fixed
obstacles and window bounds. Incident-net HPWL includes endpoints outside the
window; excess growth rejects a model. The model maintains one solver per
window and tests guarded proposals using replaceable assumptions. Unknown/time
limits are undecided and never count as proven infeasibility. Accepted pairs
remain relational constraints while the window is solved, rather than fixing
absolute coordinates immediately.

`synth.place.satlegalize.bin.size_um=W,H` selects fixed SAT bins in microns
(the `bin` role; reports call them fixed regions);
one number selects a square. Empty preserves the original pair-centered
windows. `spill_neighbors=true` enables noncritical overflow movement to
less-dense adjacent bins and requires `bin.size_um` or `bin.grid=route`. These are
opt-in experiments; the existing defaults are unchanged. A 40 by 40 micron
experiment uses `bin.size_um=40,40` and an explicit cell budget large enough
for the full region (`max_cells` accepts up to 65536). Region size is never
reduced to satisfy that budget.

The fixed-region path considers complete movable-cell membership, orders its
bounded pool of timing-selected regions by current density, and computes usable
capacity from actual rows with fixed obstacles and the resolved standard-cell
macro clearance removed. Density includes all current movable footprints;
overlaps can therefore produce density above one. Outside cells remain
obstacles. Noncritical cells can receive bounded escape candidates from legal
gaps in less-dense neighboring regions. Clock-connected cells, current or
retained alignment endpoints, and cells without reliable positive data-pin
slack do not receive this extra movement. `spill_slack_margin_ps=0` requires
strictly positive slack; a positive value adds an explicit margin in picoseconds.

Candidate collisions use spatial overlap constraints. Candidate truncation
reserves escape choices so that nearby occupied positions cannot consume the
entire domain. Acceptance checks all proposed transfers together: the donor
must lose occupancy, receiving regions cannot exceed their usable capacity or
the donor's previous density, and incident-net HPWL and alignment checks still
apply. Ordinary downstream legalization and fresh RC/STA remain the final
acceptance checks, including nonworsening hold WNS when available. The fixed-region
experiment accepts the complete proposal or restores the ordinary legal baseline;
it does not retry shorter alignment prefixes. A failed or undecided solve is reported without claiming
overflow was removed or silently retrying a smaller region. Per-region reports
include exact cell counts, bounds, capacity, density and accepted transfers;
the bounded region-selection scope is recorded explicitly.

`synth.place.satlegalize.bin.grid=route` selects the exact final native
RSMT-RUDY routing grid (`grid.route`) instead of physical `bin.size_um`; the two are
mutually exclusive. This opt-in requires explicit final-stage `route.model=rsmt`, routability,
a non-macro density grid (no `grid.density_mode=macro`) and fresh timing feedback. The requested
128 by 128 experiment uses `synth.place.grid.density=128`. These are **bin counts over
the core**, not track counts. Dynamic density bins retain the configured
auxiliary routing grid. Baseline runs can export the map with SAT disabled.

The host samples `dp::rudy` after the forced timing-weight refresh and passes
those same H/V/pin utilization arrays to SAT. `sat_route_grid.json` records
bounds, grid, resolved options, capacities and coordinate/topology/net-weight
bindings. Bin boundaries share one DBU-rounded edge sequence, avoiding gaps
from independently rounded widths. `route_guidance=true` orders eligible bins
by initial congestion, then current cell density; neighbor spill must have both
lower initial congestion and lower cell density, with aggregate placement
capacity still checked. `route_guidance=false` uses identical region geometry
and menus with cell-density ordering, for controlled experiments.

Map-grid mode adds bounded nearest legal row-gap candidates anywhere inside
each full source bin, including for protected critical cells; only independently
spill-eligible cells may escape to neighbors. Every activated pair requires both
original endpoint bodies and its chosen endpoint poses to fit inside the source
bin. Boundary-straddling cells still participate in regional legality. The
`cross_grid_pairs` counter reports rejected seeds during the bounded region
search, not a census of all design nets. The input routing map is immutable
during this first batch.
The host compares full post-legalization candidate and ordinary-baseline maps
using common pre-SAT weights, rejecting increased overflow, peak utilization,
blocked pins or unroutable nets alongside timing/electrical/alignment guards.
This map guard precedes final tie relocation and subsequent native repair;
final alignments are revalidated, and final quality must be measured separately.
It does not shrink regions or retry shorter prefixes. Fixed macros and IO stay
fixed. Map arrays are normalized utilization, not raw track-capacity inventories;
macro halos are placement exclusions, not additional routing blockages.

Alignment requires transformed rectangular pin shapes, wire-width clearance,
a compatible routing layer and an explicit DEF `TRACKS` grid or LEF `OFFSET` and
`PITCH`. Missing grid evidence skips an alignment. Rectangular LEF OBS geometry
is checked along the access corridor; nonrectangular/iterated OBS conservatively
block their whole cell on the affected layer. Polygon pin approximations do not
provide usable finite-width access. The model does not cover via stacks, PG
pin geometry, spacing or end-of-line rules. These are conservative access checks,
not a detailed-route or DRC guarantee. `satlegalize.json` records the backend,
encoding, trials, accepted/retained pairs, undecided/conflicting trials, total pass
runtime and fallback. Legalization/detail must preserve retained relationships;
the host revalidates them, tries bounded shorter prefixes in legacy window mode, and restores the
untouched GP state at prefix zero. Timing acceptance recomputes physical RC/STA
against the ordinary legalization of the same GP input. Later accepted sizing or
buffering edits explicitly mark these relations relaxed in final QoR rather than
claiming that pre-repair alignments remain preserved.

The model-producing solver bridge uses cvc5's pinned source dependency builds.
In the default source mode it joins cvc5's combined object **before** private
solver-symbol renaming. With `--define=cvc5_prebuilt=1`, a separately isolated
bridge reuses the pinned SAT archives without linking a second libcvc5. Solver
C++ types never cross the C ABI; repeated solver/cvc5/ABC coexistence is checked
by `//pass/place:sat_solver_test`. `satlegalize_test` covers geometry, competing
pairs, fragmented whitespace, boundary nets, unknowns and prefix-zero rollback.
Large CNN/Eyeriss/Coral sweeps remain external evaluations: access predictions
need routing inspection, and no timing improvement is claimed without measured
post-legalization/detail RC/STA and electrical checks on identical GP inputs.

The placer reads numerical setup slack for physical driver/sink pins, taking
the minimum available rise/fall slack per pin and then the minimum across each
net. It does not consume legacy arrival-margin criticality colors or the
`attrs::critical` annotation, and does not enumerate a few printed timing paths.
This ranking is relative: the selected fraction can receive extra weight even when
all constrained nets have positive slack. Synthesis/centroid color membership
is preserved by the weighting path. Centroid forces remain normalized against
the current combined wirelength/density force, so their effective scale can
change indirectly even though their option values are unchanged.

`synth.place.timing_spread_weight` opts into protecting timing-sensitive standard
cells from extra routing and local-density spreading. It is a fraction `a` in
`[0,1]`, default `0`. For each movable standard cell, take the largest incident
active net weight `w`, with a minimum of 1, and set its spreading mobility to
`m = 1 - a + a/w`. At `a=1`, a cell on a weight-5 net gets 20% of the additional
spreading response; a weight-1 cell gets the full response. The per-cell values
refresh after timing-weight updates and netlist replacements.

RUDY/RSMT inflation still deposits the full virtual area into the density field,
so neighbors continue to see the congested region. For area inflation `f`, the
receiver's density-force multiplier becomes `(1 + (f-1)*m)/f`: this retains the
physical-area contribution within the existing inflated footprint and attenuates
the extra charge contribution. Optional `local_density_weight` pressure is also
multiplied by `m`, after its ordinary calibration, so normalization does not
cancel the protection. No extra Poisson solve or STA invocation is required.
The existing timing-weighted wirelength force, macros, fixed nodes, physical
sizes and legalization remain unchanged. `gp_history.json` reports the option,
last protected-cell count and minimum mobility.

This is a force-preconditioning heuristic, not a hard displacement bound or an
exact gradient of the unchanged density energy. Its priorities inherit the
existing setup-slack ranking; they do not encode hold slack. Movement, density,
setup and hold must still be checked on the resulting legal placement and route.
When `f=1`, the global density response is unchanged, including response to
virtual fillers; protecting a pure-filler experiment needs a separate policy.

With timing-driven placement enabled, `synth.place.fp.timing_interval=50`
samples FP density every 50 completed iterations. Ranking-only FP, macro and
final-cell callbacks recompute RC and critical-net weights at overflow milestones
0.2 and 0.1. They do not run the timer on every interval, and coalesce milestones
crossed between samples. The IO prototype inherits available weights. Set the
FP interval to zero to disable its in-loop callbacks; macro/final-cell sampling
uses `synth.place.macro.timing_interval` (default 50). Required selected-frame
measurements for resynthesis and legalization remain separate. Designs with no
FP candidates have no FP pass. Explicit FP resizing and custom detailed-only
callback users retain their interval/density scheduling controls.

`synth.place.macro.detailed=false` skips detailed cell placement while macros
are movable. The default is `true`. This experiment retains macro/cell
legalization, timing feedback and the legal macro handoff; the final cell stage
still runs its configured legalization/refinement flow. The macro stage then
exports `after_legalization.def` and `out.def` without an `after_detailed.def`
snapshot. Compare the resulting macro/IO decisions, timing and routing before
adopting this speed tradeoff.
The final handoff DEF then contains only fixed/locked components, IOs and frame
geometry. Final placement rebuilds movable cells and connectivity from the
mapped source, as in the original full-frame handoff.

For macro-only legalization, additionally set
`synth.place.macro.legalize_cells=false` (default `true`). With FP disabled and
plain macro placement, this leaves movable CORE cells at their GP coordinates,
skips their row legalization, detail, row flips and tie-site repair, and retains
hard-macro snapping, halo/track checks and IO placement. Timing checkpoints
inside macro GP remain enabled. When IO resynthesis is enabled, one measurement
of the selected GP iterate is retained before macro snapping; resynthesis uses
that timing and RC snapshot without a post-legalization STA call. It can still
remap and restart the macro stage. The intermediate result does not claim full
cell legality. `gp_history.json` reports zero cell legalization/detail time;
the final stage still checks all cells. This mode currently requires
`synth.place.macro=none`, and is independent of the final weight policy below.

`synth.place.final.independent_timing=true` starts final placement with the
original synthesis timing weights instead of the macro stage's weights. Final
GP refreshes its own RC and criticalities at the density milestones above.
The existing selected-frame and final legalization/detail timing checks still run. The default is
`false`. This separates the final-cell objective from the provisional macro
cell placement while keeping synthesis criticalities active initially.
Use it together with `synth.place.macro.legalize_cells=false` for a macro stage
whose internal timing weights are discarded at the fixed-macro/IO handoff.

For explicit FP resizing or a custom detailed-only callback, set
`synth.place.fp.timing_after_density=true` to defer FP timing and repair
while the actual GP overflow exceeds `synth.place.stop_overflow` (default 0.1). This uses the
native halo-padded density model and rechecks at each timing interval; the
initial synthesis timing weights remain active. GP cannot declare convergence
before the first eligible callback, and the iteration/continuation cap always
requests one final callback, even if density never settles. The default is
false for a controlled comparison. `fp_place.json` records every density-gate
decision in `timing_density_schedule`, including overflow and forced callbacks.
`timing_interval=0` still disables all FP timing and repair.

Set `synth.place.fp.timing_movement` to a positive RMS movement threshold in
bin units (for example `0.5`) to coalesce the interval checks. The first sample
runs at the first eligible interval; later samples require area-weighted cell/macro motion
since the last timed placement, with a progress-based budget controlled by
`synth.place.fp.timing_max_updates` (default 10, at least 2). One update is reserved
for the iteration/continuation cap; the default allows nine earlier updates.
The budget applies only to movement-based scheduling; explicit repair trials
require additional STA calls within each update. Zero disables this extra movement
gate. Ranking callbacks still use density milestones, and `timing_interval=0`
disables in-loop callbacks. Motion from centroid forces counts even after density
settles.

QoR tuning controls preserve the existing numerical defaults:

| Option (`synth.place.` prefix) | Default | Meaning |
| --- | ---: | --- |
| `timing_fraction` | 0.2 | Worst-slack share of timed non-clock nets, `(0,1]` |
| `timing_max_weight` | 5 | Worst selected net's target weight, finite, `[1,100]` |
| `timing_smoothing` | 0.5 | New-target share `[0,1]`; zero holds prior weights |
| `stop_overflow` | 0.1 | GP overflow threshold `[0,1]`, also used by FP timing gating |
| `initial_density_weight` | 0.01 | Positive density-force multiplier before native gradient normalization |

The density controls apply to the IO prototype and normal FP/macro/cell GP calls.
Use `synth.place.density` separately for the requested density. Existing controls
already expose centroid attraction/repulsion, macro overlap, alignment/snap and
routability. Numerical epsilons and repair acceptance criteria remain internal.

When `synth.place.resize=true`, each FP timing update also performs equivalent
gate sizing, buffer removal, fanout-tree buffering and FLUTE Steiner repair.
It uses the same Liberty interface/truth-table checks, clock/reset protection,
fanout/capacitance/slew limits and timing/electrical acceptance as the post-place
repair. A load-ranked sizing sweep is followed by a whole-forest repair batch,
with at most eight individual topology fallback trials (at most four accepted).
Each evaluated batch/trial gets fresh RC and an incremental update of the one
persistent `Physical_session` timer. Even an empty sizing batch gets a second STA call.
Trials use the current overlapping GP coordinates; no legalization or macro locking
happens during these checkpoints.
Rejected edits restore graph, physical pins, RC and centroid membership together.
Accepted Steiner buffers inherit their owner's scoped synthesis color and
candidate membership, including uncolored hierarchy members.

FP continues in the same native solve, preserving centroid groups, repulsion
state and density history. A changed objective, geometry or topology resets
Nesterov/BB momentum. Stable node identities preserve surviving positions and
RUDY inflation; added buffers start at Steiner seeds. Resized/inserted cells
update density charge and centroid area accounting. Accepted masters, wiring and
weights carry through macro and final-cell placement. Per-update records are in
`synth/place/macro/fp_timing.jsonl`; `fp_repair_<iteration>.json` records topology
trials, and `fp_place.json` records timing/resize/repair counts. These are
approximate placement-RC timing decisions, with legal post-placement validation
still performed by the existing outer flow.

`synth.place.resize=true` enables bounded sizing and buffer optimization after
legal placement. Clock/reset distribution logic is protected from sizing, buffering and
buffer bypass. `synth.place.repair.reset=true` is rejected. Equivalent scalar logic cells must have the same ordered pin
interface, footprint and exact Liberty truth table (up to six inputs). Drive
variants may have different widths. Sizing and redundant-buffer bypass run first,
followed by a separate design-repair pass that splits fanout on FLUTE Steiner
branches and inserts repeaters along long edges. It uses Liberty input caps,
available slew/cap/fanout limits and placement wire RC to plan candidates. Resolved
physical pins preserve individual packed-bus bits through wiring aliases.

The forest search includes the direct source connection and one repeater at the
source, even when that source meets its electrical limits. A weak but legal
driver may benefit from isolating its load without a larger distribution tree.
Optimal repeater spacing generates spatial alternatives; branch selection uses
RC delay, capacitance and fanout rather than treating that spacing as a hard
wire-length limit. Cell/area budgets and fresh timing of the legalized candidate
still decide whether an edit is retained.

For larger fanouts, the search also partitions nearby sinks into local leaf
groups before building the Steiner backbone. Groups fit the candidate buffer's
capacitance and fanout limits; seeds use either the group center or the point in
its bounding box nearest the source. Every drive is then selected with the same
required-time model. A clustered alternative may not exceed the summed net HPWL
of the best original candidate. That fixed reference prevents a timing-only
choice from adding overlapping wire. These are internal candidate-generation
and acceptance rules; public option defaults are unchanged.

**Reset buffer tree (`synth.place.reset_fanout`, default 32; 2d-domplace U23).** One
mechanism for every mapper (`abc`, `usyn`, the `domino_provisional` profile): right after
the mapped hierarchy is flattened, before the first cell is read, every reset net with more
than N cell loads is dealt out through the library's smallest placeable buffer
(`reset_tree.hpp`), N pins per buffer, level by level until the driver carries at most N
loads. A reset net is a net that drives an asynchronous clear/preset pin of a Liberty
register, a domino storage reset `RST` pin (U22: the reset stays CMOS), or whose driver
carries the synthesis reset role (a synchronous reset folded into data logic); every other
load of that net (the synchronous-reset logic of the same signal) moves behind the tree too.
Pins are grouped in instance-name order, which follows the source hierarchy; graph outputs,
bus-wiring aliases, macros and constant (tie) drivers keep their direct connection. It is
pure fanout restructuring (a chain of identity buffers per pin, LEC-proven in
`lhd_placement_reset_tree_smoke`), idempotent (a timing-driven run builds it in the seed
placement), and it composes with the mapper's region trees (`pass.abc.reset_fanout`,
`pass.usyn.reset_fanout`, default 0), which bound a reset inside one region; this one bounds
the sum across regions. `synth.place.reset_fanout=0` turns it off (the pre-U23 behaviour for
the CMOS flows); 1 is refused. `qor.place.reset_tree` records `fanout`, `nets`, `sinks`,
`added`, `levels`, `skipped` (pins left alone behind a multi-bit or multi-driver bus alias)
of that placement call and `buffers`, the `__place_rsttree*` cells in the placed netlist; the
value is part of the placement cache identity. The tree is not reset repair:
`synth.place.repair.reset=true` (sizing reset cones) stays rejected, and the tree's nets
keep the pre-CTS exclusion below (the buffers are reset-network cells). The tree's buffers
are not growth for the shared cell/area growth budget above.

`synth.place.domino_rail_fanout` (default 32 since 2d-domplace U24, 0 off) applies the same tree
to nets on domino data rails under `pass.usyn.physical=domino_provisional` (the per-region
mapping never buffers a rail), with buffers named `__place_railtree*`;
`qor.place.domino_rail_tree` reports it. Without domino cells it does nothing. On HighTide sha3
(ORFS frame, arm 3) it took the stage period from 3967 to 2226 ps and max-cap/max-transition
violations from 51/78 to 0/0 for 170 buffers (16 loads: 2210 ps, 444 buffers; 64: 2661 ps,
69/62); lfsr has no rail above 32 loads, so it is unchanged.

Clock/reset nets are excluded from GP wirelength, RUDY demand, connectivity-driven
movement, IO attraction and legalization criticality/alignment. Synthesis preserves
clock/reset roles on LGraph drivers through mapping, hierarchy flattening and cached
artifacts, including registered synchronous resets folded into data muxes. Placement
resolves these roles to physical pins. Liberty sequential and memory control pins,
LEF CLOCK pins, and timing clock flags supply additional automatic role evidence.
The exclusion propagates in both directions through characterized buffers/inverters
and the clock path of ICGs, including sibling distribution branches. It stops at
functional logic; an ICG enable remains data. No net or cell is deleted from DEF/STA.
A nonempty `synth.place.control_ports=CLK,RESETN` replaces optional automatic
roots with the named ports; recognized resets remain excluded regardless of this
override or SDC exceptions. The default empty value uses automatic detection.
There is no substring-based net-name inference. QoR `prects_controls`
reports the resolved metadata roots, override status and excluded-net count.
The native placement timing session also cuts non-clock control-net timing arcs
before propagation; this prevents registered-reset arrivals from contaminating
downstream data slack. It preserves clock launch/capture paths and physical loads.
Standalone STA still reports the original SDC paths.
The saved-DEF terminal replay accepts `liberty` and `control_ports`; a DEF alone
does not carry LGraph signal-role metadata, and without Liberty, upstream buffer
semantics cannot be recovered from LEF alone.


New or resized cells first search row/site-aligned whitespace while all unchanged
cells remain fixed. Obstacles are indexed as sorted intervals per row, including
all rows intersected by macro halos. Each inserted cell updates those intervals;
rows are visited by vertical displacement and pruned only when that lower bound
exceeds the best full displacement. The minimum-displacement objective and original
row/site tie breaks remain unchanged and are checked against exhaustive sites. If this
fails, expanding local ECO placement is the fallback;
every candidate receives a whole-core legality check and fresh RC/STA. QoR marks
the fast path as `eco.single_cell=true`. Fixed cells and clock/reset networks are
protected and special power-domain cells are excluded. Each pass first proposes
a design-wide sweep: one load-ranked size change per
stressed driver, then non-overlapping buffer forests in the design-repair pass.
Equal-area stronger variants are eligible. Drive scoring includes the effect of
changed input capacitance on upstream cell resistance and the physical RC-tree
path; a faster local cell can otherwise slow the whole path. Batches share physical/name indices
and receive fresh legalization/RC/STA. Rejected sizing batches use per-pin electrical
and setup feedback to exclude changes on newly stressed pins and their immediate
driver/load neighbors, then retry the remaining edits. This is a proposal filter;
the full delay, WNS, TNS, hold and electrical acceptance checks still apply.
An improvement in negative setup WNS or TNS counts as progress even when the
longest absolute arrival is unchanged: endpoint requirements can differ. Extra
positive setup slack alone does not justify an edit once timing is met.
When feedback cannot isolate a useful subset, retries bisect the batch and visit
disjoint subsets before descending further. Forest batches keep this bisection
fallback. The budget is
24 batch evaluations (at most 12 for each sweep when combined), rather than 24
master alternatives on the first few cells. Ineligible trees are reported without
consuming timing evaluations. Physical repair removes and rebuilds buffers jointly in the forest sweep.
Aggregate-only diagnostic callers retain the bounded individual fallback; it
interleaves alternatives across drivers and stops at eight accepted edits. `synth/place/resize.json` and
`synth/place/repair.json` record their trials.

Placement RC uses driver-aware Prim–Dijkstra Steiner trees (alpha 0.3) up to
1000 distinct locations of a net; larger nets are
FLUTE trees over spatial groups of at most 1000 locations joined by a FLUTE tree over
each group's location nearest the driver (it was a star from the driver).
Steiner repair uses FLUTE for at most 1000 distinct locations and a deterministic
spatial median tree above that degree. The old 4096-leaf/256-buffer exclusions are
removed; a one-million-buffer resource guard prevents runaway allocation. Library
drive alternatives and several fanout partitions generate topology proposals;
fanout four is an alternative, not a hard limit on every buffer. The actual
Liberty cap/fanout limits constrain each selected buffer. Bottom-up selection uses sink
required times, input capacitance, intrinsic/RC delay, output limits and upstream
loading. Drives can differ along a tree. Root-drive limits are checked as well. Physical
roots include primary-input bits and fixed macro outputs; fixed root cells are
retained, while their downstream trees can change. Packed source and sink bits
are preserved independently. A port tree inherits a scoped color/candidate only
when its related logic has one common scoped color.
This is a bounded constructive heuristic, not OpenROAD's full Pareto rebuffering
algorithm. The estimates propose edits; fresh OpenTimer timing after legalization
controls acceptance and does not guarantee timing or electrical closure.

SDC clocks stay ideal before CTS unless `set_propagated_clock` requests propagation.
The OpenTimer adapter traces source-to-clock-sink paths, preserves inversion and
clock-to-Q timing, and retains all data-net placement RC. Ideal clock pins do not
contribute data-net criticality or electrical repair violations. All evaluations of
one flow share one persistent incremental timer (`Physical_session`).

`synth.place.resynth=true` enables up to four critical-cone trials through the
optional ABC provider. A cut walks backward through scalar combinational cells,
stopping at state, memories, aliases, protected networks or fixed cells. Cuts are
at most 32 cells/depth 16, 12 independent inputs and eight outputs; the cell limit
shrinks when necessary to fit the input/output limits. Every fanout leaving the
cut becomes an output, including reconvergent side outputs. The shared region
backend gets actual cut-input arrival/slew in ps and output required time/load
in ps/fF. Existing wire capacitance is retained conservatively in the output
load; old selected sink pin capacitances are removed because ABC accounts for
the replacement gates. Fresh placement RC, rather than the mapper's internal
wire model, decides acceptance.

Before splicing, a complete exhaustive proof compares all outputs for all cut
input assignments against the actual Liberty functions. Unsupported cells,
incorrect mappings, missing boundary timing, and mapper resource refusals leave
the original graph intact. Each proved replacement gets seeded positions,
windowed placement, fresh RC and fresh STA. Reports in `synth/place/resynth.json`
include boundary constraints, proof coverage and acceptance. Negative cut-input
arrivals/slews are currently refused. No sequential optimization is performed.

All three optimization switches default to false and require full mode plus
OpenTimer; resynthesis also needs the ABC provider. When combined they run in
this order: post-synthesis zero-wire STA, weighted first placement, placed-RC
weighting refinements, gate sizing/bypass, Steiner design repair, cone
resynthesis, final STA. Each candidate is private. Acceptance requires no worse
maximum delay, setup WNS/TNS,
hold violations or normalized Liberty slew/capacitance/fanout violations, plus an
improvement in delay, area or electrical violations. Positive hold slack may
decrease while remaining nonnegative. Electrical limits absent from Liberty
cannot be checked. Rejected trials preserve the accepted netlist, RC and geometry.

```
lhd synth design.v --top top --workdir W --set synth.place=true \
  --set synth.place.timing_driven=true --set synth.place.resize=true \
  --set synth.place.resynth=true
```

### Local standard-cell spreading and adaptive density bins

`synth.place.virtual_fillers=true` enables native DREAMPlace virtual density
particles by default in FP, macro and final GP, alongside any local spreading
forces. Set it to `false` to disable them. These are movable
density charges with no pins or nets; they consume no legal sites and never
appear in the mapped graph, timing/IO callbacks, netlist or DEF. They are removed
before legalization. The supplied core stays unchanged.

The default `synth.place.virtual_filler_fraction=1` requests the full
available target-density whitespace charge. With macro-density scaling enabled,
this is `max(0, density * (core area - fixed occupancy - movable macro
area) - movable standard-cell area)`. Physical padding and explicit reservation
accounting follow the native solver. Thus the budget scales with area,
utilization, fixed occupancy and macro percentage; there is no default particle
count target. The default `virtual_filler_cell_scale=2` makes particles twice
the central-90%-mean standard-cell width and height, subject to bin and site
limits. Compared with fraction 0.25 and scale 1, this requests four times the
charge and roughly four times the particle area, keeping the particle count
roughly unchanged when those limits do not bind.
The requested fraction, available/requested charge area and nonmacro area are
recorded in `virtual_filler_budget.json`; actual counts and sizes are in GP
history. The existing optional `virtual_filler_max_count=0` resolution guard is
disabled by default. Explicit macro-sized particles retain their separate sizing
policy and the same relative charge fraction.

`synth.place.virtual_filler_fraction` accepts positive multipliers up to 2;
values above 1 intentionally overfill the calculated whitespace budget.
`virtual_filler_cell_scale` changes particle dimensions while preserving the
requested total area/charge budget (up to rounding to whole particles).
GP history reports count, particle sizes, nominal area and effective charge
area alongside density overflow.

`synth.place.virtual_filler_fraction=1` supplies the full target-density
whitespace charge; a smaller positive fraction is useful for tuning.
The default `virtual_filler_cell_scale=2` scales the central-90%-mean
standard-cell width and height by two; use 1 for unscaled dimensions.
`virtual_filler_bin_fraction=0.25` bounds each particle dimension by a quarter
of the corresponding density-bin pitch, subject to site/row granularity. Count is derived from this footprint and the area budget.

`virtual_filler_max_count=0` disables the optional count guard. An explicit
positive guard (up to 100000000) enlarges particle footprints when necessary to
preserve the area budget. Reports expose actual count, footprint dimensions,
charge area, and `virtual_filler_count_limited` so this coarsening is visible.
Routability inflation adjusts the charge budget as well. The existing
soft-density behavior is unchanged when fillers are disabled.

For final GP with fixed macros and no routability inflation, the budget is
`fraction * max(0, density * available_area - movable_area)`, using the density
model's area accounting. The particles move under density forces, without a
wirelength objective. They do not reserve specific gaps between real cells or
guarantee uniform real-cell density after removal.

The native flow has two independent opt-in spreading controls:

- `synth.place.local_density_weight=0` disables the local field by default.
  Positive values add a screened Poisson field for real standard-cell area,
  with cloud-in-cell deposition/gather. Macros and zero-area IOs do not supply
  this field; macro density, halos and the existing overlap forces remain
  separate. The force repels separated cells as well as overlapping cells and
  decays over `synth.place.local_density_range=4` local-bin pitches.
  `synth.place.grid.local=512` chooses its grid. Strength is relative
  to the ordinary GP force, calibrated against target density and local-bin
  pitch rather than the current field magnitude. Only local area above the
  soft target emits charge; its distance-decaying field can push neighboring
  cells even without physical overlap. Macro/halo area reduces local capacity. `local_density_jitter=0.01` supplies deterministic
  dimension-relative cell offsets to break exact stacks; zero disables jitter.
  Local spreading uses the native solver and can run alongside virtual density
  particles. It does not support external `GpState` continuation yet; in-process
  timing checkpoints work.
- `synth.place.grid.density_mode=fixed` (the default) keeps the configured density
  grid. With `grid.density_mode=dynamic`, the ideal bin area is average movable-instance area divided by
  target density, following OpenROAD's rule. Power-of-two counts account for
  core aspect ratio and are capped by `synth.place.grid.density_max=512` per axis
  (32..1024, power of two). This differs from `grid.density_mode=macro`, which
  uses macro pitch and is limited to 32..64. Unless `grid.route` is set, the routing grid
  keeps the configured `grid.density` count for routing feedback, macro channel pressure,
  legalization and detailed placement, isolating the density-resolution experiment.

Adaptive density grids and the local field use the native radix-2 FFT-based
DCT Poisson path, with no external numerical package. The existing fixed-grid
solver remains the default. Neither option inserts physical or virtual cells.
The fast path caches the dimension-dependent DCT phases, normalization and
bit-reversal permutation per thread. It preserves the transform arithmetic and
keeps no density data between solves; the cache is bounded by the supported
power-of-two grid sizes.
Transforms with at least 16,384 bins use the existing placement thread budget
across independent output rows or columns. Bounded C++ workers keep this
available without an OpenMP build setting. Both fast and dense transforms
preserve each output's arithmetic order; small grids and one-thread dense
solves retain their original loops. Screened local fields use the same thread
budget, and serial/parallel fields are checked bit for bit.

The finite FFT butterfly products are expressed as real arithmetic to avoid
generic complex-multiplication recovery overhead. In an isolated screened
512² solve benchmark, three interleaved trials on one pinned CPU improve the
median from 5.189 to 4.303 seconds (1.206×); four-thread wall times are noisier,
with a measured 1.085× median improvement and about 18% fewer instructions.
These are solver measurements, not whole-placement speedups. Old/new fields
are byte-identical on five grid/thread cases and four 1024² stress patterns,
including sparse, DC and large finite inputs. Equivalence is scoped to finite,
nonoverflowing placement arithmetic; arbitrary complex NaN/Inf recovery is not
claimed. Profiles and measurement records are in
`tmp/remote_sync_20261021/poisson_perf/`.

`synth.place.local_density_target=-1` inherits the global placement density
for the extra cell-to-cell spreading field. An explicit value in `[0,1]`
sets an independent local overpopulation threshold and its physical force
reference; it does not change the global density target or the raw cell sources
in the separate cell-to-memory field. For example, target `0.05` and weight
`0.75` spread cells above 5% local density while global GP can still target
`0.5`. Range stays in local-grid bin pitches. Reports record the resolved
local target and configured weight, and active target changes invalidate
placement caches. The default retains the previous behavior.

`synth.place.macro.cell_density_weight=0` independently controls long-range
standard-cell pressure on movable macros. Positive values enable it during
macro GP only. FP, the automatic IO preview, and final cell placement disable
this field; final cell placement keeps macros fixed.
The source is real standard-cell area, including below-target density, so a
macro can prefer a quieter neighborhood before cells exceed the soft target.
`synth.place.macro.cell_density_reach=2` sets its screening length to twice
the longest physical SRAM side, excluding halo padding. This is a gradual
decay length, not a hard distance cutoff. The grid uses `grid.local`;
`local_density_range` continues to control cell-to-cell spreading separately.
Weak contributions from all real cells add linearly, without a minimum
cell-area requirement. The coefficient is calibrated once against ordinary
macro force per netlist; it is not renormalized down as the field accumulates.
A physical reference floor prevents sparse numerical noise from being
amplified during calibration. Equal pressure on opposite sides can still
cancel, as expected. The field is
integrated over each halo-padded macro footprint rather than sampled only at
its center. Macros never emit this local field: it adds no macro-to-macro
distance repulsion. Existing global density, RUDY pressure and overlap/halo
handling remain separate. It imposes no minimum gap. Phase reports include
the applied force and time spent computing it.

`synth.place.macro.density_pull_weight=0` adds an independent weak slide toward
less crowded neighboring space when positive (range `0..1`). It queries average
standard-cell density in strips beside each halo-padded macro using a summed-area
table. `synth.place.macro.density_pull_range=4` sets strip reach in local-grid bin
pitches (positive, at most 32). Equal surrounding densities cancel; clipped
strips are normalized by their actual area, and wholly off-core strips cannot
attract a macro. Macros never contribute density to this local term.

The weight limits force relative to ordinary macro WL/density force. Where a
distance-based centroid anchor exists, it also caps the pull to that fraction of
the spring's reference force at one circle radius. It is not capped against the
instantaneous spring force, which vanishes exactly at the centroid. Only macro
GP uses the pull; FP, the automatic IO preview, and final cell GP disable it.
Cell spreading, cell pressure on macros, and this pull share a single
cell-density raster per GP iteration. The added prefix-sum queries require no additional Poisson solve.
Reports record grid time and pull time/force separately.
The local field is an additional soft force, not a hard utilization limit or
minimum-gap guarantee. Tune force strength against HPWL, timing, local density
and runtime rather than interpreting legality as routability.

With `synth.place.snapshots=true`, each full GP call emits
`before_legalization.def`, `after_legalization.def`, and `after_detailed.def`
next to its `gp.json`. These are diagnostic copies of that stage's cell masters,
positions, IOs and connectivity, independent of later resizing/netlist edits.
The pre-legalization DEF can overlap or be off-site and is for inspection.
The final `out.def` also contains accepted later sizing/buffer-repair edits.
`gp_history.json` records actual density bins, local-field time and peak force.

## Path-based resize engine (`synth.place.resize.engine=new`)

`synth.place.resize.engine` selects the implementation behind `synth.place.resize=true`,
both at FP timing checkpoints and after legal placement. `legacy` is the batch
sizing/forest sweep described above; `new` (`resize_engine.{hpp,cpp}`) plans on the
persistent incremental timer instead of a fresh evaluation per batch.

The final post-placement resize/repair pass is skipped when
`synth.place.legalize.terminal=true`: terminal legalization is the last placement
operation. Setting `resize=true` alone does not enable that final pass in this
flow. Earlier GP timing checkpoints can still use resizing when configured.
Native handoff tap insertion requires terminal legalization, so a diagnostic
run of the non-terminal resize flow must also disable `handoff.taps`.

| Option (`synth.place.` prefix) | Default | Meaning |
| --- | --- | --- |
| `resize.engine` | `legacy` | `new` or `legacy` |
| `resize.vt_swap` | false | `new`: allow same-footprint/function swaps across Liberty files (multi-VT); otherwise a cell keeps its file |
| `resize.dont_use` | empty | Comma-separated globs never chosen as a new master or repeater by either engine. Existing matching instances remain characterized and may be retained or removed; Liberty `dont_use` cells stay excluded. |

*What-if timing.* `Physical_session::whatif()` (`core/timing_whatif.hpp`) applies
transient edits to the evaluated OpenTimer state: a master swap (`repower_gate`), a new
cell, and `set_net`, which moves pins onto a net and attaches its wire RC. Cutting a wire
for a buffer is two `set_net` calls whose driver-aware Steiner RC (`steiner_rc`, the same
construction placement RC uses) is rebuilt for both pieces from the seeded buffer
position. Queries (WNS, TNS per endpoint, pin slack/arrival/slew, net load, Liberty arc
delays, electrical checks) run an incremental update; `rollback(mark)` undoes edits LIFO
and the next full `evaluate()` discards anything pending.

*Plan.* One plan per round, each move a transaction (timer, netlist model and edit list
roll back together):

1. Electrical: only nets with a real Liberty max slew/capacitance/fanout violation in the
   full evaluation. The smallest stronger same-function driver that meets the limits,
   else a fanout tree. Trees use variable branching: per level 2..8 spatial groups
   (recursive median bisection) chosen from the load and fanout a mid-strength repeater
   may carry, more groups when the net fails timing, the most critical sinks kept on the
   root, a deeper level only where a group still exceeds a repeater's limits. Each repeater
   is seeded on the edge from its driver toward its group. Many nets are probed in
   bisected batches.
2. Timing (only while setup WNS < 0): failing endpoints, worst first. The path is traced
   back by pin slack, through multi-output combinational cells. Candidate moves per stage:
   up-size the driver (NLDM gain including the upstream load change), down-size
   slack-rich gates loading the on-path net (shed), bypass a run of non-inverting
   buffers (synthesis fanout chains: the on-path sink is re-driven from an earlier net of
   the chain, which carries the same value; no cell, and the skipped buffer keeps a load),
   isolate non-critical sinks behind a
   buffer, or cut the on-path wire with one repeater (30/50/70%) or an evenly spaced chain
   (count from the optimal repeater spacing). Moves are ranked by estimated gain and probed;
   at most five moves whose estimate could pass are probed per step; one is kept only if the endpoint improves (by 0.05 ps, or per added cell
   max(1 ps, 0.5% of its slack)) and setup WNS, TNS and the electrical checks of the
   touched pins are not worse.
3. Area recovery: down-size gates whose slack exceeds the slack of the worst met endpoint
   (zero when every endpoint fails; with a few failing endpoints it still protects the real
   critical paths) plus
   max(25 ps, 40% of the shortest clock period) by at least twice the estimated delay
   increase. Gates with slack above a whole period (a check the timer does not model, such
   as a half-cycle negative-edge capture) are never down-sized. Probed in bisected batches.

*Stopping.* No cell or edit budget. An endpoint stops when it meets timing or no move is
accepted; a path that fails even with ideal (zero-RC) wires and the fastest same-function
drive at every stage, each loaded only by its on-path pin, is provably not fixable by
buffering and gets sizing moves only. With many failing endpoints, the best estimated
sizing/repeater move of each of the worst quarter of failing paths (disjoint nets) is probed
as one batch, bisected when rejected (a move that fails alone is not retried); batches stop
after two rounds gaining less than 0.2% TNS and 0.5 ps WNS. Per-endpoint refinement then
stops when every failing endpoint is met, stuck or infeasible, or after two rounds of 32
endpoints that gain less than 2% TNS and 1 ps WNS. At FP checkpoints the per-endpoint pass is
limited to two rounds and uses no wire buffers (isolation, repeaters, repeater chains):
coordinates still move, and buffers placed for GP wire lengths piled up checkpoint after
checkpoint (eyeriss: +12% cells in three checkpoints); sizing and fanout trees remain.
After legal placement a new plan is made on the legalized result only while setup still
fails and the previous round gained at least 5 ps WNS or 2% TNS (at most four rounds).

*Commit.* A plan is materialized once on a clone (new cells keep the owner's color,
region and block), ECO-legalized (`legalize_cells`) or, at FP checkpoints, refreshed at GP
coordinates, and evaluated in full. It is kept only if setup WNS and TNS and the
electrical violations are not worse and something improved (timing, electrical or area);
hold is reported but not gated (pre-CTS). Otherwise the earliest half of its moves
(electrical first, then the most critical) is tried, a few times. `resize.json` (and the
`resize` member of the placement QoR, `fp_repair_<iteration>.json` at checkpoints) records
the phases, probes, rejections, stop reason and every commit trial.

*Timer fidelity this relies on.* Setup checks use the period of the ideal clock that
reaches their clock pin (multi-clock designs used the first clock's period),
`create_clock -waveform` is honored, and data paths through async preset/clear arcs are not
timed (OpenSTA's default).

The weighting follows [OpenROAD GPL's slack weighting](https://openroad.readthedocs.io/en/latest/main/src/gpl/README.html),
and buffer repair follows its [removal, rebuffering and timing-check sequence](https://openroad.readthedocs.io/en/latest/main/src/rsz/README.html).
No stock-DREAMPlace QoR parity or large-design scalability has been established
by the small fixtures. Routed-RC feedback and cross-process optimizer-state reuse remain follow-ups.
Final-GP buffer checkpoints use estimated placement RC.

The host pins DREAMPlace's shipped BUILD files and uses a plain LEMON archive
with the registry compatibility patches, avoiding root-only Bzlmod overrides.
The pinned DREAMPlace source includes the row-overlap fix for zero-area IO
points on core edges and the native legalization hooks. Both independent and
upstream checks remain enabled. Fixes to DREAMPlace belong in `../DREAMPlace`,
with LiveHD consuming a published commit rather than a local dependency patch.
FLUTE's lookup tables are runfiles of the standalone binary and test targets.

### Opt-in repulsion and routability experiments

The native solver keeps its existing defaults. These `synth.place.*` options
are independent controls for staged experiments (plain placement):

| Option | Default | Behavior |
|---|---|---|
| `grid.density_mode=macro` | `fixed` | Override `grid.density` with power-of-two grids, 32–64 per axis, targeting the smallest physical macro dimension; without macros use four average cell pitches. Automatic refinement is capped at twice the original 32-bin grid per axis. Resolve before halo padding. |
| `exact_macro_density` | `false` | Remove the sqrt(2)-bin density smoothing floor for non-CORE macros; preserve physical footprint plus halo. |
| `scale_macro_density` | `false` | Charge movable macro occupancy at target density, including in the overflow metric, and compensate filler area. Prevent a resolved, isolated macro from falsely violating a sub-unity target. |
| `macro_overlap` | `0` | Squared macro intersection-area penalty on occupancy envelopes, normalized by each pair's minimum area to the power 1.5 for coordinate-unit invariance. |
| `macro_jitter` | `0` | Seeded initial macro displacement as a fraction of width/height (0–0.1), projected into legal position bounds. |
| `adaptive_density` | `false` | Every 20 iterations grow density weight 1.4× if overflow improves less than 5%, otherwise 1.05×. HPWL growth above 20% limits that window to 1.05×. |
| `routability` | `false` | Enable native late RUDY inflation below 0.3 overflow, accumulated monotonically with 5% movable-area growth per round and a 3× per-cell ceiling. |
| `schedule_iterations` | `0` | Annealing horizon; zero uses `iterations`. Allows a longer solve with the original annealing horizon. |
| `io_iterations` | `0` | Automatic IO-prototype budget; zero uses `iterations`. Experiments pin 500 so changing final GP budgets preserves the initial frame and IO locations. |

Automatic IO-prototype GP retains the legacy grid and objective when the new
controls are used. The new controls apply to whole-design final GP; local
timing-ECO placement fast paths remain unchanged. Snapshot runs write
`gp_history.jsonl` while GP is running, so final legalization failures retain
history. Successful runs also write `gp_history.json`, including the resolved
grid and time spent inside the **direct DCT-loop** Poisson solver. History
records both the selected overflow metric and the old unscaled macro metric,
HPWL, density weight, halo-overlap pairs, near-coincident macro-center pairs,
RUDY overflow and inflation ratio. A final GP iteration-cap warning does not
mean final physical legalization failed.

Routability derives tracks per DBU from LEF layer pitches. LEF OBS layers
conservatively block that layer over the complete macro footprint plus halo;
it does not claim rectangle-exact obstruction capacity. Macro pin-access demand
uses physical signal-pin locations and available tracks crossing bin edges.
The effective density target is reduced with accumulated inflation, and
macro/fixed charge and the remaining filler-charge budget are rebalanced
consistently. A uniform target shift alone would be discarded by the Poisson
solve. This is
an approximate placement model, not a router. OpenROAD remains a separate
reference/downstream routing tool; no OpenROAD placement code is called by
these controls. The plain path has no CAMPL channel-sizing stage.

The 2026-10-16 Eyeriss grid follow-up retains the original `bins=32` and
`auto_bins=false` defaults. The capped automatic option resolves 64 bins for
this design while preserving the 32-bin IO prototype; setting `bins=64`
directly also changes that prototype. Use the automatic option when comparing
only the final GP grid. Historical 256-bin measurements are retained as
evidence, but the automatic planner no longer chooses that resolution.

`MODULE.bazel` pins the published DREAMPlace implementation. Build with
`bazel build -c opt //lhd:lhd`; the native library needs
no Torch or Python. The focused integration check is
`bazel test -c opt @dreamplace//dreamplace/native/... //pass/place:all
//inou/lefdef:physical_test //pass/partition:flatten_test
//lhd/tests:lhd_placement_macro_smoke`. Eyeriss inputs remain external; see
the [grid comparison](../../repros/placement-results/eyeriss-20261016/comparison.html)
for per-run settings, timing and pre/post legalization PNGs.

### Same-type alignment during macro GP

`synth.place.macro.align_global=true` makes GP alignment classes depend on the
macro master alone, across synthesis colors and hierarchy. It is enabled by default with
`synth.place.macro.align_weight=25`. Set the weight to zero to disable attraction;
`align_global=false` selects the older per-color pairing. Pair candidates
are rebuilt each iteration using sparse X/Y strips sized from the smallest
physical macro. Neighboring strips are searched too. Only pairs whose physical
projections overlap on exactly one axis attract on that axis; fully overlapping
macros remain the responsibility of overlap repulsion. Halos do not widen this
attraction range. The grid indexes neighbors; it does not fix target coordinates.

The existing `align_sigma`, `align_start`, `align_full`, and `align_nearest`
controls set the bounded-well force, ramp, and partner selection. Defaults are
weight 25, sigma 0.25, start 100, full 500. The force is scaled by
current macro wirelength/density forces and normalized by pair degree. It acts
only in macro GP in the staged LiveHD flow: the optional FP stage and final
fixed-macro cell stage do not apply it. RUDY and overlap repulsion remain active.
End-of-GP macro snap still uses the original hierarchy/size groups.

`alignment.json` reports `global_same_master` and `gp_alignment_seconds`;
GP history records `align_pairs`, `align_residual`, `align_ramp`, and the peak
applied L1 `align_force` in each sampling window. Alignment residual alone does
not establish QoR; compare final WNS/TNS, HPWL, RUDY, and legalization displacement.

### Global rigid macro magnetization (opt-in)

`synth.place.macro.magnet=true` enables opportunistic merges during the staged
native macro solve. Selection is global: equal padded shapes may pair across
masters, synthesis colors and hierarchy. Shape keys come from stored dimensions,
so fractional GP origins cannot split a shape class through coordinate rounding.
The default remains `false`; real-design comparisons are still in progress.
With magnet enabled, `synth.place.fp=false` and `synth.place.final=false`
run macro_plan only, including macro snap. Set `macro.legalize_cells=false`
and `macro.detailed=false` for a preview without cell legalization/detail.
Periodic timing feedback remains active during macro GP; the CLI skips the
initial timing seed, post-placement repair and final independent STA in this
mode. No macro timing weights are carried into a subsequent final placement.

A selected batch replaces pairs with actual union nodes. Pins are re-offset,
internal nets are masked for placement, and groups of groups flatten their
member offsets. The union retains two ordinary halos between member bodies and
one halo outside. Density, routing feedback, pulls, alignment, snap and macro LP
see that single rectangle. Members keep their original orientations; a group
cannot flip. Momentum and temporary particles restart at current positions,
with the original filler geometry retained across contractions.

Routing-model decisions, accepted routing probes, timing observations and the
original density-continuation deadline survive group restarts within macro_plan.
HPWL/inflation convergence samples restart because their geometry changed.
A deferred timing callback preserves the last measured violation; only a new
estimate can clear it. The final frozen-macro relaxation adds no STA callbacks
and retains these diagnostics. A subsequent placement stage starts with fresh
timing state.

| Control suffix under `synth.place.macro.` | Default | Meaning |
| --- | --- | --- |
| `magnet_settle_window` | `10` | Movement observation window |
| `magnet_start_displacement` | `0.10` | One-shot global maximum macro motion threshold |
| `magnet_before_snap` | `false` | Run ordinary GP, then merge only immediately before macro snap; no GP restart |
| `magnet_settle_displacement` | `0.05` | Each candidate macro center excursion / its own smaller extent |
| `magnet_max_gap` | `1` | Gap limit / shared macro extent |
| `magnet_density_fraction` | `0.25` | Gap cell-density limit / global cell density |
| `magnet_max_per_round` | `0` | Independent merges per round; zero is unbounded |
| `magnet_max_rounds` | `0` | Successful rounds; zero is unbounded |

Timing checkpoints, applied pull replacements, terminal moves, merges and snap
reset the settling gate. A complete motion window is required after an event. Each candidate macro must
individually satisfy the 5% motion limit over that window. An unrelated moving
macro cannot block the pair after the global gate opens. The global maximum/RMS use each macro's own smaller
dimension. Search starts only after a complete movement window has global maximum
motion at most 10%, then continues using the 5% per-object checks even if global
motion rises. There is no density/overflow prerequisite for magnetization.
Opening this one-shot latch preserves motion history. Reports record every
iteration's global maximum/RMS, window validity and the crossing; animation pull events
identify actually applied warpspace anchor updates. There is no additional
cooldown or future-event lookahead. Candidates sort
by gap density, displacement and stable node order. Only mutually independent
pairs enter a batch. GP-only and H2D modes reject the option.

For the late-only comparison, `magnet_before_snap=true` leaves GP forces and
stopping rules unchanged, then applies geometry/density/track checks to the
frozen final geometry before snap. It does not require motion windows or rerun
GP after merging. `synth.place.final=false` also supports a no-magnet macro-only
preview: it skips the timing seed, post-placement repair and final STA, retains
internal macro timing, and records macro/density playback when snapshots are on.

For offset candidates, the gap region joins corresponding endpoints of the two
facing padded edges. Equal-shape macros form a parallelogram; aligned edges form
a rectangle. Density counts cell footprint area inside this region, excluding
the unused corners of its bounding box. Small searches clip individual cells;
larger searches weight the auxiliary density bins by their clipped region area.
The global reference is movable standard-cell area divided by core area minus
physical macro-body union area, excluding halos. Overlapping macros are counted
once. A global density of 0.4 with ratio 0.25 gives a gap limit of 0.1, independent
of the GP target density. Any other macro intersecting the trapezoid blocks the
pair. The half-extent orthogonal offset limit remains a separate eligibility check.
The merge preserves the area-weighted center where feasible. Near a core edge,
the complete padded proposal moves by the minimum whole-block translation into
the core; a union too large to fit is rejected. Each stable GP sample rechecks candidates, including after a no-pair result:
cell motion can empty a gap without moving its macros. Macro GP uses an adaptive
momentum restart when extrapolation would move macros against the local descent
direction, retaining routing pressure while reducing channel oscillation.

Before selecting a merge, a cached routing-phase preflight intersects the
original members' supported pin profiles at their proposed rigid offsets. It
combines mixed-layer pin coverage across both axes; a single common translation
must serve every supported pin. Incompatible offsets report
`routing_track_phase` without interrupting GP. The cache keys include master,
orientation and relative member origins, independent of hierarchy and absolute
position. Pinless macros still respect the manufacturing grid.

Track correction profiles a group as one union using each original member's
supported oriented LEF pins, applies a common translation, then independently
checks the original pin profiles. A standalone abstract's existing best-effort
pin selection is preserved rather than reselected over all raw union pins.
Expansion precedes the fixed-macro handoff. Final placement receives
ordinary fixed members and the existing cell-halo behavior. Group ids are
diagnostic and do not become persistent physical constraints.

`W/synth/place/magnet.json` records attempts, candidate decisions, events,
membership and sampled macro/density frames. `macro_animation.html` plays those
frames with stepping and group outlines; it draws no individual cells. The
recorded-decision selector returns to the search frame and overlays the tested
gap region and proposed padded union, with the rejection reason and core
translation. Multiple before-snap merge batches at one GP iteration select
their own pre-merge pose; an unrecorded decision pose has no overlay. The feedback
selector jumps to timing, warpspace, merge and snap events. The status shows the
last measured macro-stability step separately from the global/local magnet gates.
Interior gaps inside group outlines reserve both member halos. The background
contains movable standard-cell footprints, excluding macros, halos
and fillers. GP history includes round/group counts, movement max/RMS and the
last instability iteration. QoR reports group/member counts, largest shape,
magnet time and whether the stage stopped settled. A hard iteration cap can
leave the stage unsettled even after legal macro snap.

The **macro stability** metric is the sum of Euclidean center movements over
all physical SRAMs between consecutive GP iterations, normalized to the largest
such sum in iterations 1–50. Lower percentages mean less motion; 100% is that
initial reference peak. Rigid-group members count separately, so magnetization
does not artificially reduce the population. The absolute sum is recorded in
`motion_history.center_step_sum_dbu`, with `center_step_valid` and
`physical_macro_count`. Merge/snap jumps are separate actions, not GP steps.
This movement metric does not certify spreading, overlap freedom or convergence.
Warpspace uses this metric for its one-shot start gate described below. The
magnetization global/local movement gates remain separate.
Export the plot, all-step CSV and standalone HTML with:

```sh
python3 tools/macro_magnet_motion.py W/synth/place/magnet.json \
  --output W/macro_stability --title 'CNN macro stability'
```

`tools/macro_magnet_gallery.py --output gallery.html --entry NAME=W/synth/place`
indexes available replays. Repeat `--entry` for multiple designs and add
`--watch-seconds 14400` to update the gallery as their macro stages finish.
The index links an animation only after the producer has finished writing it.

### Macro legalization snap algorithms

`synth.place.macro.snap=true` enables macro snapping before cell legalization.
`synth.place.macro.snap_mode` selects `alignment` (default) or `symmetry`.
Both use the same eligibility: movable macros of identical physical width and
height in the same original hierarchy scope. Synthesis-only region scopes
(`__flat___sub_<number>`, or `sub_<number>` in older netlists) are ignored. A wrapper containing a single macro inherits its nearest
enclosing scope containing multiple macros; separate branching scopes never
share snap constraints. Flat designs use the top scope. Fixed macros and standard
cells do not participate. This grouping is independent of synthesis colors.

- `alignment` keeps the existing near-line algorithm: projection-overlapping
  macro pairs form x/y groups within `macro.snap_tolerance` (default 0.25), then
  use median lattice targets and soft equality arcs.
- `symmetry` reuses the floorplan centroid fitter for horizontal/vertical lines,
  repeated neighbor gaps and shared midpoints. Equal-area circles from the
  halo-padded macro rectangles set its local scale. Fitted, lattice-rounded
  targets and relative offsets become soft macro-LP constraints. If the bounded
  fit fails, that group adds no symmetry constraints.

Both retain hard rectangular legality and halo spacing. Soft constraints may
break when they conflict with obstacles or spacing; `alignment.json` reports
these violations. `snap_classes` is the shared hierarchy/size eligibility map;
`regularity_relations` records the symmetry fitter's accepted proposals. The
older `macros[].class` field still describes the independent GP alignment color.
Neither algorithm adds FP centroids or changes GP forces. The opt-in GP
`macro.align_*` term still uses same-master/synthesis-color groups.

Symmetry controls are `macro.snap_alignment=1`, `macro.snap_spacing_tolerance=0.25`,
`macro.snap_spacing_penalty=0.02`, `macro.snap_symmetry=1` and
`macro.snap_displacement=1`. Distances use the equal-area circle radius; the
spacing tolerance is dimensionless. Both algorithms use `macro.snap_weight=100`.

### Optional synthesis-color GP alignment and legacy snap measurements


These controls apply to plain whole-design placement (`synth.place.macro=none`).
The default mode and all existing placement forces remain unchanged.

| `synth.place.macro.*` option | Default | Behavior |
|---|---|---|
| `align_global` | `true` | Same-master pairs across colors and hierarchy, refreshed each iteration. False selects the older per-color pairing. |
| `align_weight` | `25` | GP attraction relative to mean macro wirelength+density force. Zero disables. |
| `align_sigma` | `0.25` | Well width as a fraction of the smaller macro extent. Zero is quadratic; `0.25` gives the bounded well used in the experiment. |
| `align_start`, `align_full` | `100`, `500` | Quadratic ramp window in global GP iterations. An enabled alignment run cannot stop before the full-strength point. |
| `align_overlap_boost` | `0` | Multiply macro-overlap weight by `1 + boost * ramp`. |
| `align_nearest` | `false` | Restrict the experimental pair selection to nearest partners per side. |
| `snap` | `false` | Soft snapping of nearby same-class macros during macro legalization. Requires full placement. |
| `snap_tolerance` | `0.25` | Maximum initial center offset divided by padded extent for membership in a snap group. |
| `snap_weight` | `100` | Equality penalty relative to unit macro-anchor displacement. Core bounds and halo separation remain hard. |

Classes are keyed by `(master, synth_color_group)` after hierarchy flattening.
Unclassed macros have class `-1`; no color is inferred from their location or
master. Repeated hierarchy instances keep separate color classes. GP pairs
have overlapping projections on exactly one axis; both-axis overlaps are left
to repulsion. Pair lists refresh every 20 iterations. The force is equal and
opposite along the alignment axis, with degree normalization; centroids are
not pinned to fixed points.

Snapping groups nearby macros independently per class and axis. Only macro
anchors change. The native difference LP receives finite-capacity equality
arcs; hard geometry can break a group, which is reported. An explicit macro
mask admits SRAMs below the legacy five-row macro cutoff in this opt-in path.
Cell legalization and detail placement preserve the snapped macro locations.
There is no macro flipping or second cell-GP pass in this implementation.

With snapshots, `gp_history.json[l]` includes alignment pair count, residual,
ramp and final GP/macro-LG/cell-LG/DP times. `alignment.json` records scoped
class membership, proposed groups, broken equalities and displacement.
Residuals in history use the pair list frozen for that 20-iteration window;
posthoc evaluation recomputes eligible pairs from each saved physical frame.

The R4-family comparison also exposes
`synth.place.routability_charge_compensation=true` (unchanged behavior).
Setting it to `false` replays the earlier inflation-only charge model. The
new R4-family experiments use 64 bins, so their fresh baseline must not be
confused with the archived 256-bin R4 result. The R7 disabled-option run
reproduces its saved GP snapshot and final DEF byte for byte.

R4c is the separately named corrected 256-bin rerun; it already uses the
same charge accounting as R7. Grid size also changes native wirelength
smoothing and RUDY resolution. See the
[alignment/snap comparison](../../repros/placement-results/eyeriss-alignment-20261016/comparison.html)
for the twelve-case 64-bin study, pre/post legalization PNGs, phase times,
independent halo audits and external timing/congestion measurements. R7
snap-only preserves baseline GP and improves regularity/congestion with
essentially unchanged HPWL; timing gains are mixed. These observations do
not change defaults.

For a second cell-placement pass, import a DEF with SRAM components and IOs
marked FIXED through `synth.place.floorplan`. The existing native movable/fixed
partition keeps their geometry fixed during GP, cell legalization and detail
placement. LiveHD disables virtual GP fillers in all native placement calls.
Density is a soft spreading target, not a requirement to fill the core. Imported
floorplans keep their supplied area even when actual utilization differs from
the target. Generated floorplans use `synth.place.utilization` for initial area.

A benchmark that fixes its frame (ORFS `DIE_AREA` / `CORE_AREA`) gives it in microns as
`synth.place.die=xl,yl,xh,yh` and `synth.place.core=xl,yl,xh,yh` (default empty: derive
the frame from utilization and aspect). The core snaps INWARD to the site/row grid (lower
left rounds up, upper right rounds down, like OpenROAD's `initialize_floorplan`), so
`CORE_AREA 2 2 248 248` on ASAP7 is the 2.052..247.968 x 2.160..247.860 um core
OpenROAD reports; rows start at that origin, and IO pins are placed on the DIE boundary.
Giving only one of the two uses it for both. Utilization and aspect then do not apply;
the run refuses a core that lies outside the die, holds no complete row, or is smaller
than the mapped cells, and the options cannot be combined with `synth.place.floorplan`.
Physical filler insertion, if a downstream implementation tool requires it, is
outside LiveHD placement. The experimental `synth.place.filler_width` option was
removed with virtual filler insertion.

All future Eyeriss comparisons explicitly set `synth.place.grid.density=64` and
`synth.place.grid.density_mode=fixed` for every GP stage. The native default grid is unchanged;
earlier mixed-grid results are historical.

### Optional floorplan, macro and cell passes

`synth.place.fp=true` runs the R8H3-style floorplan GP from scratch, without
legalization. It discovers repeated scopes using the same full-color membership,
3K equivalent-cell threshold, four qualifying copies per immediate parent,
physical-area/density circles and adaptive
same-type repulsion. It then calls native `snap_floorplan`, freezes the resulting
world-coordinate **target points**, and warm-starts the ordinary mixed-size GP
with member attraction to those targets and no centroid repulsion. The normal
native macro/cell legalization and detail placement produce a macro-source DEF.
With `synth.place.final=true` (the default), the native LiveHD driver then fixes
SRAM/IO frames and runs the usual cell GP without candidate anchors in the same
synthesis invocation. Standard cells stay movable. Intermediate macro-source
reports and the fixed handoff DEF live under `place/macro/`; final-cell reports
stay under `place/`. `flow.json` records both enclosing phase times. Explicit ECO
frames bypass fresh floorplan initialization. `synth.place.final=false` retains
the isolated macro-source flow used in the historical evaluations.

The snap fits nearby horizontal/vertical lines, repeated neighbor distances and
shared row/column midpoints. Each relationship contains only candidates with the
same type and immediate parent candidate. Root candidates share the implicit
design scope. Children of different candidate instances never share snap lines,
gap classes or symmetry axes. Hard same-type circle separation still applies
across parent candidates. For equal-radius copies, center projection offset
strictly less than one radius permits alignment; offsets at or beyond one radius
remain eligible for staggered arrangements. A total-span guard also prevents
transitive collapse through midpoint/spacing constraints. Neighbor gaps, not all
pair distances, are clustered by penalized scalar dynamic programming. Accepted
relationships are exact in the target geometry, within numerical tolerance.

A deterministic, area-weighted least-squares projection fits coordinates jointly.
Same-family separation is enforced by conservative halfspaces along the starting
pair directions, with center/core bounds and a local movement budget. Parent target
translations are inherited before child types are fitted; parents/children do not
repel. Whole circles need not lie inside the core and are not hard cell regions.
The bounded iterative solver may reject a feasible relationship it cannot fit in
its iteration budget; a failed family is retained and reported, not called legal.
This is a local heuristic, not a global symmetry optimum.

| Option (`synth.place.` prefix) | Default | Meaning |
|---|---:|---|
| `fp` | true | Enable separate floorplan GP, snap and frozen-target refinement |
| `final` | true | Fix macro/IO frames after the FP/macro stages and re-place cells without centroid anchors |
| `final.iterations` | 0 | Final-cell GP cap; zero inherits `place.iterations` |
| `fp.snap` | true | Fit circle targets; false freezes the original floorplan GP means without calling the snap solver |
| `fp.iterations` | 1500 | Floorplan GP cap; zero uses `place.iterations` |
| `fp.minimum` | 3000 | Strict scope threshold in equivalent physical cell area |
| `fp.min_macros` | 2 | A hierarchy candidate needs strictly more than this many movable macros; 0 admits scopes with one macro |
| `fp.min_copies` | 4 | Minimum qualifying copies of one macro structure under each immediate hierarchy parent; minimum accepted value is 2 |
| `fp.attraction`, `fp.repulsion` | 0.25, 5 | First-GP attraction and initial adaptive circle penalty |
| `fp.anchor` | 0.25 | Weak member attraction to frozen targets in the next GP |
| `fp.distance_attraction` | true | Calibrate once per phase at one circle radius; retain increasing restoring force |
| `fp.attraction_outside` | 4 | Extra stiffness beyond the circle; 4 gives five times the inside slope |
| `fp.alignment`, `fp.symmetry` | 1, 1 | Alignment/midpoint thresholds in smaller-circle radii |
| `fp.spacing_tolerance` | 0.25 | Maximum observed gap range / mean within a class |
| `fp.spacing_penalty` | 0.02 | Normalized squared gap error plus penalty per class |
| `fp.max_displacement` | 1 | Per-axis local movement / own radius; inherited parent translation is additional |

Distance attraction uses the same physical-area/density radius as the repulsion
circle (physical member area when the repulsion budget is zero). For distance
`d` from the candidate center, its energy is proportional to
`d*d + fp.attraction_outside * max(0, d-R)^2`. The pull grows gently inside the
circle and more steeply outside; its value is continuous at the boundary. This
is a soft anchor, not a containment constraint. Each member contributes by its
physical area, and membership in several candidates adds several springs.

The coefficient is calibrated once per phase against the ordinary wirelength
and density gradients at one radius, before adding overlap/alignment penalties.
It is then retained, rather than renormalizing away increased displacement.
Moving FP centroids include the centroid derivative so a group remains free to
translate, with the usual startup ramp. Frozen targets apply immediately in
macro GP without restarting that ramp. Circle repulsion retains its existing
normalization and schedule. The final cell pass clears all centroid anchors.
`fp.distance_attraction=false` selects the legacy per-iteration normalization;
`fp.attraction_outside=0` keeps a quadratic spring with the new fixed coefficient.
The native library remains opt-in (`centroid_distance_attraction=false`).
`fp_place.json` reports both phases' coefficients and calibration iterations.


The user-authorized production defaults also enable SRAM snapping and the evaluated
repulsion/routability controls, with 64 bins, utilization 0.30, density 0.5,
5 µm halo and 1500 iterations; IO/schedule horizons remain 500. H2D requires
explicitly disabling the FP and incompatible plain-placement controls.
The macro-source FP stage does not restore/publish placement cache entries;
the final fixed-macro cell stage uses the ordinary placement cache.
`fp_place.json` records targets, actual means, relations, FP history and
phase times. `fp_candidates.json` / `fp_membership.tsv` retain the candidate/color
census. With snapshots, `fp_start.json` / `fp_gp.json` describe floorplan GP and
`gp_start.json` / `gp.json` describe the subsequent frozen-target GP.

The [64×64 Eyeriss evaluation](../../repros/placement-results/eyeriss-fp-snap-20261016/comparison.html)
fits all 204 target circles legally in 1.15 s, with 123 alignment, 36 shared-spacing
and 32 symmetry relationships. The four large target circles form an exact
rectangle. Mesh, staggered triangle and tree examples also center correctly.
However, weak anchors do not fix the measured member means: circle overlap returns
in refinement, including one large ClusterGroup pair after final cell placement.
Final legal HPWL is 6.578 m versus the reused R8H3 reference's 4.595 m (+43.15%).
This comparison adds a refinement GP as well as snapping, so it does not isolate
snap's contribution. The feature remains experimental and off by default.

For an unsnapped control, use `synth.place.fp.snap=false`; the refinement GP then
attracts members to their measured first-floorplan means. To legalize the completed
first floorplan directly, use `synth.place.fp.iterations=1500`,
`synth.place.iterations=0`, `synth.place.fp.anchor=0`, and
`synth.place.fp.snap=false`. This skips refinement GP and frozen targets, then runs
the ordinary native legalization/detail placement. Only SRAMs and IOs are fixed in
the separate final cell placement invocation. The zero-refinement path does not
serialize native LG/DP microtimers; driver and floorplan GP times remain available.

The 3K threshold experiment uses explicit `synth.place.fp.minimum=3000`, keeping
the default at 1K. In this Eyeriss netlist all 204 selected scopes exceed 4.57K,
so this threshold increase does not remove candidates. The hierarchy-local snap
change is evaluated separately from that unchanged scope census.

The [3K control evaluation](../../repros/placement-results/eyeriss-fp-3k-20261016/comparison.html)
retains the 1K results and reruns direct locking, snapped refinement, unsnapped
refinement and an independent direct-lock repeat. Direct locking gives 4.595 m
final legal HPWL; unsnapped refinement gives 7.395 m. Hierarchy-local snapping
accepts 86 relations and gives 5.345 m, versus 6.578 m with the earlier cross-parent
snap. Target circles separate, but measured means still drift: four of six large
ClusterGroup circle pairs overlap after final cell placement. Physical SRAM/cell
and halo legality remain valid. The report includes small-circle before/after PNGs,
nested-candidate zooms, physical hierarchy overlays and phase times. No new routed
or timing result is claimed.

For the R8 evaluation protocol, SRAM macro snap is required **before** locking
the SRAM frames: pass `synth.place.macro.snap=true` in the macro-source invocation.
This is independent of `synth.place.fp.snap`, which fits candidate centroid
targets. The two-pass protocol skips centroid snap/refinement and applies SRAM
snap during legalization of the first floorplan. The three-pass protocol retains
local centroid snap and refinement, then applies SRAM snap during legalization.
Both final cell invocations import SRAMs/IOs as FIXED and disable `fp` and
`macro.snap`. Earlier 4.595 m / 5.345 m controls had SRAM macro snap disabled;
their measurements are retained as controls. This explicit evaluation protocol
does not change the global option defaults.

The [SRAM-snap control comparison](../../repros/placement-results/eyeriss-macro-snap-controls-20261016/comparison.html)
runs exactly two new evaluations with this protocol. Two design-GP passes give
4.644 m final legal HPWL; three passes with local centroid snap/refinement give
4.769 m. Macro snapping changes the matching two-pass control by +1.05% and the
three-pass control by -10.78%. Source pre-legalization GP snapshots are
byte-identical to their snap-off controls. The two-pass SRAM snap produces 403
groups (392 exact); three passes produce 430 groups (427 exact). Hard halos take
priority over the remaining soft equalities. All physical/halo/connectivity/IO
audits pass, and all 1040 SRAM frames remain fixed in final cell placement. The
report includes all saved-stage PNGs, hierarchy/circle overlays and phase times.

Candidate discovery now requires `synth.place.fp.min_copies` qualifying siblings
**per immediate hierarchy parent** (default 4). Copies in different parents never
combine to meet the threshold. Each copy must independently exceed the configured
equivalent-area minimum and contain more than two movable macros. The setting also
is independent of the area threshold. The area setting remains separate;
the current experiment uses `synth.place.fp.minimum=3000`.

`synth.place.fp.colors=true` adds an opt-in fallback for designs with SRAMs but
little repeated RTL hierarchy. Hierarchy candidates are discovered first. Each
remaining synthesis color with at least one movable non-standard-cell macro and
physical area strictly above `synth.place.fp.minimum` becomes one candidate.
The area includes the full color's standard cells and macros, in the same
equivalent-cell units used by hierarchy discovery. A color already used by any
hierarchy candidate is excluded entirely, including members outside that scope.
The color option defaults false; the existing area default remains 3000. The CNN
experiment explicitly uses 4000. Virtual fillers are disabled in both its control
and color-centroid runs.

With color candidates enabled, centroid repulsion is scoped by the selected
candidate parent, independently of repeated-type families. All root candidates
repel one another; nested candidates repel only siblings under their own selected
parent. Parents never repel their children. Color-only candidates bypass centroid
snapping exactly. Their post-floorplan means become fixed targets for macro
refinement, followed by the existing SRAM snap and fixed-macro final cell pass.
The final cell pass has no centroid anchors. `fp_place.json` and
`fp_candidates.json` record each candidate's kind, snap eligibility, and repulsion
scope. With the color option off, the previous same-family repulsion is retained.

Sibling counting precedes wrapper collapse: a singleton child inside each wrapper
does not hide repeated outer instances under a common parent. Reports record
`minimum_copies_per_parent`, `copy_scope`, and each candidate's `copy_parent` /
`copies_per_parent`. `copy_parent` is the actual hierarchy parent used for eligibility;
the numeric `parent` still identifies the nearest retained candidate for centroid
hierarchy. The copy setting and scope rule participate in placement fingerprints.

A native discovery-only replay of the saved Eyeriss physical netlist at 3K and four
copies retains **76 candidates in three families**: four ClusterGroups, 36 input
activation SRAM banks (nine per GLB parent), and 36 PE instances (nine per PE-cluster
parent). The earlier 204-candidate tables used the previous whole-design rule.
Their layouts/HPWL remain historical measurements. The
[four-copies-per-parent evaluation](../../repros/placement-results/eyeriss-parent4-20261016/comparison.html)
reruns exactly the two requested flows with 76 candidates, 3K minimum, 64×64 bins
and SRAM snapping before locking. Two passes give **4.559 m** final legal HPWL
(−1.83% versus the recorded 204-candidate snap-on control); three passes give
**11.890 m** (+149.32% versus its 4.769 m control). Synthesis / placement-export
totals are 28.42 / 222.81 s and 28.81 / 294.89 s, respectively. The three-pass
regression develops during the fixed-SRAM cell GP: its adaptive density weight
keeps rising while overflow approaches 0.1. Reaching that density threshold does
not establish stationary wirelength. Both source/final layouts pass independent
physical/halo/connectivity/IO checks, and all SRAM frames stay fixed through the
last pass. The report includes stage PNGs, hierarchy overlays, small centroid
before/after-snap PNGs, independently recomputed stage HPWL and phase times.

`bazel build -c opt //tools:physical_repair` builds a native repair-only diagnostic.
It accepts one JSON configuration containing `lg`, `top`, `def`, `liberty`,
`tech_rc`, `sdc`, `lefs` (array), `output`, and optional `halo` (microns).
Supply LEFs for every master in the DEF, including inferred memory proxies.
It fills DEF cell dimensions from LEF and verifies that initial master names,
positions and orientations are unchanged, then runs sizing and design repair
without synthesis or GP. The output retains per-evaluation timing, both repair
traces, phase runtimes, the final DEF and the repaired graph library.

For an exact replay of an already exported placement, set `retained_repair` to
`true` and set `net_weights` to the path of a JSON object mapping every current
graph driver key to a finite weight in `[1, 100]`. This mode retains the imported
placement while reconstructing the graph, then permits ordinary repair trials. It checks every
initial master, position, orientation, IO coordinate and driver weight before
starting fresh timing analysis; `retained_initial.def` and
`retained_import.json` record that starting state. It cannot be combined with
`cell_place`, `floorplan`, `timing_only` or `snapshot_only`. Use identical
placement and weight inputs for paired repair-policy experiments; this replay
starts a new timing session and does not reproduce a prior run's incremental
timing history.

For terminal-legalizer replay, `legalize_sat_milliseconds` and
`legalize_sat_conflicts` set the terminal solver budgets independently of the
legacy `satlegalize_milliseconds` and `satlegalize_conflicts` options. Omitted
values use the production defaults.

For a read-only timing comparison, `timing_only:true` preserves all imported
cell and IO coordinates and uses the supplied SDC plus reset false-path cuts
persisted with the physical RC. Add `prects_timing:true` to derive the placement
timer's additional clock/reset exclusions; this requires the
mapped LGraph input (`lg`) so synthesis roles survive and cannot be combined
with `source_def`. The report labels this analysis with `prects_filtered`.
No sizing, buffering, legalization or timing-constraint edits run in this mode.
Independent DEF-based timing comparisons also need the recorded synthesis reset
roots in QoR's `prects_controls.reset_ports` and `reset_pins`, with the same
Liberty buffer/inverter distribution closure. DEF and Liberty alone cannot
identify registered synchronous resets. Preserve this metadata when exporting
false paths; do not infer reset roles from instance or net names.

`timing_calls.jsonl` separates each diagnostic timing callback into engine/report
generation and feedback parsing, with report byte counts. Sizing/forest trial
records include preparation, placement/RC and timing durations, upsize/downsize
operation counts, and the metrics that rejected a transaction. Trial operations
include rolled-back proposals; compare final instance/master inventories to count
surviving edits. These measurements preserve the existing acceptance checks.
`feedback_pruned` counts edits excluded from a rejected sizing batch's next retry.
Placement refinement and repair return their accepted timing feedback. Callers can
move that feedback into the next repair pass only when graph, placement, RC and
constraints are unchanged; rejected-trial timing is never reused as the baseline.
Final legalization can accept this feedback too. It reuses detailed timing only
when the rebuilt RC nets, cell identities/poses, terminals and reset cuts match
exactly; `final_legalization.reused_timing` records the decision. Changed physical
states and approximate rankings still require detailed evaluation.
The final STA report can reuse the persistent placement timer after reconciling
the final physical state and reset cuts. This is distinct from reusing the prior
feedback above: the report still checks the requested state, and can propagate
changes, but avoids rebuilding a second timer and refitting unchanged drivers.
Its `physical_session_reused` flag and ordinary STA cache counters identify the
reporting path.
GP rank feedback carries a compact pin array and an immutable pin/net index.
Net weighting uses indices for the sampled RC snapshot; a different or reordered
snapshot uses physical labels. Clock/reset filtering and timing-aware legalizers
accept this representation as well as detailed feedback. This preserves rank
selection and legalization priorities without copying full pin maps at each GP
checkpoint. Detailed repair still requires detailed feedback.
With pin-level electrical feedback, ordinary sizing selects gates with negative
slack, electrical violations or drive stress. Healthy neighbors do not consume
detailed timing trials for optional area recovery, even when an unrelated path
misses setup. Final buffer bypass candidates likewise require an affected or
uncharacterized incident net; explicit recovery remains available for area
optimization. Forest reconstruction remains available while setup fails; once
setup is met, healthy forests are skipped too. The sizing-to-design handoff records that
sizing has already run. If the forest sweep accepts no changes, it skips repeated
sizing; accepted forest edits reopen sizing against the changed loads. These
decisions are automatic and do not require benchmark-specific settings.
The full synthesis flow shares optional-trial admission across warm refinement,
sizing, forest repair, recovery and cone resynthesis. New trials stop once six
detailed physical evaluations have been spent, reserving final legalization and
standalone STA within the eight-evaluation budget. When electrical repair is
enabled, warm refinement stops at four detailed evaluations, leaving the next
two slots available for sizing and buffer repair. Rejected warm trials must not
starve those phases. Ranking samples do not spend this budget. Admission is checked before cloning or legalization, and reports
record `timing_budget_exhausted`; accepted states and mandatory final validation
are retained. Cone resynthesis also accepts the preceding unchanged state's
timing instead of repeating a baseline evaluation. Low-level callers can provide
their own shared `Trial_admission` callback.
This avoids duplicate analyses between passes; each changed-state callback is an
incremental update of the persistent timer. The diagnostic tool publishes the accepted
analysis as `final_timing.json`; the last numbered trial can be a rejected state.
Structural barriers use OpenTimer's `update_netlist()` to materialize queued
library, gate, net, RC and constraint changes without propagating intermediate
timing. RC attachment, case analysis and data exclusions complete before the
feedback query propagates timing. Dirty frontiers survive these barriers; the
next timing query uses the final combined state and the same detailed model.
Detailed RC threshold crossings use a bracketed Newton solve with the waveform's
analytic derivative and bisection fallback. Its time-relative tolerance is
`1e-10`, below the precision of the timer's float outputs; ranking samples retain
the cheaper total-load/Elmore model. Numerical regressions cover reference
bisection, waveform residuals, extreme poles and unit scaling.

### Fixed-macro cell-placement experiments

For a controlled replay, `physical_repair` accepts `source_def=true` and
`cell_place=true` to import the exact mapped DEF and recompute standard-cell
placement while checking fixed macro geometry and cell masters. The JSON can
set `density`, `grid_density`, `threads`, `grid_density_mode`, `grid_density_max`, `grid_route`,
`virtual_fillers`, `virtual_filler_max_count`, `virtual_filler_fraction`, `local_density_weight`,
`grid_local`, `local_density_range`, `local_density_jitter`,
`adaptive_density`, `routability` and `route_model`. These select the same
production controls described above. Omitted fields retain the diagnostic's
existing values (`density=0.5`, `bins=64`). Keep the supplied core and macro
frame fixed when isolating spreading, then evaluate the accepted DEF with
matched CTS, routing and extraction. A density estimate alone is not routed QoR.

The following opt-in `synth.place.*` controls preserve the existing defaults:

| Option | Default | Meaning |
| --- | --- | --- |
| `density_upper_bound` | `false` | Underfull density bins contribute no Poisson charge. Useful with a supplied core and fixed macro obstacles. |
| `min_iterations` | `40` | Minimum GP iterations before density convergence may stop a call. Experiments use 500 to finish wirelength smoothing. |
| `fixed_macro_overlap` | `0` | Exact cell versus fixed-macro overlap penalty. Its Huber force points toward the nearest feasible edge and is bounded by this coefficient. The main GP ramps it quadratically over `schedule_iterations`; the IO pilot leaves it disabled. No force acts between separate objects or on fixed macros. GP sees halo-padded boxes where applicable. |
| `detailed_iterations` | `2` | Iteration cap for each existing k-reorder, independent-set matching and global-swap kernel. |

The obstacle pass hashes fixed macro footprints into the existing bin grid; it
adds no finer density grid or cell-to-cell pair enumeration. All comparisons use
64×64 native bins. These controls do not insert fillers or change the supplied
core, SRAM frame, IO frame, cell masters or net connectivity during placement.
The density/obstacle controls remain experimental; timing and legalizer movement
must be checked on each design before adopting a different default.

The diagnostic also accepts an experimental final-GP macro-transparency schedule:
`macro_transparency_cycles=0` disables cycling, `macro_transparency_warmup=100`
sets the initial opaque iterations, and `macro_transparency_period=20` sets
both transparent and opaque phase lengths. With five cycles, iterations 1–100
are opaque, 101–120 transparent, 121–140 opaque, and so on through iteration
300; all remaining iterations are opaque. The total budget must include at
least one additional opaque period after the cycles. Early convergence and
selected-iterate restoration cannot bypass this settling period.

For the fully transparent diagnostic, set `macro_transparency_always=true`
(default false) with zero cycles. Macros remain transparent from the first GP
iteration through the last; there is no opaque settling phase. Density feedback
and checkpoint gating then use capacity with fixed macro/mask areas omitted.
`overflow` reports this diagnostic model, while `raw_overflow` still reports
physical occupancy. Pins retain their original offsets and connectivity. Inspect
`before_legalization.def` for the transparent result: normal legalization still
uses the physical macro obstacles, so its output is a different diagnostic.
In this mode `macro_transparency_period` controls snapshot frequency only.

Cell-only spreading can be combined with transparency: `local_density_weight`
sets its strength, `local_density_range` its decay length in local-bin pitches,
and `grid.local` its independent grid. Transparent macro/mask bodies do
not subtract local capacity. `local_density_target=-1` inherits global density;
an explicit value in 0..1 sets a separate real-cell target without changing
filler population or the global target. Zero emits pressure from all real-cell
density. Set `local_density_jitter=0` when comparing the same imported seed.

Transparent phases zero fixed macro density charge, including macro-shaped
row-exclusion masks, and disable the explicit fixed-macro overlap penalty and
routing macro blockage/pin-access model. Fixed coordinates, physical dimensions,
actual macro pins, connectivity, and final legalization obstacles remain intact.
Filler count and dimensions stay fixed; effective filler charge compensates for
the temporarily available area. Each switch clears routing inflation, restarts
optimizer momentum, and invalidates stale selected-iterate/stability history.
The density-weight and wirelength-smoothing schedules continue normally.

This experiment requires fixed macros, disabled movable-macro density forces,
and a single GP call without external continuation or topology replacement. It is not
an `fp_plan` or `macro_plan` control. `cell_jump=1` may retain a supplied
pin-adjacent DEF seed without later graph-depth jumps; see [cell-jump
continuation](cell_jump.md). No cells are directly relocated by transparency.
GP progress includes `macro_opacity` (zero or one). The diagnostic writes a
`macro_transparency/nodes.tsv` map and phase-boundary `iterationN.bin` files:
native-endian doubles, all movable x coordinates followed by all movable y
coordinates, in DBU and node-map order. Macro opacity affects optimization only;
reported raw physical overflow and final legality use the original fixed geometry.

The alternative diagnostic `vanished_memory=true` (default false) starts final
GP with fixed macro/mask exclusion rectangles at 60% of their original width
and height (36% area), centered on the original bodies. Hold this size through
iteration 70, then grow linearly over 25 steps: iteration 71 uses 61.6%,
iteration 75 uses 68%, and iteration 95 reaches full size. Physical macro
positions, sizes, pin offsets and connectivity never change. Global and local
density capacity, explicit overlap pressure and routing obstacles use the same
centered footprint. Filler charge tracks its available area without changing
filler count or size. Routing is refreshed at every growth step when the usual
density gate permits it.

The configured `local_density_weight` is held through iteration 70, then drops
by 0.05 per iteration starting at 71, clamped to
`vanished_memory_local_floor` (default 0). The floor must be finite, nonnegative
and no larger than the starting local weight. A starting weight of 0.67 becomes
0.62 at 71 and zero at 84 with the default floor; a floor of 0.1 is reached at
82 and retains that local spreading force through the end of GP. This floor
does not disable virtual fillers: set `virtual_fillers=false` explicitly when
testing without them. The budget must allow at least 20 settling steps after
both memory growth and the local-weight ramp finish. This option cannot be
combined with the alternating or always transparent schedules, movable macros,
external continuation or topology replacement. It does not enable itself in
`fp_plan`/`macro_plan`. Default local weight is unchanged; configure it explicitly
for the experiment.

Progress adds `macro_exclusion_scale` (linear extent, not area or fractional
opacity) and the effective `local_density_weight`. Raw overflow retains the full
physical geometry; model overflow and checkpoint gating use the growing body.
The snapshot period is `macro_transparency_period`, throughout the GP solve.
Optimizer momentum restarts at the first growth step, full-size restoration,
and the start/end of the local ramp, rather than restarting every growth step.

### Placement-color leaves and adaptive IOs

The opt-in controls below extend FP without changing existing defaults.

The matching native support is in `../DREAMPlace`; `MODULE.bazel` uses that local
checkout for co-development of separate circle budgets and fixed-terminal updates.

| Option (`synth.place.` prefix) | Default | Meaning |
|---|---:|---|
| `fp.color_source` | `synthesis` | `placement` selects unbounded macro input/output cones; `memory_first` selects bounded memory input-cone growth. Both run after synthesis and require FP colors and Liberty metadata |
| `fp.color_maximum` | 30000 | Memory-first logic growth limit in equivalent average standard-cell area; macro footprints do not consume this budget |
| `fp.color_minimum` | -1 | Strict color member-area threshold in equivalent cells; -1 inherits `fp.minimum`. Reserved area does not affect eligibility |
| `fp.pin_adjust` | false | Bus-aware initial IO assignment and periodic perimeter moves during FP |
| `fp.pin_interval` | 50 | Iterations between IO updates, independent of STA/resize cadence |
| `fp.pin_step` | 0.01 | Maximum per-update travel as a fraction of die perimeter |
| `fp.pin_improvement` | 0.001 | Minimum relative incident-net weighted-HPWL improvement for moves/reversals |
| `macro.pin_adjust` | false | Enable the same bus-preserving IO moves during GP with movable macros, independently of FP |
| `macro.pin_interval` | 50 | Macro GP iterations between IO updates |
| `macro.pin_step` | 0.01 | Maximum per-update macro-stage travel as a fraction of die perimeter |
| `macro.pin_improvement` | 0.001 | Minimum relative incident-net weighted-HPWL gain during macro GP |

Add these flags to a technology-configured `lhd synth` placement invocation:

```
--set synth.place.fp.colors=true --set synth.place.fp.color_source=placement \
--set synth.place.fp.pin_adjust=true --set synth.place.macro.pin_adjust=true
```

The color source and pin adjustment are independent: either can be enabled
without the other. Explicitly fixed IOs remain fixed in both cases.

Placement colors are split into local portions under their deepest selected
hierarchy candidates. A portion outside every selected candidate remains at the
root; external shared logic does not promote the portions inside candidates.
Each portion is evaluated independently for a movable macro and the physical
area threshold. Structural candidates retain all their physical descendants for
attraction. Each local color leaf receives its full member budget. One-stage
reservation reachability is recomputed from local members, allocated once among eligible color leaves and
propagated once to each ancestor, without duplicating parent budgets. Member attraction
remains additive through ancestors. Parent/child centroids never repel, and
repulsion is between siblings at the same candidate scope. Colors are not
independently snapped; a color leaf follows its parent's snap translation.

Pin adjustment preserves actual graph-port bus lanes in circular numeric order,
ascending or descending. Buses may have gaps and interleave, and can turn corners.
A whole-bus reversal is accepted only on improved weighted HPWL. Imported fixed
pins retain their coordinates, including pin centers inset from the edge. Imported
constraints bind by graph-port name; a scalar name and a DEF name ending in `[0]`
are distinct, so check naming before claiming that all pins are constrained.
Slots use a conservative pitch derived from the
used LEF layers and pin extents; movements respect spacing, a travel bound and a
gain margin. The initial assignment uses ordered dynamic programming. Subsequent
moves score only incident nets. Native GP resets momentum/convergence history
after a terminal changes, and timing callbacks see the updated coordinates.
IO updates can run without centroids or STA. The FP and macro controls are
independent; macro adjustment also works when FP is disabled. With both enabled,
FP performs the ordered initialization and macro GP continues from those IOs
without reinitializing them. After macro placement, the staged flow freezes IOs
alongside macros for final cell placement. A separate final-cell invocation must
likewise import the handoff pins as `FIXED`; the macro callback is inactive when
there are no movable macros.

When preparing comparison inputs, preserve actual design pin constraints, but
leave automatically chosen pin positions `PLACED` if either adjustment is to
optimize them. A script that converts every imported pin to `FIXED` prevents
both stages from moving those pins. Do not infer design-required positions from
an automatically generated intermediate DEF.

The ordinary automatic IO assignment uses imported routing tracks for its
die-edge slots when those tracks are available. Left/right pins use a horizontal
layer; top/bottom pins use the next vertical routing layer. Movable edge-pin
shapes extend inward to reach the normal-axis track. This avoids off-grid pins
and wrong-way access stems on technologies with staggered track patterns.
Fixed imported IO coordinates and shapes remain unchanged.

`fp_placement_colors.json` summarizes physical/reserved/unassigned area and
merges. `fp_placement_colors.tsv` records each member and its reservation
recipients. `fp_candidates.json` and `fp_place.json` distinguish member area from
repulsion area. `fp_pins.jsonl` and `macro_pins.jsonl` record stage-specific pin positions,
weighted HPWL, travel and reversals. Reports live under the selected output
directory, or its `macro/` subdirectory for the three-pass flow.

For controlled evaluation on an existing mapped graph, `//tools:physical_repair`
accepts `floorplan_place:true`, `lg`, `top`, the same technology/SDC/core inputs,
and `fp_color_source` / `fp_pin_adjust` / `macro_pin_adjust` in its JSON configuration. This mode uses
the normal FP, macro and final-cell APIs without synthesis. Optional
`fp_color_minimum`, `fp_iterations`, `fp_timing_interval`,
`fp_timing_after_density`, `fp_timing_movement` and `fp_resize` make experimental
choices explicit; it checks unchanged masters/counts when repair is disabled.
Keep configurations, reports and images under ignored `repros/`.

The optional `synth.place.fp.color_source=memory_first` uses bounded, round-robin
memory input-cone growth after synthesis. Enable it with
`synth.place.fp.colors=true`; `synth.place.fp.color_maximum=30000` sets the
logic-growth cap in equivalent average standard-cell area. Macro footprints
are excluded from that growth cap and included fully in centroid sizing.
See [placement coloring](../color/README.md#memory-first-placement-colors) for
ownership, merge ordering, boundaries and repair behavior. This option does not
recolor or resynthesize the mapped netlist. The default source is still
`synthesis`. All other FP/macro/final placement options apply unchanged.

For a grouping-only diagnostic, `tools/physical_repair` accepts
`floorplan_place: true` with `color_analysis_only: true` and a physical color
source. It writes membership and centroid-area reports under `analysis/`
without GP, legalization, RC/STA or a final DEF. The result is explicitly tagged
`floorplan_color_analysis`; it is not a legal placement. Use this inexpensive
check to inspect budget sensitivity before running full placement. The typed
API's `analyze_colors_only` mode rejects caches, prior placements and competing
placement modes.


### Moving hierarchy-centroid alignment during FP

`synth.place.fp.align_weight` is an opt-in FP-only force (default `0`).
`align_sigma=0.25` sets the bounded well as a fraction of circle diameter;
`align_start=100` and `align_full=500` define its quadratic ramp. Weight `25`
uses the same relative scale as the default SRAM alignment experiment.
The reference is the absolute net wirelength+density force of each candidate's
movable members, averaged across candidates per axis.

Candidate circles use their physical reserved area at the target density.
Only the same family under the same candidate parent can pair. Overlap on
exactly one axis attracts their centers on that axis; overlap on both axes is
left to existing circle repulsion. The gradient reaches members through the
area-weighted centroid derivative, including summed contributions for shared
members. Existing attraction, repulsion, end-of-FP snap and macro-stage anchors
are retained. This force ends when FP freezes the centroid targets.

`fp_history.jsonl` streams iteration, overflow, weighted HPWL, circle overlap,
and centroid alignment pair/residual/ramp/applied-force diagnostics.
`fp_place.json` includes the same history, controls, and alignment runtime.
An explicit zero weight preserves the previous floorplan algorithm.

During FP, `synth.place.fp.parent_weight=1` enables explicit parent–child
centroid springs (default `0`, opt-in). It scales the once-calibrated member
attraction coefficient; `fp.parent_outside=4` controls the outside-circle term.
For separation `d` and parent radius `r`, the energy is proportional to
`d² + parent_outside * max(0,d-r)^4 / r²`. The force is linear inside the
parent circle, with an additional cubic restoring force outside it. Equal and
opposite endpoint gradients propagate through both moving, area-weighted
centroids, including shared members. These springs only run during FP; the
macro stage retains frozen-target member attraction. Diagnostics record edge
count, maximum child/parent distance in parent radii, applied force and time.

FP centroid snap reserves the whole circle inside the core. Radius-inset bounds
also apply after inherited parent translations; mandatory boundary correction
has priority over the regularity movement limit. Unique color leaves gain no
snap relations but also obey these bounds. An impossible circle fit is reported
instead of handing an out-of-core target to macro GP. Rectangular macro
regularity continues to use its existing bounds and macro legalizer.

`synth.place.fp.anchor_macros_only=true` restricts the frozen-target spring in
macro GP to movable macros. FP itself still attracts all candidate members. The
candidate areas, circle radii and calibration retain all members, so dropping
standard-cell springs does not strengthen the remaining macro springs. The
option defaults to false; final cell placement has no centroid anchors.

### Steiner route feedback (`3r-route`)

`synth.place.route.model=rudy|rsmt|auto|off` selects the estimator. The default
is `rsmt`. `rsmt` uses FLUTE; `auto` starts each stage on RUDY and latches
RSMT after consecutive qualifying route samples. Each of `fp`, `macro`, and
`final` has `synth.place.S.route.model=inherit` and these independent controls:

| Suffix | Default | Meaning |
| --- | --- | --- |
| `switch` | 0.05 | RUDY route overflow at or below which auto may switch |
| `target` | 1e-9 | Active-model route overflow required for convergence |
| `hold` | 1 | Consecutive qualifying samples (1..1000) |
| `probe` | false | One diagnostic RSMT sample at a convergence candidate |
| `probe_iterations` | 100 | Maximum extra GP iterations after a failing probe |

For example, `--set synth.place.route.model=auto --set
synth.place.macro.route.model=rudy` keeps macro feedback on RUDY while final
placement switches to RSMT. `synth.place.fp=false` skips FP planning; the
incremental fused flow still runs macro placement followed by final cell
placement when `synth.place.final=true` and the design contains macros.
`lhd.incremental=true` enables compile, mapping, timing and placement reuse.

The existing `synth.place.routability=false` disables feedback in the ordinary
FP, macro-source and final GP stages. H2D retains its independent
`synth.place.macro.routability` switch (default false).
A stage's `route.model=off` disables its inflation, macro channel pressure and
route convergence test. IO prototype GP continues to disable route feedback.
An enabled probe accepts successful RSMT diagnostics without changing feedback;
a failing probe latches RSMT and continues within the configured extra budget.
Ordinary auto switching also evaluates both models at the transition. Inflation
remains monotone across the change of estimator.
H2D retains the latch across its abstract and final-cell passes and rescales
incumbent merit comparisons after a switch. Its cached macro frame preserves
the active estimator for subsequent scoring and reports. The same stage latch
applies after a failed Rudy probe; a passing probe is not repeated by later
inner solver calls.

Density overflow below 0.3 gates enhanced route sampling; it is distinct from
route overflow. Enhanced route overflow is the mean excess of directional/pin
utilization above one **plus** `(unroutable_nets + blocked_pins)/bin_count`.
These geometric penalties can dominate a macro stage and prevent auto switching.
A stage that never reaches its switch threshold reports `route_never_switched`.

`synth.place.route.degree=40` caps FLUTE's net degree (2..1000); larger nets keep
the weighted bounding-box approximation. `route.accuracy=3` accepts 1..10.
Explicit FLUTE options require an rsmt/auto/probe stage. FLUTE nets omit the
bounding-box pin-count correction to avoid double counting. Degree two uses a
box and degree three a median Steiner junction without a FLUTE call. Other
nets use deduplicated, core-clamped integer pins, guarded against coordinate and
wirelength overflow. Missing lookup tables fail clearly. Straight projections
consume a site/row corridor. Macro-aware RSMT retains net-box reachability and
detour detection and distributes tree wirelength over reachable free space;
per-edge obstacle routing is deferred.

`target > switch`, negative/non-finite levels, invalid models, holds, FLUTE
controls and probe budgets are rejected. All stage policies and FLUTE controls
participate in placement and native continuation identity. The command's resolved
options include the controls; `route_options.json` resolves all three stage policies,
including inherited models and feedback enablement. `gp_history.json` and snapshot `gp_history.jsonl`
include active `route_model`, `route_sampled`, `route_calls`, `route_seconds`,
model-specific seconds, bin count, blocked pins, unreachable nets, switch
iteration, both switch overflows, and probe status. Seconds include the two
model calls on a switch sample. Values of -1 mark an absent switch measurement.

`tools/rudy_metrics physical_snapshot.txt output_prefix [rudy|rsmt]` measures a
saved snapshot without modifying it, exporting degree histograms, call times,
and base/enhanced utilization maps. `physical_repair` with `snapshot_only=true`
and `snapshot_prects=true` excludes the same Liberty-derived clock/reset networks
as placement (including buffer/inverter closure); `snapshot_filters.json` records
the number excluded. Use this on both layouts when comparing data HPWL or RUDY:
otherwise reset-distribution wirelength can dominate a pre-CTS comparison. The
optional filter defaults off, preserving the existing clock-pin-only snapshot.
Evaluate against the same placement's real router map: estimator overflow is a
proxy, not a routed timing result. In particular, HPWL is only a lower bound on Steiner length; the draft's universal
`RSMT <= 1.12 * HPWL` test is invalid (four corners already give 1.5). Tests use
an independent Manhattan spanning-tree bound instead.


The design and evaluation protocol are documented in
[`todo/livehd/3r-route.html`](../../todo/livehd/3r-route.html). The global-swap
kernel caches high-degree net extrema and current-position net HPWL to preserve
exact proposal costs while avoiding repeated full-pin scans. Sparse flat HHDS attributes persist in sorted
key order so unchanged compiled designs keep the same completed-placement cache
identity. Both changes have independent correctness regressions; routing model
and existing placement defaults remain unchanged.
Repair candidate sorting caches immutable electrical/slack, critical-path and
name keys before sorting the original node sequence, preserving proposal order
and trial budgets while avoiding repeated physical-label construction.

### Local constants, timing refresh, and legalization

Constant-output drivers are recognized from Liberty functions and split per physical
load, including bus bits, memory pins and constant top-level outputs. Their area is
reserved before sizing the placement frame. Legalization defers these drivers behind
functional cells; final tie placement uses the users' oriented pin positions and legal
row whitespace, with bounded equal-footprint exchanges when necessary. Exported DEF,
physical nets, graph connectivity and repeated placement retain the local identities.
Automatically generated IO positions remain movable in later FP/macro runs; only
explicit `FIXED` constraints persist. Masked clock/reset nets do not attract IO pins
toward their sinks.

Inserted buffer trees inherit half of the original driver's excess weight above one,
on every segment. Fresh STA replaces provisional priorities and immediately resets
nets outside the selected critical set to one. Final cell GP now uses the same bounded
checkpoint cadence as macro GP. `<stage>_weights.json` records current driver identity,
critical membership, applied weight and checkpoint. Final buffer recovery trials use
legal coordinates and fresh RC/STA; both adjacent nets must have one physical load
and no port branches. Electrical and timing acceptance still gate each bypass. The physical
flow bounds trials to 24 per design and reserves at most 12 for final topology
recovery; counters distinguish attempts, accepted edits and timer operations
caused by rejected-trial rollback.

The following legalization controls are opt-in (`synth.place.` prefix):

| Control | Default | Meaning |
|---|---:|---|
| `legalize.eco_connectivity_weight` | `0` | Incident-net wirelength cost during edited-cell legalization, 0–100; zero preserves nearest-seed placement |
| `legalize.timing_strength` | `0` | Displacement protection scaling, 0–99; zero retains ordinary legalization |
| `legalize.timing_threshold` | `0` | Fresh cell-slack rank threshold, 0–1; zero gives continuous protection |
| `legalize.timing_corrections` | `0` | Up to two local eight-iteration correction attempts after detail; fresh STA/electrical checks decide acceptance |

The selected final-GP coordinates receive fresh physical RC/STA before assigning
cell priorities. Priority packing tries bounded nearby free row intervals and falls
back atomically on fragmented/unsupported geometry. Weighted Abacus and detailed
placement retain displacement protection. `legalize_priorities.tsv` records current
slack, criticality, cost and GP anchors, including designs with positive slack only.
`legalize_timing.jsonl` reports independent post-legalization/detail timing and,
for terminal legalization, the exact final-GP timing before any cell is legalized.
`legalize_correction.jsonl` records correction acceptance. These controls do not select
new global defaults; use matched saved GP inputs when evaluating their scaling.

With positive `legalize.eco_connectivity_weight`, the fast ECO row-gap search
adds weighted incident-net HPWL to seed displacement when choosing legal sites.
Earlier edits contribute their chosen coordinates; pending edits retain their
seeds. Ideal-clock nets remain excluded. Unchanged cells, macros and IO stay
fixed, and the surrounding repair transaction still requires fresh RC/STA
acceptance. This is an opt-in placement heuristic; routed timing determines
whether a configuration is useful. The standalone diagnostic uses the JSON
key `eco_connectivity_weight`. Enabled `qor.eco.connectivity` telemetry records
net/pin and candidate counts; `search_seconds` covers the complete fast ECO
legalization, including its setup and legality check.

### ECO diagnostics

For ECO diagnostics, `placement::Options::eco_trace_cells` accepts exact edited
cell names (empty by default). A successful fast row-gap legalization records
only those names under `qor.eco.trace`, including seed/chosen DBU coordinates,
master, edit order, selected row, free-gap bounds and displacement. Tracing
does not change legalization or placement-cache admission. The standalone
`physical_repair` diagnostic accepts the same JSON list. Its explicit
`retained_repair=true, repair_scope="forest"` mode runs the existing forest-only
repair API, skipping sizing and recovery. `repair_scope="sizing_forest"` first
runs ordinary sizing, retaining its refreshed weights in process, then runs
forest-only repair. Both explicit scopes require the legacy engine. Absent
`repair_scope` keeps the ordinary complete flow. A successful first full forest
batch ends these modes; rejected batches retain the existing bounded bisection
behavior.

## Terminal legalization

Terminal legalization is mandatory for the final cell handoff. After GP and enabled sizing/buffer repair, the terminal
legalizer selects timed critical nets, jointly solves eligible critical pairs,
places the remaining selected critical cells, tries local pairs within a smaller
occupancy budget, and redistributes the remaining cells across the full core.
It runs after optimization, using the accepted repaired netlist, without another GP solve.
`synth.place.resize=true` enables sizing and buffer repair independently; the obsolete
`synth.place.legalize.terminal` switch has been removed. Intermediate optimization
trials still receive legal placement and fresh timing before acceptance. OpenROAD receives ordinary PLACED
cells and may modify them starting at CTS. Local-pair alignment is disabled by
default (`local_pair_limit=0`). The SAT budget defaults to 1000 ms and 10000000
conflicts. The replay tool defaults to a 5 µm macro halo in each direction;
native placement resolves its automatic halo from ten placement-row heights. Without timing feedback there are no timed nets to
protect; geometric legalization still runs. GP-only mode omits legalization.
For very small cores, native placement bounds the inherited routing-grid counts so bins
are at least one row high and wide enough to hold the widest cell under the hard
cap. Reports record the effective grid. This happens before critical selection
and does not relax the density cap. Explicit replay/API terminal grids remain
authoritative; the replay tool bounds its grid only when no bin counts are supplied.

Controls under `synth.place.legalize.`:

| Key | Default | Meaning |
| --- | --- | --- |
| `bin.max_density` | 0.8 | Mandatory bin area cap; must exceed GP target density |
| `bin.grid` | `route` | Bins per axis: the routing grid (`grid.route`, bounded for tiny cores) or an explicit 1..1024 used as given |
| `critical_fraction` | 0.1 | Fraction of timed data nets selected globally |
| `local_pair_fraction` | 0.5 | Local-pair occupancy budget relative to GP target |
| `padding_sites` | 1 | Left/right reserved sites for remaining cells |
| `remainder_wire_weight` | 4 | Optional timed-net HPWL cost for unprotected cells that must move |
| `padding_max_extra_rows` | 0 | Optional stage-5 padding movement budget in row heights; zero retains mandatory padding |
| `critical_padding_sites` | 0 | Left/right sites for protected critical cells |
| `critical_fanin_depth` | 0 | Optional depth of untimed side-input net protection (0–8) |
| `joint_minimize_total` | true | Minimize total displacement after the maximum-move objective |
| `candidates` | 12 | SAT candidates per endpoint and anchor |
| `pair_move_sites`, `pair_move_rows` | 4, 1 | Maximum candidate displacement from original GP coordinates |
| `critical_pair_limit`, `local_pair_limit` | 256, 0 | Proposal budgets; zero disables that alignment stage |

Step 1 uses one SAT instance for the entire selected set, with one candidate
choice per physical cell. Every chosen cell has exactly one placement; overlapping
candidates on different cells have an at-most-one clause, including cells on
different nets. Endpoints retain their original center-bin membership and obey
the configured movement radii. No endpoint is anchored or installed ahead of the
joint solve. Alignment witnesses, cell obstructions, competing same-layer
corridors and aggregate bin occupancy are checked together before commit.
Initial candidate menus balance nearby poses across legal rows. If a pair has
no valid alignment in those menus, its connected component is refined from the
complete legal site/row domain inside the existing movement bounds and original
bins. Oriented pin-access intervals index compatible layers/tracks; the normal
pin-alignment predicate verifies actual track phase and obstructions. Refinement
keeps the cheapest support for each endpoint pose on each track, so an alternate
pose required by a neighboring net is not discarded just because the other
endpoint can stay nearer its anchor. Track alternatives enter the final menus
before redundant shifts on the same track. The configured candidate cap still
applies to every physical cell. Fully supported components retain their original
menus, and no cell is installed during refinement. All selected pairs still enter
one joint SAT solve.

Pin-alignment relations use bidirectional support clauses; overlapping endpoint
poses cannot supply a witness. Common track segments also constrain other
selected cells' obstructions before solving. The remaining geometric checks still
validate every accepted model. Reports include `joint_menu_recovered` for pairs
that gained a witness through refinement and `joint_pool_candidates` for the
bounded-domain poses explored before final menu selection.

Reports distinguish `joint_no_witness` (no compatible nonoverlapping pose pair in
the sampled menus) from `joint_conflict_dropped` (a coupled conflict core).
`joint_density_cuts`, `joint_obstruction_cuts`, and `joint_corridor_cuts` identify
lazy validation failures; `joint_static_obstructions` counts cell-obstruction
clauses on unavoidable track segments. None of these menu-based rejections proves
that alignment is impossible over every legal site in the original box.

The selected fraction is a ceiling. Step 1 reserves at most the GP target density
per bin (including fixed cells), leaving the margin to the hard cap for later
steps. A conflict core removes its least critical selected pair and re-solves all
remaining placements; unrelated selections remain eligible. Dropped pairs still
receive critical-cell legalization in step 2. Solver timeouts are recorded as
unknown, not infeasibility. If the initial joint solve times out before finding a
validated model, priority admission searches for an incumbent with one additional
`sat_milliseconds` budget shared across all admission trials. Each trial re-solves
all admitted cells together; accepted endpoints are not fixed between trials.
`joint_admission_fallback` records this recovery and `joint_timeout_deferred`
counts proposals deferred without a proof of conflict. After selection, bounded
SAT trials minimize maximum endpoint displacement within the candidate menus; a timeout retains the last
validated solution. The default `joint_minimize_total=true` adds a binary sum of
endpoint displacement costs and minimizes it without changing the retained nets
or increasing the maximum-move bound. Every cost trial is a joint placement
solve. Reports distinguish a validated joint solution from completed maximum
and total displacement optimization. The first implementation accepts only two-terminal nets in
a common horizontal or vertical bin strip. Multi-terminal nets need explicit
RSMT-edge evidence and currently proceed to step 2.

The grid follows the active routing auxiliary grid. Usable capacity excludes
macro/halo-covered row sites and fixed `CORE WELLTAP`/`CORE SPACER` footprints.
Those immutable physical cells reserve their body without logic-cell sizing
padding. Input-fixed logic cells and critical cells locked during legalization
still count against the same hard cap. This prevents imported tap-only row
fragments from failing a logic-density limit before any movable cell is placed.
Cell area is apportioned across all overlapped bins. An optional side-input expansion also protects all movable endpoints of untimed
data nets feeding selected cells; clocks and supplies remain excluded. This is
useful to test slew effects that setup slack alone misses. Critical locks remain
immutable during redistribution. If timing-order packing fails, only the
unprotected remainder is restored and retried widest-first. If nearest positions
still fragment the free rows, a final widest-first retry places padded footprints
against gap/bin edges; mandatory cell spacing remains reserved. Search has no
maximum displacement in bins. Failure is
atomic; neither density nor padding is silently relaxed. The current row model
supports compatible single-height CORE cells. An exhaustive site scan catches feasible interior positions missed by the fast
menu before a fragmentation retry. Exhausting the current packing still does not
prove infeasibility, even when aggregate area fits.

Movable two-row CORE cells (the provisional domino abstracts of
`pass/domplace`, recognized by their reserved `__lhd_domino_` names; a library
multi-row cell keeps the single-row paths) are packed first by the anchor
prepass in `multirow.hpp`:
widest first, each takes the nearest site-aligned x interval free in both of
its rows (one row segment and site grid, never a split footprint), the start
row's orientation, and a pose the legalization bins can hold. Under
`Options::row_access` and `multirow_templates` a master starts only where each
local row lands on a row of its access class, counting rows from the first
core row recorded before the handoff taps cut any. Free sites and bin
capacity come from the occupancy model terminal legalization builds
(`row_occupancy`). The anchors are FIXED while terminal legalization packs
the single-row cells around them, then released.

Every start also passes the PG rail parity rule of OpenROAD's
`check_placement` (`pg_rails.hpp`): a row has the edge rails of its site's
one-row CORE cells (USE POWER/GROUND of routing-layer PG shapes centered on
the row edges), swapped on FS/S rows, and each PG rail on a local row edge of
a multi-row master must land on a row edge of its polarity. With rows
alternating N/FS a two-row master fits every other start, so a
`Multirow_template` may name a `rail_twin`: the same function, pins and
footprint with the other rail stack (the domino `_t0`/`_t1` templates). A
start that needs the twin takes it, in the floorplan and in the netlist
instance (`change_custom_declaration`); without a twin the start is refused.
A restored placement re-derives the template from its rows. `check_legality`
rejects a multi-row cell on rails of the wrong polarity and refuses one with
rails whose row rails it cannot derive.

The legalization bins are fitted (`fit_terminal_legalize_grid`) at least as
wide as the widest movable CORE cell over the cap and as tall as the tallest:
two rows when a two-row cell is movable. Bins one row tall would charge a
two-row cell's full height to every row of a bin column, so the hard cap
quantized to cell widths; the HighTide lfsr area-driven core failed the
prepass that way (`multirow_test`, `TinyCoreBinsAreAtLeastAsTallAsTheTwoRowCells`).
The prepass is atomic and reports `qor.multirow` (cells, even/odd starts,
`rail_twins`, displacement). With movable two-row cells present the macro stage, native
legalization outside the final stage, the detail-final region mesh and ECO
re-placement refuse explicitly, and a changed design places cold instead of
re-placing around a cached reference; there is no DREAMPlace change.

The one exception is the internal `Options::multirow_eco` (no lhd option),
set by sizing transactions that swap two-row variants around a previous final
placement (with `legalize_cells`). `multirow_eco.hpp` poses each edited
two-row cell, widest first: a cell that is not wider keeps its pose; a wider
one takes the nearest x of its own start row within `shift_sites` free of
single-row cells, else the nearest one overlapping at most `max_displaced`
single-row cells, else the packer's nearest pose for it alone, free of every
cell. Poses keep the terminal `padding_sites` free, pass the row-access and
rail-parity start check (taking the rail twin where the start needs it,
counted as `rail_twins`) and the hard bin cap, on the terminal grid the reference was legalized
on (GP bins as `dp::place` resolves them, so `automatic_bins` uses the
design's automatic grid; reported as `bins_x`/`bins_y`). The posed cells are then locked, padded
obstacles while nearest-whitespace ECO legalization re-places the displaced
and other edited single-row cells. Any failure throws `Multirow_failure` with
nothing committed; the two-row ECO never falls back to windowed
re-placement. `qor.multirow_eco` reports outcomes, released and grown sites
(over both rows), displaced cells and the largest displacement of that one
place() call; `qor.domino_sizing.eco` sums them over the accepted sizing
trials.

Those transactions come from `custom_sizing.hpp` (`size_custom_placed`,
lhd `synth.place.domino_sizing`). `Options::custom_cells` (`custom_cells.hpp`,
internal, part of the environment key) lists same-interface variants of
custom sequential macros by family and rank; a swap declares the new
variant as a pin-for-pin clone of the instance's declaration, so every pin,
edge and pin attribute stays. After final legalization a critical instance
(negative output slack, in period terms: the evaluator divides a slack by its
capture fraction) steps up, its output is buffered only once its top
variant is in place or its up move was rejected, and an instance with spare
slack steps down. Each trial is one edit through `run_resize_trial` (the
resize engine's clone, ECO re-place and evaluate) with `multirow_eco`, and
is accepted when the evaluator's period (`Feedback::delay`, compared in ps)
is shorter and electrical violations are not worse, or is not worse and
`resize_accepts` holds; a `Multirow_failure` (`rejected_packing`) or any
other runtime error of the trial, such as an unmeasured period
(`rejected_failed`), rejects the trial and leaves the state untouched; only
a failure of the initial evaluation aborts sizing. `domino_sizing.json` and `qor.domino_sizing` list every
trial, the variant counts and the clock-capacitance change.

`terminal_legalize.json` records settings, bin capacities/areas, stage counts,
SAT outcomes, retries and displacement. `terminal_legalize_cells.tsv` records
critical membership, stage, slack and before/after coordinates. Normal placement
snapshots include the final-GP input and exact legalized output; fresh timing
appears in `legalize_timing.jsonl` as `global_placement` and `legalization`
records, allowing WNS/TNS comparison without another GP run. Pin alignment is a geometric access check;
locked coordinates alone do not guarantee timing or routing success.

`bazel run -c opt //tools:terminal_legalize -- config.json` replays a saved DEF.
The JSON requires `def`, `lefs` (array) and `output`; optional controls use the
unprefixed names above plus `bins_x`, `bins_y`, `target_density`, `backend`,
`sat_conflicts`, `sat_milliseconds`, `macro_halo_x_um`, `macro_halo_y_um` and
`timing`. Timing is a TSV with exact `instance:pin` or `instance/pin`, slack in
ps, and clock flag 0/1. Use a fresh output directory for each configuration.
The production pipeline obtains its own final-GP timing; a replay needs matching
timing data for critical protection.

See [the terminal legalization task](../../todo/livehd/3s-terminal-legalize.html)
for evaluation protocol, measured regressions and remaining routed acceptance.

The saturated-maze regressions use four- and eight-track channels with shared
corner cells and independently shuffled initial positions. They verify full
channel utilization, every alignment, original-bin membership, and absence of
cell/corridor conflicts. A fifth path in a four-track maze must reduce selection
without timing out. Run `bazel test -c opt //pass/place:terminal_legalize_test
--test_filter='TerminalLegalize.*Maze*'`; `parallel_maze.svg` in the undeclared
test outputs shows the actual before/after placement. See the
[generated maze](../../todo/livehd/3s-terminal-maze.svg).

Optional `padding_max_extra_rows=N` compares padded and unpadded destinations for
unprotected stage-5 cells. It drops padding only when doing so saves more than N
row heights of displacement, or makes an otherwise exhausted search succeed.
The hard bin cap and previously reserved neighbors remain enforced. Critical
cells and accepted local pairs are unaffected. Reports count relaxed cells and
record each cell's actual padding; this experiment remains off by default.

Optional `remainder_wire_weight=W` adds W times incident timed-data-net HPWL to
stage-5 Manhattan displacement cost. Clocks and supplies are excluded. Only cells
that cannot retain their original coordinates are considered; accepted critical
and local-pair locks are unchanged. Pin offsets, row mirroring, multiple pins on
the same cell, and current positions of already installed neighbors enter the
cost. The geometric candidate search uses convex coordinate minima and safe
lower bounds; hard bin capacity, padding and corridor checks still apply. This
is a sequential relocation heuristic, not a global HPWL optimum or a timing
model. Zero retains the displacement-only behavior.

Timing and repair callbacks receive current mapped nets as well as component
geometry, including after FP topology refreshes. Intermediate frames reconstruct
connectivity from the placement view before automatic clock/reset classification;
otherwise reset cuts would disappear while the placement objective still ignored
those nets. Clock launch/capture timing and functional data paths remain timed.

FP and macro IO adjustment are enabled by default: `synth.place.fp.pin_adjust=true`
and `synth.place.macro.pin_adjust=true`. Explicit `false` disables either phase;
FIXED pins remain fixed, and final placement retains the selected fixed IO frame.
A supplied previous/ECO frame does not re-enter fresh FP or macro IO adjustment.
The default update interval is 50 iterations, step is 0.01 of perimeter and
minimum relative improvement is 0.001 in both phases.

### Density-warp targets (experimental, opt-in)

`synth.place.fp.warpspace=false` and `synth.place.macro.warpspace=false`
independently enable density-warp guidance. Neither changes the final legalizer.
The shared in-memory engine (`density_warp.cpp`) is also used by the standalone
`//tools:warpspace` density-grid mode. It integrates actual standard-cell overlap,
reserves the union of macro bodies, blends original and 32-bin-smoothed free-area
density equally, and solves a boundary-fixed, no-fold mesh. Physical BLOCK macros
(including native memory proxies and black-box memory models) supply samples;
fixed macros reserve space but receive no pull.

FP samples its provisional physical coordinates after FP GP, before snapping.
Each distinct movable macro belongs to its deepest centroid candidate; stable
candidate order breaks equal-depth ties. Each owning leaf averages its vectors.
Ancestors average contributing leaf movements using sums/counts, not child
averages. All target additions use the same original target snapshot; descendants
never receive a second ancestor translation. Existing snap and frozen-target
handoff then run normally, including the snap-disabled path. Cell coordinates,
membership and physical connectivity are unchanged by the target shift.

Macro GP replaces absolute virtual center targets at safe iteration boundaries,
independently of STA. The native optimizer holds them between refreshes, adds a
separate bounded distance-pull objective, and restarts momentum after replacement.
The force uses the ordinary macro-force reference and retains displacement
magnitude using a shared macro-length reference. These pulls never enter physical nets,
RUDY demand, STA, DEF, or final-cell placement. A topology replacement drops anchors of merged members and preserves untouched
macro anchors until the next scheduled refresh. Solver failure clears the affected pulls atomically; FP
failure retains the original target snapshot and reports its reason.

Each stage has `.warpspace.bins=16` (2..64), `.iterations=2` (1..6), and
`.seconds=3` (per-axis solve limit, up to 30). The filter remains 32 bins,
independent of solve-grid resolution. FP `.weight=1` scales target movement;
macro `.weight=0.7` is a constant relative pull strength (both 0..1). Macro
pull strength uses a common preconditioned wirelength/density reference and
shared distance normalization, so equal anchor offsets have equal pull-only
center response across memory sizes. It is independent of the optional local
cell-density objective.

Macro `.start_stability=0.20` opens a one-shot gate once the mean of the last
`.stability_window=10` absolute macro-center step sums is below 20% of the peak
in the first 50 GP steps. It cannot start before the first 50 reference steps.
Once open, `.refresh_interval=25` refreshes the target field every 25 GP steps
at constant strength until GP ends. Subsequent stability changes never reduce
strength or suspend refreshes. These parameters are configurable. The trace
records the measured stability for inspection; it does not adapt strength.

With `.refresh_interval=0`, the older `.updates=10` attempt budget determines
cadence. `.start_stability=0` selects `.warmup=0.1` instead of the motion gate.
The alternative `.once_after_density=true` mode requires `.start_stability=0`.
Early convergence is never delayed to meet a quota. A target refresh at the
final GP step is skipped because no subsequent optimizer step would consume it.
For macro GP only, `synth.place.macro.filler_fraction=0.125` selects particles
with width and height one-eighth of the smallest physical memory by body area.
Macro halos are excluded from this size reference. These particles bypass the
cell-resolution bin cap while keeping the existing total filler area budget;
`virtual_filler_max_count` can still enlarge them if explicitly requested.
`0` preserves cell-derived sizing. The option does not enable fillers: use
`synth.place.virtual_fillers=true` for the experiment. FP and final cells retain
their existing cell-derived particles. Particle dimensions/count are reported in
QoR, and the macro fraction enters reuse keys.

`macro_warp_stability.jsonl` records the constant weight, refresh interval,
observed window mean and first-50 reference. Replay and stability plots expose
the same trace. The experimental strength-reduction and upward-ramp controls
have been removed. FP runs once. Zero macro weight skips field work and
optimizer restarts.
All enabled policy values and algorithm version enter placement
reuse keys; disabled stages do no field work.

`fp_warp_field.json` reports samples and transported-field diagnostics;
`fp_warp_targets.tsv` records targets before warp, after warp and after snap,
including hierarchy sums/counts. `macro_warp_<attempt>.json` records absolute
endpoints, displacement, weight, status/time, density peak/p99/overflow, original
grid arrays and conserved mass/occupied area. `macro_warp_summary.json` records
actual attempts/successes, completed GP iterations, early termination and total
callback time. A refresh at the final iteration is skipped because no optimizer
step remains to consume it. Sample coordinates and endpoints use native DBU; field areas use µm².
The adapter converts physical geometry to microns before solving so objective
scales match the standalone prototype, then converts displacement back to DBU.
Uniform power-of-two objective scaling bounds LP costs without changing relative
penalties; feasibility tolerances remain unchanged. Field predictions describe transported density, not legal rigid
macro placement; actual QoR must be measured from the completed stage.

The optional macro `.warpspace.once_after_density=true` policy keeps its normal
warmup but waits for native GP's physical density overflow to reach
`stop_overflow`. It generates one set of absolute anchors and holds them for the
remaining solve. A first refresh requires at least twenty iterations left;
convergence then waits twenty optimizer steps after the change (the overall cap
still applies). If density is reached too late, no anchors are generated.
The summary reports the anchor iteration and number of iterations that consumed
it. This avoids continually chasing a changing field and remains independent of
STA checkpoints. The repeating policy remains the default when enabled.

`synth.place.legalize.warpspace=false` independently controls standard-cell target
warping inside terminal legalization. Critical and local-pair stages run first
and lock their accepted poses. One density field then supplies preferred sites
for only the remaining cells; fixed macros, critical cells, and accepted pair
endpoints never move. The field includes all current cell demand and macro halo
reservations. `.weight=1` scales displacement (0..1), and `.bins=16` chooses the
solve grid (2..64); `.seconds=3` is the per-axis solver budget (up to 30). Actual geometry is always produced by the existing full-core
candidate search with padding, corridor exclusions and hard bin capacity.
The displacement objective and its pruning bounds consistently use the new
preferred sites; original GP sites remain the reference for SAT domains and
movement reporting. `remainder_wire_weight` still trades displacement against
incident data-net wirelength. A failed field retains original preferred sites
and reports its reason; an exhausted legalizer still fails atomically.
`terminal_legalize.json` includes field status, sampled-cell count, runtime and
maximum target displacement. Density transport is guidance only: critical cells
and physical macro bodies stay fixed even though the field predicts transport.

The final-cell handoff reconstructs a fresh placement view from the fixed macro
and IO frame. FP centroid targets and macro warp pulls end with `macro_plan`;
they are not saved as final-cell objectives or GP continuation state. Final
legalizer remainder warping remains an independent option.

### Fixed-wire handoff foundation

`//pass/place:power_grid` contains the native metal-planning stage and the
`pg_asap7` strategy of the ASAP7 technology profile (`inou/lefdef/tech_profile.cpp`),
transcribed from pinned ORFS profile data. `//tools:power_grid`
reads repeated `--lef` inputs and `--def`, and writes a JSON metal plan with
`--output`; `--profile` accepts `pg_asap7` or `auto`. It derives followpin widths
and polarity from one-row standard-cell PG pins and row orientations (a
multi-row master, such as a two-row domino abstract, never defines a row's
rails; its parity is the legality check of `pg_rails.hpp`), merges shared
rails, and clips stripe spans around macro halos on obstructed layers. Short
row segments missed by periodic vertical feeders receive supplemental feeders
using the profile width and spacing, on the manufacturing grid. Feeders must
fit fully inside the rail and clear macro obstructions; an unserviceable segment
fails explicitly. These additions precede channel cuts. Export verifies that
each supply's grid is connected, including when channels are disabled.
Rows require enough die inset
for the full edge-rail width. The profile follows ORFS's unsnapped stripe offsets.
The JSON remains an intermediate metal plan. Experimental `--def-output`
additionally builds the PG: LEF-derived via arrays,
stack landing patches, enclosure-driven stripe extensions/trimming, and PG
terminals are emitted as DEF VIAS/SPECIALNETS. The result requires independent
OpenROAD placement, connectivity, DRC and IR checks. Fixed macro PG contacts
are transformed from LEF geometry, connected through the profile's macro layer
pairs, and kept immutable. Complete via stacks are preflighted before emission.
A macro-pin crossing that is too small for a supported via is skipped; failed
fits are cached, while unsupported-rule errors still fail the export. Every
declared macro PG pin must have a grid
connection; overlapping opposite-net contacts and virtual proxy masters are
rejected. Experimental profile channel patches are available through
`synth.place.pg.channel.patches` (empty by default), `Pg_profile::channels`,
and the PG audit tool's repeated
`--channel-skip layer,xl,yl,xh,yh` / `--channel-gap layer,xl,yl,xh,yh` arguments
(coordinates in microns). A skip covers the full stripe width and extent; a gap
covers the full width and removes only its specified longitudinal interval.
Followpin layers cannot be patched. Cuts happen before via generation, and
export rejects undersized remnants, illegal gap spacing, disconnected grid
islands, missing supply terminals, or wire/via pads that re-enter a channel.
Reference profiles retain an empty channel list. The audit JSON reports removed
planned stripe length; it is not a measurement of freed signal-track capacity.

The placement option accepts semicolon-separated entries:
`--set 'synth.place.pg.channel.patches=gap,M6,2,6.039,4,6.327'`.
It requires final full placement, fixed taps and an enabled native grid.
The physical cache and placement identity include the ordered patches.
For channel-only comparisons, use separate fresh workdirs (or identical input
cache snapshots) and verify equal rows, placed components and signal nets before
routing. With IO resynthesis enabled, a reused workdir carries learned timing
and wire-load data into mapping even when a changed placement option misses the
completed-placement cache. Such a miss alone does not give a matched baseline.
`power_grid.json` schema 3 records each patch, the profile's supply nets with
their cell pin names (`supplies`, power first), and marks channel IR verification
false; native connectivity checks do not establish an IR bound.

The report's `channel_accounting` object records removed planned stripe length
per layer and removed/added via stacks by lower:upper layer pair. Stack identity
includes the supply net and origin: moving a stack counts as a removal and an
addition. With channels enabled, accounting generates an unpatched reference
on the same input; no reference grid is rebuilt when channels are disabled.
Freed signal tracks remain null until measured with signal spacing and occupancy.

Freed-track reporting,
matched downstream routing and bounded IR-drop acceptance for channels remain
pending. The profile/audit API does not constitute a validated channel policy.
Full grid acceptance is still pending.
`--set synth.place.pg=auto` (or `pg_asap7`) enables the experimental grid
export in `lhd synth`; the default is `none`. It requires
`--set synth.place.handoff.taps=true` and emits PG only after final legalization.
An automatically sized frame adds die margin around the core for edge rails;
explicit die/core frames must supply sufficient margin themselves.
`power_grid.json` records per-layer wire length and via counts. Connectivity
is reported as unverified until checked independently, rather than assuming
that successful generation proves it.

The PG via builder supports ordinary cut spacing and LEF58 tables whose entries
all use DEFAULT, plus the enclosure forms required by ASAP7. Unsupported table
forms fail explicitly. This constructor is not the signal-routing rule oracle.

`inou/lefdef/routing_rules` supplies rectangular signal-metal checks for width,
area, minimum size, manufacturing-grid alignment, spacing ranges and parallel-run
tables. `Signal_metal_rules` compiles the supported ASAP7 LEF58 spacing,
EOL keepout, convex corner spacing, width table, RECTONLY and right-way/on-grid
forms once. EOL spacing checks short facing edges; keepouts use the LEF's
backward/side/forward extension order, and convex-corner spacing applies to
diagonally facing corners. Parallel-edge trigger conditions are conservatively
assumed active, so missing neighbouring trigger context cannot admit an illegal
route. FIRSTLASTPITCH and right-way rules require actual DEF tracks; the checker does
not replace M2's multiple track patterns with a uniform grid. Unknown qualifiers
and minimum-step contours reject the candidate. These primitives do not certify
complete nets: same-net route contour unions, general via legality and
route-search integration still need implementation and independent validation.

`//tools:metal_rules --lef tech.lef --lef cells.lef --def frame.def
--shape "M2 1000 1026 1200 1044" --shape "M2 1000 1143 1200 1161"`
audits rectangle legality and different-net pair spacing in DEF DBU. Its JSON
explicitly distinguishes these primitive checks from complete-net certification.

`inou/lefdef/pin_access` enumerates M1-to-M2 access for placed standard cells
using supported single-cut LEF vias. The M1 landing must lie inside the existing
pin rectangle union; no M1 extension is introduced. A minimum-area M2 escape
rectangle must be on an actual track and clear of the supplied foreign-net
metal/cut occupancy. Mirroring, manufacturing grid, missing rules and bounded
candidate enumeration are handled explicitly. The caller must supply the full
nearby occupancy; an empty list is valid only when the frame has no relevant
obstacles. This is an access candidate generator, not a complete-route verdict.
`//tools:pin_access --lef tech.lef --lef cells.lef --def frame.def --instance u0`
builds diagnostic occupancy from the frame and reports points/rejections per
signal pin. Repeat `--instance` to audit multiple cells with one shared occupancy
index. `--signal-wires exclude` removes only signal-wire obstacles for a
read-only baseline diagnostic; PG, pins and obstructions remain. The JSON
explicitly labels this exclusion, and it does not authorize route acceptance. It now uses the shared spatial index; wider master/orientation
checks and complete route-search integration remain pending.

`Pin_escape_guard` preserves an initially legal M1-to-M2 escape for each
supported unrouted standard-cell pin while accepting signal traces. It indexes
M2 escape rectangles, bare M2 via landings, directional router probes and V1 cuts, applies the same
spacing rules as access enumeration, and rejects a trace that consumes a pin's
last surviving candidate. Bare landings are checked separately because their
shorter edges can violate EOL rules in downstream whole-cell pattern checks
even when the extended escape is legal. Directional probes preserve at least
one preferred-direction continuation matching OpenROAD standard-cell access
checks (three wire widths plus the outer half-width end cap). Each direction
is consumed independently; a legal short landing alone is insufficient.
For cells with an initially verified compatible combination, affected-cell checks
also preserve a joint assignment of bare via landings, including inter-pin metal
and cut spacing. A bounded search tries at most 10,000 choices and observes the
route deadline; exhaustion rejects the trial. Cells without a verified initial
combination remain outside this additional guarantee. These pairwise checks do
not replace OpenROAD's full pattern generation or independent DRC.
Accepted traces consume candidates cumulatively; rejected trials do not mutate
reservations. A completed net retires its own reservations while its real metal
remains in routing occupancy. Setup is bounded by the stage deadline and the
existing access enumeration budget; checks share the per-net deadline. Runtime
is reported as `pin_escape_setup` and `pin_escape_check`. Pins without a supported
initial candidate are outside this guard's coverage, so independent OpenROAD
pin-access and final DRC validation remain required. Rebuild reservations when
cells or supply geometry change; deferred channel removal uses the complete
initial grid as a conservative baseline.

`inou/lefdef/routing_occupancy` seeds pins, obstructions, terminals and all
signal/PG wire and via shapes with net ownership. Queries retain exact DEF
geometry, return deterministic shape IDs, and use rule-derived clearance.
Unowned blockages are never skipped as same-net geometry, and cut spacing still
checks same-net cuts. Long narrow shapes are indexed along their short axis,
so a local query does not scan every power rail on the layer. Broad shapes and
wide queries retain bounded-index fallback paths. Candidate nets can be validated
before insertion, so failed trials do
not change existing occupancy. Unsupported pin polygons become conservative
full-cell layer blockages rather than vanishing from the index.

The physical DEF reader/writer preserves Manhattan `FIXED`, `ROUTED` and `COVER`
paths, explicit endpoint extensions, oriented vias, rectangular and generated
DEF via definitions, routed SPECIALNETS, and POWER/GROUND pin uses. Unsupported
route features fail explicitly rather than disappearing on export. This is the
serialization foundation for `todo/livehd/3x-reroute.html`; the automatic
pre-router, aligned power-grid generator and their acceptance checks are still
in development.

`//tools:routing_geometry` is a read-only audit tool: pass repeated `--lef`
arguments, `--def` and `--output` to dump rectangular signal, special-net and
via geometry as JSON in the DEF's DBU. It resolves technology vias, expands DEF
via arrays, applies via orientations and explicit wire-end extensions, and
rejects geometry it cannot represent. This supports shape comparisons against
OpenDB; it is not a spacing, pin-access or connectivity verdict. LEF fixed-via
and generation-rule data are retained separately from DEF-local definitions.
Unsupported via/cut features are recorded for rejection by the routing oracle.
DEF GCELLGRID records are also preserved for downstream standalone DRC checks.

The LEF model retains ordinary spacing, range and EOL conditions, parallel-run
spacing tables, minimum area/width/size and minimum-step rules. LEF58 property
text is retained without treating an unparsed extension as supported.

`cut_macro_rows` and `//tools:cut_rows` provide the global-halo row-cut stage for
handoff parity work. The audit tool takes repeated `--lef` arguments, `--def`,
`--output`, and explicit `--halo-x-dbu`, `--halo-y-dbu`, `--min-row-width-dbu`
values. It cuts outward to sites, enforces the endcap segment/step requirements,
and preserves rows containing fixed core cells. Optional `--tracks` applies the
literal platform `make_tracks` file for a combined row/track parity audit.
Optional `--endcap-master`
adds left/right endcaps using `row_endcaps`: a single-height, whole-site master
on contiguous horizontal rows, with mirrored right edges when supported by
master symmetry. Joined row segments have no internal endcaps. Unsupported
row geometry or symmetry and instance-name collisions fail explicitly.
Endcap names are deterministic LiveHD names; OpenROAD traversal indices are
not reproduced. This primitive does not choose specialized corner/top/bottom
masters or check existing component overlaps.

Optional `--tap-master` and `--tap-distance-dbu` insert interior taps with
`row_taps` after the endcaps. It reproduces the width-quantized staggered pitch,
denser horizontal boundary rows (including macro holes), fixed-obstacle
relocation, and the one-site-gap policy. Movable cells are ignored, as in
OpenROAD; this must run before final legalization. The current tap generator
accepts a single site with contiguous N/FS rows and fails on unsupported
geometry. All generated endcap/tap components are locked internally and emitted
as DEF `FIXED`. The audit tool does not enable the aligned handoff. Tap sites must be reserved before final cell
legalization: inserting taps afterwards can overlap movable standard cells.

`--set synth.place.handoff.taps=true` enables the technology profile's tap
preparation in a fresh final solve with terminal legalization. The profile
(`physical::Tech_profile`, detected from the LEF or named by `synth.place.pg`)
supplies the tap master, tap distance and row-cut halo; ASAP7 uses
`TAPCELL_ASAP7_75t_R`, 25 µm and 2 µm. Real fixed physical-only components
are inserted into the terminal legalizer's frame, so cell legalization accounts
for their occupied sites. They remain outside the logical graph and signal
connectivity. Final DEFs and legalization snapshots include the cut rows and
fixed components; `handoff_taps.json` records the generated counts. Physical taps
may occupy a macro's larger placement halo, while logical cells still respect
that halo and all component bodies must be disjoint.

This mode changes the placement environment key and currently disables the
inner block-reuse cache. Explicit previous-frame/ECO placement is rejected for
this mode until cut rows and physical-only components can be preserved through
that path. The option defaults to false. Power-grid generation requires the
separate `synth.place.pg` option above. Signal pre-routing and complete
aligned-handoff acceptance remain pending.

For a DEF containing fixed signal paths, source `pass/place/preroute_handoff.tcl`
in OpenROAD and call `livehd::preroute::protect_fixed_routes` after import and
before any repair. Use
`livehd::preroute::has_fixed_taps $::env(TAP_CELL_NAME)` to decide whether to
skip `TAPCELL_TCL`; it checks the platform master and rejects movable tap
instances. It does not certify the pattern or replace `check_placement`.
`protect_fixed_routes` derives protection from encoded path types, rejects mixed
fixed/non-fixed paths and non-FIRM endpoint instances, then marks the affected
nets and instances `dont_touch`. Instance protection prevents resizing through
another attached net. It validates all endpoints before changing protection,
and sourcing the helper alone does nothing. This lock check does not replace
the independent pin-connectivity, spacing, grid or timing checks required by
3x-reroute.

The handoff marks only instance locks it creates with
`livehd_preroute_buffer_inputs_v1`. The HighTide fixed-endpoint buffering adapter
uses this permission to buffer ordinary signal inputs while keeping cell masters,
positions and fixed-net connections protected. It restores instance protection
before buffer legalization, including on exceptions. Pre-existing user
`dont_touch` instances do not receive the permission. Use the matching OpenROAD
patch when consuming this handoff.

The repair wrappers also leave ordinary internal signal nets editable when their
driver has a handoff-owned lock. Buffering their loads keeps that driver's output
connected to the original net; the driver's instance remains protected. Nets with
top-level ports remain excluded because preserving the port net name may require
reconnecting the driver. Fixed wiring, user-owned locks, clocks and special nets
retain their protection.

The CTS wrappers also release only handoff-owned instance locks. User-owned
`dont_touch` remains set during clock edits; an incompatible clock operation
must fail instead of silently bypassing that constraint. Handoff-owned locks
are restored on success and failure, and successful clock edits are checked
for changes to fixed wires, endpoint geometry and fixed-net connectivity.

Native pin-access checks apply to the placement used when routing. Locking the
routed endpoints does not protect unrelated movable cells: downstream repair
can move or flip one of those cells beneath an unchanged fixed trace and remove
its access. A clean pre-CTS pin-access check therefore does not replace the
post-CTS check. Downstream placement must account for immutable routing when
moving or resizing cells.

During downstream CTS or repair, an experimental reservation step can
keep movable cells away from fixed low-layer wires without adding instance locks.
Source `pass/place/preroute_reserve.tcl` and call
`livehd::preroute::reserve_fixed_route_space` before that stage's next detailed
placement. It projects fixed wire and via-landing geometry on the two lowest
routing layers (M1/M2 on ASAP7) onto placement
rows with a four-wire-width margin, rounds outward to sites, and creates hard
placement blockages except where existing FIRM/LOCKED bodies already occupy
those rows. It changes neither instance statuses nor positions. Movable cells
can initially overlap the new blockages: downstream detailed placement must
clear them before placement validation, pin access or routing. This is not an
extra legalization step in the aligned pre-CTS import.

The reservation record persists in ODB; repeat calls validate it instead of
rebuilding it around a changed placement. Missing or changed fixed bodies and
blockages are errors. On a fresh DEF import, derive the reservations again at
the same downstream stage. The margin is conservative geometry, not a
pin-access or DRC proof; full CTS-to-route and extracted timing validation remain
required. Earlier experimental checkpoints that also locked movable corridor
occupants do not meet the endpoint-only lock policy in 3x-reroute Q8.

When using reservations, create them before capturing the downstream protected
state, then call `protect_fixed_routes` again. `_fixed_routes` includes the
reservation's already-fixed bodies as well as routed endpoints; a reference
captured before reservation creation omits those bodies. Across checkpoints,
validate both the original routed endpoints and the recorded reservation bodies;
do not replace a mismatching reference with the current placement.

For an independent import check, also source `pass/place/preroute_validate.tcl`
and call `livehd::preroute::check_fixed_routes /path/to/fresh.drc` in OpenROAD.
It reads OpenDB wire/via geometry and transformed cell pin rectangles directly,
requires every fixed net's metal and endpoints to be connected, then runs
`drt::check_drc`. Missing or nonempty DRC reports fail the check. The supplied
report path is overwritten. Polygon/via pins and fixed IO nets are unsupported
and fail explicitly. Pins with several rectangular shapes are treated as one
logical terminal; coincident metal on different layers connects only through
an explicit via cut, and point-only contact does not connect.

After CTS, also inspect the router's DRC report: the default standalone
`drt::check_drc` can omit nondefault-rule (NDR) spacing when automatic NDR
tapering is enabled. HighTide's `openroad-drc-explicit-ndr.patch` adds
`drt::check_drc -check_ndr -output_file /path/to/ndr.drc` to check that spacing
explicitly against the current OpenDB geometry. This option changes only the
temporary checker configuration; it leaves the live router and subsequent
default checks unchanged. The strict check can additionally flag tapered
geometry, so its marker set need not match the router's report. Investigate
those differences; an empty default check does not waive a router NDR marker.
Post-route acceptance still requires both the router report and the independent
geometry check to be clean.

Signal `RECT` patches use offsets from the preceding route point and the current
layer, including layer changes through a via. The native parser, writer,
geometry decoder and connectivity checker preserve them. OpenDB's path iterator
omits these patches, so the independent checker also reads them through
`dbWireDecoder::getRect`. The Tcl binding must return its four offsets; an older
binding fails explicitly when a patch is encountered. HighTide's
`openroad-def-rect-bindings.patch` provides that binding. Patch geometry support
also supports maze via landings selected from the LEF width table. Wider
landings widen their complete connected straight run, since ASAP7 `RECTONLY`
forbids the width transitions made by a local patch. The complete widened
rectangle is checked again against existing and trial metal. Routed RC supports patches
centered on a preferred-direction centerline, unions overlapping metal and
splits it at width changes. Relative to the layer's nominal-width RC, resistance
scales inversely with width and effective ground capacitance scales with width.
This is an area-equivalent capacitance approximation, not width/coupling
extraction. Offset patches and patches narrower than nominal metal fail
explicitly; independent timing comparison remains required.

Use the HighTide pin-access patches for fixed-wire handoffs. An access point
matching an existing same-net fixed via reuses that via without adding a
duplicate cut or a hypothetical three-width escape segment. The actual fixed
connection may already turn onto the next layer through a shorter legal
landing. New access vias still require directional escape checks. Whole-cell
patterns retain the imported fixed geometry, and the independent full-design
DRC check still checks fixed/fixed interactions; pin-access success alone is
not a geometry or connectivity certificate.

Run this in a separate checker database: when no GCELLGRID exists, it adds an
ancillary grid with at least 64 bins per axis and at most 10um pitch for the
DRC engine. This bounds the geometry retained by each checker region on large
dies; an existing routing grid is preserved. It does not change metal, instance
placement or protection flags. `check_fixed_connectivity` is also available
without DRC or grid creation. These checks complement `check_power_grid` and
the post-route invariance/extracted-timing checks; they do not establish timing
or IR-drop acceptance. Revalidate the independent engine with known open,
short and spacing controls when changing the pinned OpenROAD version.

During CTS, use `livehd::preroute::repair_clock_inverters` and
`livehd::preroute::clock_tree_synthesis` in place of the corresponding OpenROAD
commands (the latter takes the same arguments). OpenDB instance `dont_touch`
otherwise forbids reconnecting a protected flop's clock pin. These wrappers
temporarily release only the derived endpoint instance flags; FIRM placement
and fixed-net `dont_touch` remain active. They restore instance protection even
on errors and require unchanged masters, origins, orientations, placement
statuses, fixed-net connectivity and exact encoded wires after a successful
clock operation. Referenced layer widths, width rules and via rectangles are checked too:
unchanged object IDs cannot hide an in-place geometry edit.
Do not use this release around placement or timing repair.
Discard a failed CTS database; these wrappers restore protection, not topology.

Use `livehd::preroute::repair_design` for downstream electrical repair and
`livehd::preroute::repair_timing` for setup/hold repair. Each keeps
the fixed wires and endpoint instances protected, and temporarily marks their
other incident signal nets `dont_touch` when they touch a protected output or
an input without the handoff's buffering permission. Ordinary inputs with that
permission remain eligible for the scoped buffer-insertion adapter. Previously
unprotected nets are restored even on errors; existing net protection is retained.
Continue to check
design rules and timing after routing; protection does not prove either passes.
Supply nets remain editable for other cells' PG connections; the protected
endpoint instances themselves stay locked throughout repair.
Use the HighTide OpenROAD build with the clone-protection patch: gate cloning
must reject protected input nets and moved loads before creating the clone.
Checking only the driver's output net is insufficient, even with the incident-net
guard, because cloning adds new terminals to the original input nets.

The HighTide incremental-usage patch is also required for mixed fixed-wire and
global-guide designs. FastRoute reinitialization must restore occupancy for
guide-only nets while retaining the fixed-wire obstructions. Restored routes
must use the matching resource-release path when repaired; their newly created
FastRoute trees are initially empty. A no-op incremental route must preserve
every edge's usage. Passing that invariant does not establish routability.
The layer-overflow patch also includes overloaded individual metal layers in
congestion-neighbor searches when aggregate planar capacity is still available.
Immutable wires remain excluded from resource release and rerouting.
The mixed-guide reload patch recognizes existing signal wires when checking a
saved global-route database: these nets need no guides, while every remaining
multi-terminal net still requires them. Empty wires and via-only access stacks
do not satisfy that exemption. The reload check is exercised in a fresh process,
matching the global-to-detailed-route handoff; it is not a substitute for the
independent connectivity and DRC checks on fixed traces.

Channel IR acceptance uses an explicit budget:
`--set synth.place.pg.channel.max_ir_drop=0.001` permits at most 1 mV of
additional drop on either rail or any matched terminal. The empty default leaves
the budget unconfigured. The setting is cached with placement and written as
`channel_max_ir_drop_v` in `power_grid.json`; generation still reports
`channel_ir_drop_verified=false` because geometry generation does not run IR
analysis. No geometry or placement default changes with this policy setting.

Run `bazel run -c opt //pass/place:channel_ir -- manifest.json --output verdict.json`
after matched OpenROAD `analyze_power_grid -voltage_file` runs. The manifest has
`reference` and `candidate` objects, each with `power_grid_report`, a `rails`
object with one CSV path per supply net recorded in the reports' `supplies`
(`VDD` and `VSS` for ASAP7), and an identical `analysis` object.
Paths are relative to the manifest. Record the actual `liberty_sha256`,
`sdc_sha256`, `corner`, `source_model`, `power_model`, and `voltages_v`
(`{"VDD": "0.7", "VSS": "0"}`, for example) in that analysis object. The caller
must establish that these descriptors describe the runs; the checker matches
recorded context and hashes the input artifacts, but cannot reconstruct how a
CSV was produced. The reference must have channels disabled, and the candidate
must carry a finite nonnegative budget and the same profile, supplies and revision.
Both reports must be schema 3 or later; older reports lack the supply records
and must be regenerated.

The checker requires matching terminal identities, layers and coordinates,
rejects missing/duplicate/nonfinite data, and compares decimal CSV voltages
without adding tolerance to the configured budget. It returns nonzero for a
budget violation or invalid evidence and writes a separate verdict. A passing
`channel_ir_delta_within_limit` only verifies this IR comparison: connectivity,
DRC, long-net timing and routed-wire measurements remain separate acceptance
gates. Keep the generated reports unchanged as provenance.

Run OpenRCX extraction on a disposable copy of the routed database. Its wire
ordering pass rewrites encoded paths, including FIXED status and patch records;
that working database must not replace the validated physical handoff. Save the
original routed ODB/DEF first, extract and write SPEF in the scratch process,
then read that SPEF into a fresh process containing the original routed database,
Liberty and SDC. Check exact FIXED wire/endpoint state across SPEF import and
report setup and hold timing there. Retain the original routed database for
physical exports and independent DRC; do not weaken the wire-invariance check
to accommodate extraction's internal re-encoding.

For the native ASAP7 power-grid handoff, final placement legalizes movable IO
pins before final RC estimation. Left/right pins use preferred M4 tracks and
top/bottom pins use preferred M5 tracks. Rectangles meet the layer width and
minimum-area rules, stay inside the die, and are checked against existing metal,
including the power grid and previously assigned IO pins. Fixed, locked and
supply pins retain their input geometry. Missing tracks, unsupported rules or
exhausted legal slots fail the handoff rather than emitting inaccessible pins.
This legalization is enabled with the native grid; ordinary placement defaults
are unchanged.

### Routability evaluation

The internal `Options::retain_preroute_design` handoff retains a
`Result::preroute_design` only for completed final placement. It carries the
physical DREAMPlace design and pin-indexed labels, including the existing
clock/reset demand masks. Ordinary placement does not retain this copy.
The pre-router checks candidate demand endpoint identities against the DEF
and RC endpoint sets before it can remove that net from remaining RSMT demand.
Rebuild the snapshot after netlist or placement changes; it is not a persistent
cache artifact.
Repeated exclusions use DREAMPlace's RUDY cursor to replay only affected bins
in original contribution order, including blocked-pin operations. Trial queries
leave the cursor unchanged; only accepted routes commit exclusions. If cache
preparation fails, the pre-router retains the uncached measurement path.
`bind_preroute` joins DEF, RC and demand nets by complete endpoint sets; array
order and DEF net names are irrelevant. It rejects stale or ambiguous views,
retains characterized tie exclusions, and accepts the host's expanded
`dont_touch` pin set. Clock/reset masks remain in the demand design.
The internal graph marker `attrs::preroute_exclude` excludes every incident
net of a marked physical cell. A marker on a graph input applies to its contents;
a marker on a Sub applies only to that occurrence. Flattening propagates the
marker without changing other instances of the same definition. Final placement
expands marked cells into protected endpoint identities in the snapshot. This
marker is a routing exclusion, not a synthesis optimization lock. The Slang
reader imports Verilog `dont_touch` and `DONT_TOUCH` attributes into this routing
policy (integer booleans or case-insensitive strings `true`/`false`, `yes`/`no`,
and `1`/`0`). This preserves routing exclusion through mapping; it does not
promise to preserve the original cells or prohibit synthesis optimization.
Invalid values and duplicate/conflicting aliases are rejected.
The explicit source attribute
`synth.preroute_exclude=true` requests routing-only exclusion through the shared
synthesis-policy path (a variable cone, scope, or instance). It has no run-wide
`--set` value and does not lock synthesis optimizations. Synthesis preserves excluded
signals and protected-cone boundaries as stable mapper output positions.
Readback marks the mapped nets between protected outputs and their external
input cuts; a collapsed output/input boundary stops the walk. Source-to-route
validation remains pending.
`attrs::preroute_net_exclude` marks a driver net without excluding the other
nets of its cell. Hierarchy alias resolution carries this marker, and final
placement resolves marked wiring drivers to physical endpoint identities.
Removing an identity buffer transfers its output-net exclusion to the surviving
driver. Buffer-tree repair and resize insertion carry exclusions onto the
surviving driver and inserted outputs; pre-route repeater materialization rejects
an excluded tree. Source annotations, mapping, and other rewrites still require
a complete propagation audit before source-level protection can be claimed.
The synthesis cache currently refuses reuse when either pre-body carries these
exclusions: its logic-equivalence proof does not compare physical annotations.
This also prevents stale protection after an annotation is removed. Unchanged
protected regions consequently miss until annotation-aware correspondence is
implemented.

Host-resolved protected pin identities can also be stored in the final
`Preroute_design` snapshot. These exclusions survive speculative repeater
snapshots and apply independently of caller-supplied binding flags; neither
ordinary routing nor repeater insertion may override them. Binding rejects
empty or stale protected labels. The host must still resolve source annotations
through mapping and final repair; storing this set does not implement that
source-level propagation.
The snapshot takes ownership of the completed placement arrays without a
second full design copy. `preroute_hold_margin` queries the timing session's
Liberty model for twice the specified HOLD_BUF arc delay at an explicit nominal
slew/load; a missing or invalid characterization fails instead of yielding zero.

The physical LEF importer rejects unsupported pin and obstruction `PATH`/`VIA`
geometry (including iterated forms), masked geometry, and geometry-specific
`SPACING`/`DESIGNRULEWIDTH` constraints. These shapes and rules must not silently
disappear from the occupancy model, even when the same pin also has supported
rectangles.
Polygon pins remain unavailable for access; their bounding boxes are retained
as unowned obstacles, including portions outside the cell boundary. Polygon
obstructions retain those bounds too. Iterated pin and obstruction geometry is
rejected until its complete repetition extent can be modeled.

`synth.place.preroute=true` invokes the initial pre-router after final
placement and accepted repairs. It requires `synth.place.final=true`, timing,
`synth.place.handoff.taps=true`, and a native power grid (`synth.place.pg=auto`
or `asap7`; legacy `pg_asap7` remains accepted). The default remains disabled.
The current search supports M2 spans and multi-pin M2/M3 trunk patterns,
then a bounded A* M2–M7 maze. Multi-pin nets use a FLUTE topology with one
legal access selected per pin and Steiner junctions snapped to nearby legal
M2/M3 intersections. Up to eight nearby tracks per axis are considered for each
junction; junction trials and all branch searches share the net budget. The
current proposal does not backtrack access/junction choices or retry a failed
merged route. It supports up to 1000 distinct topology locations. Placement RC
and pre-routing share `steiner_topology` and its one-time FLUTE initialization.
The maze uses literal preferred
tracks, minimum-area landings and an endpoint box expanded by one span (at least
four track pitches), clipped to the die. Each layer retains its preferred track
set; transitions use supported adjacent-layer LEF vias at intersections of the
two track sets. Signal and PG vias share cut-class, all-DEFAULT spacing-table
and enclosure parsing in `inou/lefdef/cut_rules`; signal vias additionally require
a single-cut class. Signal EOL enclosures apply only below their strict metal-edge
width threshold; fractional-DBU thresholds are compared without rounding. PG via
generation explicitly selects the conservative `pdngen` enclosure policy, which
retains EOL enclosure rules regardless of their width threshold to reproduce the
reference grid geometry. Unsupported
upper layers or missing vias stop the reachable
stack without approximating their rules. Pattern trials and maze expansions share
the work budget. The dispatcher reserves an equal share of remaining work and
access enumeration for each remaining strategy, carrying unused allowances
forward so exhaustive patterns cannot starve the maze. All stages share the
net deadline. The returned
path is merged into collinear spans, checked for new same-net metal/cut spacing
and complete connectivity before the caller evaluates RC, timing and capacity.
A failed final geometry check rejects the proposal; it does not publish a partial
route or rip up previously accepted nets.
Pins sharing an M2 track share one branch and one trunk via; every endpoint
must be connected, and all new cuts are checked against each other. General
multi-pin Steiner/maze routing and repeaters remain under development.

`preroute.setup_margin` is 20 ps; `preroute.hold_margin=-1` computes two buffer
delays. `preroute.hold_buffer=auto` selects the technology profile's hold
buffer (ASAP7: `BUFx2_ASAP7_75t_R`), at
`preroute.nominal_slew=20` ps and the buffer's own input capacitance (fanout one).
Explicit nonnegative hold margins bypass this characterization. Eligibility
checks timing on every connected data pin of each endpoint cell:
committing a route locks the whole cell, so unrelated data pins must retain
setup/hold margins too. The host's clock/reset masks remain excluded. These
pins are checked again with the proposed route RC and retained in the protected
timing set after commitment; rejection leaves the route and cell placement unchanged.
Search budgets
are `preroute.budget_ms=10` per net and `preroute.stage_budget_ms=10000` overall;
`preroute.bins=64` sets both axes of the shared capacity/RSMT grid.
The stage deadline is checked during occupancy construction, between capacity
bins, and periodically while scanning their obstacles. If setup expires, its partial indexes are discarded and the original
placement/wires are retained with `stage deadline` rejections; cleanup, required power-grid generation, and
physical-frame copies can still add time beyond the search budget.
`preroute.max_rows=0` and `preroute.max_sites=0` optionally cap the original
net's physical pin Y/X span in placement row/site units. Zero is unlimited;
positive bounds include equality. Oversized nets remain for OpenROAD, and
repeater splitting cannot bypass these caps. Reports record both settings and
span rejections.
`preroute.capacity=0.30` limits reservations to that fraction of usable tracks
in every layer and gcell (range 0..1). Remaining-net RSMT demand must still fit
alongside those reservations. The report records the effective fraction.
Capacity setup narrows each obstacle to the sorted track-center interval that
can intersect its clearance envelope, then applies the same exact spacing check
to those footprints. A track blocked anywhere in a bin remains unavailable for
the whole bin; the indexing optimization does not change capacity or margins.

Successful runs emit FIXED traces and endpoint cells in `out.def`, update the
graph's placement/RC attributes, and write `preroute.json` plus a `preroute` QoR
member. Report schema 2 includes mapped-net and search-attempt counts, accepted
fractions by pin count and estimated pre-route length, rejection totals, accepted
wire length per layer, via count and route-stage runtime.
The diagnostic `endpoint_locks` array records only changed component statuses
(and inserted components), with instance name, original/final status and an
`inserted` flag. Inserted components have a null original status. Matched controls
can use this provenance to preserve pre-existing fixed cells; production handoff
protection continues to derive from the DEF without requiring this report.
Capacity-rejected route entries include `capacity_detail` with the limiting
bin, layer/direction and track counts; the aggregate rejection reason stays stable.
Pin-access rejections identify the failing bin and its utilization in the same
field. This is the remaining macro-pin demand normalized by available bin area;
a remote macro-pin hotspot can reject a standard-cell-only candidate.
The report also includes `phase_runtime_ms` for snapshot copying, occupancy,
capacity, search setup, eligibility and search/timing. Repeater diagnostics report
`repeater_length_limit_um` (null when no finite threshold was derived),
`repeater_candidates`, `repeater_margin_eligible`, and `repeater_attempted`.
Candidates passed the structural and length filters; margin-eligible candidates
also passed the initial timing screen, while attempted candidates reached the
proposal search after the live timing recheck. A false
`repeater_classification_complete` means classification was skipped or its
deadline expired, so the counts do not establish full-design eligibility.
Attempted-net entries
include `runtime_ms` for completed remaining-demand, demand precheck, geometry,
capacity precheck, RC, capacity and timing checks. The `demand_precheck` rejects
remaining-demand failures before geometry search, without authorizing any wire
reservation. A failed geometry `capacity_precheck` avoids RC work; passing both
prechecks still requires the full remaining-demand and wire-capacity check
before acceptance. Per-attempt measurements are subdivisions of the search phase,
not additional runtime. The unchanged stage deadline includes setup. The denominator is the
mapped multi-terminal nets; physical singleton nets are not included. A net
rejected by eligibility or initial timing has `attempted=false`; a search with
no legal result still counts as attempted. Fractions with no attempts are null.
Per-layer lengths count explicit wire spans, excluding via landing pads. Runtime
includes capacity/demand construction and admission, but excludes host LEF and
Liberty preparation, final report serialization and the final timing refresh.
The completed physical cache fingerprints all pre-route sources and
options and stores the FIXED DEF, `preroute.json`, and `power_grid.json` with
the repaired graph. With incremental mode enabled, pre-routing uses this
completed-state cache even when IO resynthesis is disabled; restoring it does
not invent an IO-resynthesis report. Independent full-flow
cache replay and CTS/routing acceptance are still required by 3x-reroute.

For intermediate OpenROAD timing, the pinned HighTide build includes
`openroad-est-fixed-wire-fallback.patch`: global-route estimation otherwise
clears parasitics for FIXED nets without guides. The patch restores placement
estimates for those nets in both full and per-net estimation, while preserving
global-route estimates for guided nets. This is an intermediate approximation;
final timing must use OpenRCX extraction of the realized fixed wires.

Keep per-design parameter sweeps, measured QoR, performance profiles and routed
comparisons under ignored `repros/`, and share selected reports separately.
Evaluate density guidance against completed CTS, detailed routing and extracted
timing before choosing settings. A hard density cap cannot be lower than the
configured target. Pre-route density alone does not establish a routed gain.
FP, macro and remainder warps remain opt-in.

The automatic macro halo (`synth.place.halo=auto`) uses ten selected
placement-row heights; explicit micron overrides remain supported. FP and
macro IO bus adjustment default on.

### Experimental final-placement bus ordering

`place.region.busorder=false` is the default. Opt in with
`--set synth.place.region.busorder=true` (or `"region_busorder": true` in a
`physical_repair` configuration). Only the final cell stage uses it, between
ordinary GP and legalization; macro and IO positions stay fixed.

The region mesh (`Region_mesh`, built once per detail-final phase and shared
with boundary pins and pin alignment) comes from that run's cell coordinates, macro edges,
and IO bus spans. Cuts within `region.merge_um=5` um merge; regions over
`region.max_cells=10000` movable cells split along their longest dimension.
Pruning alternates H/V candidates sorted by their initial neighbor distance,
without reranking. The mesh then stays fixed; `region_mesh.json` describes it.

RSMT crossings supply soft adjacent-bit order constraints. Ascending and
descending orders both work, including gaps in bit indices. Each virtual crossing
is a supporting real pin coordinate plus an offset, so its force moves real cells;
there are no added physical cells, signal nets, capacitance or timing arcs. A
one-DBU order margin avoids making coincident crossings a zero-force optimum;
it does not reserve physical routing tracks.
Clock/reset masks apply. Ambiguous branched crossings are measured but do not
receive single-lane constraints. RSMT uses a deterministic HV realization where
its branch is diagonal; these are placement estimates, not routed wires.

Controls under `place.region.busorder.` (`physical_repair` JSON uses `region_busorder_` followed by the field):

| Option | Experimental default | Meaning |
|---|---:|---|
| `iterations` | 100 | Extra GP iteration budget |
| `refresh` | 20 | Rebuild crossing constraints between chunks |
| `weight` | 4 | Ordering strength; 0 is an extra-GP-only control |
| `max_displacement_um` | 20 | Movement bound, reduced for timing-weighted cells |

Each chunk starts with final wire smoothing and fresh optimizer state; existing
wirelength, density and routability objectives remain active. A snapshot must
stay within +2% physical HPWL and +0.01 density overflow of the input to be
accepted. Selection favors genuinely resolved initial bus/edge groups, followed
by lower ordering penalty. This is not a timing guarantee: compare actual
post-CTS routed timing, wire/via counts, DRC and hold results.

`busorder/report.json` records the mesh and per-chunk measurements. The initial
unordered bus/edge cohort is tracked after convergence and after legalization:
ordered percentage, still-unordered percentage, tied/ambiguous groups, and groups
whose participating bits changed or no longer cross the same edge. Ties and
missing crossings are not counted as untangled. Stage TSVs contain crossing
coordinates for overlays. The report also includes the overall current unordered
count, since new disorder can appear outside the original cohort.

### Experimental fixed boundary pins

`--set synth.place.region.boundarypins=true` enables a separate final-GP experiment
(default false), with `place.region.busorder=false`. The same independently built mesh
is used. All active signal nets are considered; clock/reset masks apply.

Each RSMT branch is cut where it crosses rectangle boundaries. Connected pieces
inside a rectangle become local placement nets, joined through fixed zero-area
terminals at the original crossing coordinates. Separate reentries into one
rectangle remain separate connected pieces. Exterior pieces retain real fixed
IO terminals. With `place.region.boundarypins.order=false` (default), crossing positions
are unchanged. There is no cell fence or clock/reset sizing. Nets above the bounded 1,000-pin routing limit remain unsplit and are
reported. Virtual pins consume no area and never enter STA, legalization, DEF or
LGraph; only real movable cell coordinates are copied out of the temporary model.

The default extra budget is 50 iterations (`place.region.boundarypins.iterations`).
`place.region.boundarypins.split=false` runs the matched extra-GP control without virtual
pins. Both use final wire smoothing, the same optimizer startup and a 20 um
movement bound reduced by original timing weights
(`place.region.boundarypins.max_displacement_um`). Pin coordinates and the partition
are frozen throughout this extra phase. Its last result is legalized normally,
including when placement metrics regress: this experiment measures pin overhead
and does not use the bus-order candidate selector.

`boundarypins/report.json` records the boxes, pin/net counts, build time, real-net
HPWL and density overflow before/after refinement and after legalization.
Temporary split-net HPWL is labeled separately and is not comparable to real-net
HPWL. `boundarypins/pins.tsv` gives the immutable boundary-pin locations for PNG
overlays. `physical_repair` JSON uses `region_boundarypins`, `region_boundarypins_split`,
`region_boundarypins_iterations` and `region_boundarypins_max_displacement_um`.

`place.region.boundarypins.order=true` uses the same prefix-minimum slot-assignment
solver as top-level IO initialization to order bus lanes and minimize weighted
split-net HPWL. Each virtual pin stays on its original atomic rectangle edge;
there is no perimeter rotation or pin-to-pin ordering force. Per-bus assignment
tries ascending and descending bit order, permits missing bits, and reserves
site-pitch-spaced slots against other buses. Slots inside real macro footprints
are forbidden; frame masks are not routing obstacles. Residual macro-interior
pins from ambiguity or whole-edge fallback are reported explicitly. Ambiguous repeated crossings of one
bit and corner pins stay fixed. An edge without sufficient slots is restored to
its original coordinates and reported rather than silently breaking constraints.

The assignment runs once before the extra GP iterations. All resulting virtual
pins remain fixed throughout GP and are removed before normal legalization.
The ordering report distinguishes virtual-pin order from actual estimated RSMT
order: `boundarypins/rsmt_order/` tracks the initial unordered real-net cohort
before GP, after GP, and after legalization. Changed or ambiguous groups are not
counted as untangled. `pins.tsv` includes both original and assigned coordinates.
The diagnostic JSON switch is `boundarypins_order`. Native HPWL trace fields use
timing weights; the main QoR HPWL remains the unweighted physical-net metric.

### Experimental opposite-edge pin alignment

`--set synth.place.region.pinalign=true` enables the split-boundary-pin model in the
final cell placement stage only (default false). It requires
`place.region.boundarypins.split=true` and can be combined with
`place.region.boundarypins.order=true`. It does not run during floorplan or macro
placement. `physical_repair` JSON uses `region_pinalign`.

After optional bus ordering, the pass follows each split net from its original
driver. It considers opposite-side boundary pins connected directly inside a
rectangle, or through one movable standard cell whose center lies inside that
rectangle. Adjacent sides, corners, masked controls, multi-cell paths and local
pieces containing multiple real pins are excluded. This is a connectivity
heuristic, not a timing-arc or via-delay model.

For a direct wire, the output boundary pin takes the input boundary pin's
coordinate along its existing edge. For a one-cell path, both boundary pins move:
the input boundary pin aligns with the cell's physical input pin, and the output
boundary pin aligns with its physical output pin. Left/right passage uses each
cell pin's Y coordinate; bottom/top passage uses each pin's X coordinate. Native
pin offsets already include cell orientation. The two boundary coordinates may
differ; no artificial straight line is forced through the cell.

Connected groups are handled atomically. Conflicting targets or cycles are
skipped, as are moves that change bus order, violate site-pitch spacing, leave an
atomic edge segment, or enter a real macro. A conflict at either boundary leaves
the whole group's pins unchanged. Bus order takes precedence. No real cell moves
during pin assignment. The assigned virtual pins stay fixed during extra GP,
but real cells remain movable, so their actual pin alignment can change during
GP or legalization. The ordinary physical-net model is restored before
legalization.

`boundarypins/report.json` includes a `pinalign` object with direct/one-cell
candidate counts, accepted pair counts (already aligned versus newly aligned),
moved-pin counts, and rejection reasons.
Its `pairs` list identifies input/output anchors (zero-based rows in `pins.tsv`),
rectangle, intermediate cell (or -1), temporary cell-pin IDs, assignment-time
cell-pin target coordinates, and outcome. Counts describe assignment-time
alignment; virtual targets stay fixed through GP but real cell pins may move; the separate unconstrained
physical-net RSMT diagnostic does not measure compliance with those assignments.

For controlled macro spreading experiments, `synth.place.macro.fill_cell_space=true`
sets nominal filler body area to `(1-density) * cell_space * virtual_filler_fraction`.
Cell space is core area minus reserved physical macro bodies and fixed ordinary
obstacles; macro halos and current overlaps do not change that reservation.
The existing positive `virtual_filler_fraction` selects the requested budget
multiplier (for example 1, .75, or .5). This opt-in mode is cleared before final
cell placement, and does not change the default filler budget.

Macro warpspace pulls compensate the GP preconditioner
`max(1, pin_degree + density_weight * padded_area)`. Their common force reference
is the mean preconditioned ordinary macro gradient, before optional objectives.
Every anchor uses the same characteristic length (mean maximum anchored-macro
dimension), with a common far-target cap. Thus equal center-to-anchor offsets
and strength give equal pull-only center gradients regardless of macro area
or pin degree. Ordinary wirelength, density, and boundary projection still
contribute to the final movement. The placement cache version distinguishes
this behavior from the earlier size-dependent normalization.

Macro targets are projected into center bounds that fit the full physical
body plus halo inside the core, intersected with any node position box.
The macro can reach the edge; its center cannot cross the body/halo boundary.
Per-refresh JSON retains raw displacement as `raw_dx/raw_dy`, the actual applied
projected displacement, and `projected_samples`. FP cell/centroid targets keep
their existing behavior.
### Experimental within-run GP A/B search

`physical_repair` accepts opt-in `gp_ab=true` for final fixed-macro cell
placement, with timing feedback, RSMT routability, virtual fillers, and
`local_density_jitter=0`. It is incompatible with moving boundary terminals,
cell-jump propagation and macro-placement forces. It also requires
`gp_resize=false`: its optimizer snapshots assume fixed topology. Ordinary
post-placement resizing remains available with `resize=true`. The driver rejects
the incompatible combination before placement starts. Existing defaults are unchanged.

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

### URoute learned congestion (development)

`--set synth.place.route.model=uroute` selects the learned estimator in final
cell GP; `auto` enables the one-way `rudy -> rsmt -> uroute` ladder when usable
weights and support exist in `synth.place.uroute.dir`. Existing RSMT defaults
remain. Final `uroute_switch=.01` and `uroute_hold=2` gate the second latch.
FP/macro stages use `S.route.probe=uroute` for a single convergence probe and
bounded feedback on failure; `true` remains an RSMT probe. Macro channel forces
and H2D abstract search use RSMT. Learned measurements expand physical macro
members before extracting features.

The bundled `@pretrained` directory currently records training as pending and
contains no weights. Empty/incompatible pools report `uroute_skipped` and use
RSMT. A project directory is read-only to LiveHD; the offline publisher owns
atomic manifest updates. Final physical features and geometry/grid metadata are
exported by default to `uroute_export/` under the placement workdir. Disable
export independently with `synth.place.uroute.export=false`. Weight/manifest
hashes, selected IDs and switch measurements appear in the resolved options,
GP history and QoR reports.
Final legalization preserves the optimizer's routing model, support IDs, skip
reason and incremental reuse accounting in QoR; its geometry and RC metrics
describe the final legalized placement. The prior report is also retained as
`before_final_legalization`.

See [the offline tools and format](../../tools/uroute/README.md) for training,
data ownership, support selection and validation commands. The implementation
currently needs the local DREAMPlace override during co-development. Routed-DEF
ingestion and partial LFSR baselines are implemented; full router evaluation and
pretrained weights are still pending.

## Timing-closure observation controller

`//pass/place:timeclosure` is the independent observation/controller foundation
for [2t-timeclosure](../../todo/livehd/2t-timeclosure.html). It has no timer,
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
