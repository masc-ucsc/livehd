# Physical placement stage

With ABC, OpenTimer and an explicit workdir, the default runs macro placement,
fixes the macros, and then places the cells (`synth.place.fp=false`). Without
macros it uses a single placement stage. Macro snapping remains enabled.
Enable `synth.place.fp=true` explicitly for centroid floorplanning and the separate
macro-refinement/final-cell passes. The legacy `r8h2`/`r8h3` macro modes and manual
macro-centroid diagnostic have been removed.

`synth.place.io_resynth=5` enables placement-aware whole-color ABC resynthesis.
At each completed stage, OpenTimer's in-memory pin arrivals/required times are
reduced to region port bits; clock and reset nets are excluded. A near-critical
color is remapped only if an IO constraint on a near-critical path tightened by
more than 5% of the clock period. A scoped fanout-to-Steiner-length wire-load
table supplies ABC's capacitance estimate. Timing must not worsen under the
same environment and a bounded combinational CEC must prove the replacement.
State and memory instances remain opaque and retain their interfaces.

One automatic batch is allowed: either before placement using saved final
feedback, or after the first completed stage. An accepted batch restarts that
stage, seeding surviving cells (including SRAMs) and IOs from its placement.
These are movable initial coordinates: the restart does not copy fixed status,
add position bounds, or lock unchanged colors. The normal macro lock happens
after macro placement. A saved-feedback resynthesis before placement has no
placement seed. `restart_seed.json` in the restarted stage reports reused
movable cells/macros and new cells. Later stages save
feedback and warn when another run could help. The per-color decisions and
used/placed environments are in `synth/place/io_resynth.json`; reusable state is
in `abc_cache/io_timing.json` and `abc_cache/wire_load.json`. Use a distinct
`--workdir` for each major placement configuration. `lhd.incremental=false`
disables reading/writing reusable state but permits feedback within this run.
The IO timing snapshot records the timing-engine source identity. A timer
change discards its old measured arrivals/required times before the pre-placement
trigger check, while preserving the constraints used to map the body and the
geometric wire-load tables. Fresh placement then supplies current measurements.
Set `synth.place.io_resynth=0` to disable the loop and retain the single-call
non-FP flow. Explicit positive values require a workdir, ABC, full placement,
OpenTimer, and a resolvable SDC clock. No IO timing JSON is exchanged during GP
timing callbacks; the two state files are stage-boundary snapshots.

`synth.place.resize=true` enables timing-checked sizing and buffering in full
placement with OpenTimer. Explicitly disable it for a geometry-only comparison.
The shared synthesis defaults use coarse partitions, state sweeping and register
merging, a delay budget of 0.4 of the SDC clock period when available, ABC fanout
32 with area relaxation off, and flat reset fanout 32. Explicit options override
these defaults.

`lhd synth --set synth.place=true` runs CPU DREAMPlace between mapping and STA.
The default remains false. `synth.lef` is a comma-separated tech LEF and cell/macro
LEFs; its sky130HD fallback is next to the Liberty directory in
`HAGENT_TECH_DIR`. Locate installed PDKs with `ciel ls --pdk sky130A` and
`ciel path --pdk sky130A VERSION`.

`place.cpp` flattens into a separate library without editing the input, resolves
mapped physical pins, generates/imports a floorplan, runs GP, assigns IO slots,
and runs GP plus legalization/detail. Integer DBU positions and orientation live
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

GP also waits for timing-weighted HPWL and routability after geometry/density
settle. `synth.place.post_density_factor=4` allows up to four times the first
eligible iteration count as **additional** iterations (40 becomes a deadline
of 200); the configured iteration cap still applies. Set it to zero to replay
the previous density-only stop. Two consecutive 20-iteration samples must have
relative weighted-HPWL and inflation changes at most
`synth.place.convergence_tolerance=0.01`, and current RUDY overflow must be zero
when routability is enabled. FP's measured OpenTimer setup violation, when
available, must also be zero. Other phases use weighted HPWL as a timing proxy;
they do not claim measured timing closure. New net weights or topology reset
the stability window but never extend the continuation deadline. Satisfied
density does not keep increasing the density penalty during this continuation.
The GP/FP JSON records the deadline, budget exhaustion, and whether timing was
observed; a legal placement stopped by the budget is not reported as converged.
For the small full-sky130 trial, `synth.place.bins=16` and
`synth.place.iterations=1500` reached the overflow target; the earlier 32-bin/500-iteration
trial legalized but did not converge in GP. This is fixture evidence, not a
universal tuning recommendation.

Parasitics use FLUTE with the first horizontal routing layer and the following
vertical layer in LEF stack order. Sheet resistance/width and area/edge
capacitance become ohm/um and fF/um. `synth.place.wire_rc=R,C` overrides both
directions. RC enters OpenTimer in memory; there is no SPEF file round trip.
This is a placement estimate, with no routing, via, coupling or clock-tree model.
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

Nets beyond FLUTE's 1000-location degree limit use an explicitly approximate
Manhattan star until fanout repair can split them. Placed timing includes the
last wire segment into primary outputs and native memory captures.

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

Floorplan input currently requires the same DBU as LEF, contiguous equal-width
N/FS rows, and FIXED IO pins. Row gaps/blockages and rotated rows are refused.
Mapped standard cells need matching LEF and Liberty models. Explicit hard macros
remain movable and retain exact LEF dimensions, even when their widths/heights
are not integral standard-cell sites/rows. DREAMPlace uses its existing macro
legalizer, which rounds private occupancy envelopes for packing without changing
physical dimensions or pin offsets. Row flips apply only to CORE cells; BLOCK
macros keep their supplied orientation, including when a warm cache hit locks
them.

`synth.place.halo=H` reserves H microns per side around every non-CORE LEF
macro, including native-memory BLOCK proxies. `synth.place.halo=X,Y` selects
horizontal and vertical clearance separately; the default is 5 µm per side. Clearance
rounds up to the technology's site width and row height. CORE standard cells
receive no halo. Physical dimensions, pin offsets and reported physical area
remain unchanged; private padded occupancy participates in density, legalization
and detailed placement. Two macros reserve both halos between them. The same
clearance applies to cache reuse, ECO placement and independent legality checks;
an imported fixed macro with an illegal halo cannot silently pass. Requested
microns and effective DBU clearance are recorded in placement QoR. Floorplan
utilization continues to use physical area, so halo occupancy can exceed the
requested density target even when physical utilization is low.

`synth.place.snapshots=true` saves physical GP coordinates before legalization
for visualization. Ordinary placement writes `gp.json`; H2D writes
`h2d_round<N>_gp.json` after each abstract GP is expanded to physical cells,
before group legalization. Use the selected round in `macro_topology.json`
when comparing H2D against its final placement. These diagnostic frames can
overlap and are not legal placement exports. Snapshots default off and do not
change placement objectives or coordinates.
Snapshots are generated by fresh GP calls; cache hits do not regenerate them.

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
first analyzes the mapped netlist with zero wire RC to seed the first placement's
net weights. It exports physical pin names with arrival, required time, slack,
slew and load. The seed prepares native memories and tie cells without running
placement or publishing a placement cache or DEF.
The worst 20% of data nets get linear weights from 1 to 5 with 0.5 weight
smoothing; clock nets are excluded. `synth.place.timing_rounds` chooses 1–8
refinement checkpoints after the first placement (default 3). Each checkpoint
starts a fresh GP/LG/DP call with fixed IO, then recomputes RC and STA. This is
outer staged weighting, separate from the FP-only checkpoints below, and does
not use virtual-buffer timing. Reports are in
`synth/place/timing_driven.json` and placement QoR; `initial_weights` and
`initial_timing_source` identify the post-synthesis seed. Missing constrained data
nets are reported as an error when timing-driven placement is requested.

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
weight W. Other nets target weight 1. `synth.place.timing_smoothing` (default 0.5)
is the new-target share at updates: `(1-alpha)*old + alpha*target`. Initial
zero-wire weights use the target directly. These weights multiply both
the smooth wirelength cost and its gradient before GP preconditioning. They do
not directly change the configured centroid or density coefficients, or size
cells. The native RUDY estimator also uses net weights, so critical nets count
more toward routing demand. Detailed placement also runs with timing weights: k_reorder,
independent_set_matching and global_swap score moves with weighted HPWL (net
weight times net HPWL), and a pass is kept only if weighted HPWL does not
increase and the placement stays legal.

The placer reads numerical setup slack for physical driver/sink pins, taking
the minimum available rise/fall slack per pin and then the minimum across each
net. It does not consume legacy arrival-margin criticality colors or the
`attrs::critical` annotation, and does not enumerate a few printed timing paths.
This ranking is relative: the selected fraction can receive extra weight even when
all constrained nets have positive slack. Synthesis/centroid color membership
is preserved by the weighting path. Centroid forces remain normalized against
the current combined wirelength/density force, so their effective scale can
change indirectly even though their option values are unchanged.

With timing-driven placement enabled, `synth.place.fp.timing_interval=50`
recomputes placement RC and runs OpenTimer every 50 completed FP GP iterations.
It uses the same `timing_weights()` rule and configured smoothing. Set the
interval to `0` to disable FP timing and sizing checkpoints. No checkpoints run
in the IO prototype, macro GP, or final-cell GP; those stages inherit the last
available weights. Designs with no FP candidates have no FP pass/checkpoints.

Set `synth.place.fp.timing_after_density=true` to defer FP timing and repair
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
The budget applies only to movement-based scheduling, not fixed cadence;
repair trials require additional STA calls within each update. Zero preserves
fixed cadence; `timing_interval=0` disables both modes. Motion from centroid
forces counts even after density settles. The CNN scheduling comparison enables `0.5`
explicitly; the existing fixed-cadence default is unchanged.

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
legal placement. Logic upstream of a clock or reset pin is protected from every edit by
default, so a reset net that fans out to thousands of registers (a reset synchronizer's
mux driving all of a block's asynchronous resets) is left as mapped, one weak gate driving
the whole net. `synth.place.repair.reset=true` lifts the protection for RESET pins and
reset-named pins only (clock cones stay protected), letting the same sizing and fanout
splitting treat the reset net like any data net, as a conventional pre-CTS flow does; it is
part of the placement cache identity when floorplan resizing is on. Equivalent scalar logic cells must have the same ordered pin
interface, footprint and exact Liberty truth table (up to six inputs). Drive
variants may have different widths. Sizing and redundant-buffer bypass run first,
followed by a separate design-repair pass that splits fanout on FLUTE Steiner
branches and inserts repeaters along long edges. It uses Liberty input caps,
available slew/cap/fanout limits and placement wire RC to plan candidates. Resolved
physical pins preserve individual packed-bus bits through wiring aliases.

`synth.place.reset_fanout=N` (default 0 = off; N >= 2) is the placement-time reset buffer tree that does not
depend on timing acceptance. Right after the mapped hierarchy is flattened, every net that drives an asynchronous
clear/preset pin of a Liberty flop and more than N cell pins in all (the synchronous-reset logic of the same signal
included) is dealt out through the library's smallest placeable buffer, N pins per buffer, level by level until the
driver carries at most N loads. Pins are grouped in instance-name order, which follows the source hierarchy, so a buffer
serves neighbouring registers; graph outputs, bus-wiring aliases and macros keep their direct connection. It is pure
fanout restructuring (a chain of identity buffers per pin, LEC-proven in `lhd_placement_reset_tree_smoke`) and
idempotent, and it composes with `pass.abc.reset_fanout`, which builds the same tree inside each mapped region at
read-back: the region trees bound a reset net where its registers sit in one region, this one bounds the sum across
the many regions a reset reaches (a mux output driving 225 pins spread over fourteen regions of NVDLA partition_a).
Unlike `synth.place.resize` it is not an accept/reject trial: the post-placement repair measures a change with a
fresh STA and rejects a whole batch on any hold-WNS movement, and on NVDLA partition_m the forest that fixes the
439-pin reset (3496 -> 1420 ps) was rejected for `hold_wns` in each trial that contained it. The `reset_tree` object of `qor.json`
records `fanout`, `nets`, `sinks`, `added`, `levels` and `skipped` (clear/preset pins left alone because a multi-bit or multi-driver bus alias drives them) of the pass in that placement call and `buffers`, the tree cells present in the placed netlist (a timing-driven run builds the tree in its seed placement, so the final call adds none); the value is part of the placement cache identity.

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
When feedback cannot isolate a useful subset, retries bisect the batch and visit
disjoint subsets before descending further. Forest batches keep this bisection
fallback. The budget is
24 batch evaluations (at most 12 for each sweep when combined), rather than 24
master alternatives on the first few cells. Ineligible trees are reported without
consuming timing evaluations. Physical repair removes and rebuilds buffers jointly in the forest sweep.
Aggregate-only diagnostic callers retain the bounded individual fallback; it
interleaves alternatives across drivers and stops at eight accepted edits. `synth/place/resize.json` and
`synth/place/repair.json` record their trials.

Placement RC uses FLUTE up to 1000 distinct locations of a net; larger nets are
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

### Path-based resize engine (`synth.place.resize.engine=new`)

`synth.place.resize.engine` selects the implementation behind `synth.place.resize=true`,
both at FP timing checkpoints and after legal placement. `legacy` is the batch
sizing/forest sweep described above; `new` (`resize_engine.{hpp,cpp}`) plans on the
persistent incremental timer instead of a fresh evaluation per batch.

| Option (`synth.place.` prefix) | Default | Meaning |
| --- | --- | --- |
| `resize.engine` | `legacy` | `new` or `legacy` |
| `resize.vt_swap` | false | `new`: allow same-footprint/function swaps across Liberty files (multi-VT); otherwise a cell keeps its file |
| `resize.dont_use` | empty | `new`: comma-separated globs never chosen as a new master or repeater (Liberty `dont_use` cells stay excluded) |

*What-if timing.* `Physical_session::whatif()` (`core/timing_whatif.hpp`) applies
transient edits to the evaluated OpenTimer state: a master swap (`repower_gate`), a new
cell, and `set_net`, which moves pins onto a net and attaches its wire RC. Cutting a wire
for a buffer is two `set_net` calls whose FLUTE Steiner RC (`steiner_rc`, the same
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
by the small fixtures. Macro placement, routed RC, virtual buffering, continuous
optimizer checkpoints and cross-process optimizer-state reuse remain follow-ups.

The host pins DREAMPlace's shipped BUILD files and uses a plain LEMON archive
with the registry compatibility patches, avoiding root-only Bzlmod overrides.
The zero-area-terminal patch fixes the upstream row-overlap checker for IO
points on core edges. Both independent and upstream checks remain enabled.
FLUTE's lookup tables are runfiles of the standalone binary and test targets.

### Opt-in repulsion and routability experiments

The native solver keeps its existing defaults. These `synth.place.*` options
are independent controls for staged experiments (plain placement):

| Option | Default | Behavior |
|---|---|---|
| `auto_bins` | `false` | Override `bins` with power-of-two grids, 32–64 per axis, targeting the smallest physical macro dimension; without macros use four average cell pitches. Automatic refinement is capped at twice the original 32-bin grid per axis. Resolve before halo padding. |
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

The sibling DREAMPlace checkout used by `MODULE.bazel` is committed at
`7b9b496`. Build with `bazel build -c opt //lhd:lhd`; the native library needs
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

### Macro legalization snap algorithms

`synth.place.macro.snap=true` enables macro snapping before cell legalization.
`synth.place.macro.snap_mode` selects `alignment` (default) or `symmetry`.
Both use the same eligibility: movable macros of identical physical width and
height in the same original hierarchy scope. Synthesis-only `sub_<number>`
containers are ignored. A wrapper containing a single macro inherits its nearest
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

All future Eyeriss comparisons explicitly set `synth.place.bins=64` and
`synth.place.auto_bins=false` for every GP stage. The native default grid is unchanged;
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
This avoids duplicate analyses between passes; each changed-state callback is an
incremental update of the persistent timer. The diagnostic tool publishes the accepted
analysis as `final_timing.json`; the last numbered trial can be a rejected state.

### Fixed-macro cell-placement experiments

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
