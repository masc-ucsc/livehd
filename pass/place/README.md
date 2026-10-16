# Physical placement stage

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
An unmet GP overflow target is reported even when legalization succeeds.
For the small full-sky130 trial, `synth.place.bins=16` and
`synth.place.iterations=1500` reached the overflow target; the default 32/500
settings legalized but did not converge in GP. This is fixture evidence, not a
universal tuning recommendation.

Parasitics use FLUTE with the first horizontal routing layer and the following
vertical layer in LEF stack order. Sheet resistance/width and area/edge
capacitance become ohm/um and fF/um. `synth.place.wire_rc=R,C` overrides both
directions. RC enters OpenTimer in memory; there is no SPEF file round trip.
This is a placement estimate, with no routing, via, coupling or clock-tree model.
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
horizontal and vertical clearance separately; the default is zero. Clearance
rounds up to the technology's site width and row height. CORE standard cells
receive no halo. Physical dimensions, pin offsets and reported physical area
remain unchanged; private padded occupancy participates in density, legalization
and detailed placement. Two macros reserve both halos between them. The same
clearance applies to cache reuse, ECO placement and independent legality checks;
an imported fixed macro with an illegal halo cannot silently pass. Requested
microns and effective DBU clearance are recorded in placement QoR. Floorplan
utilization continues to use physical area, so halo occupancy can exceed the
requested density target even when physical utilization is low.

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

`synth.place.timing_driven=true` enables slack-weighted placement. OpenTimer
first analyzes the mapped netlist with zero wire RC to seed the first placement's
net weights. It exports physical pin names with arrival, required time, slack,
slew and load. The seed prepares native memories and tie cells without running
placement or publishing a placement cache or DEF.
The worst 20% of data nets get linear weights from 1 to 5 with 0.5 weight
smoothing; clock nets are excluded. `synth.place.timing_rounds` chooses 1–8
refinement checkpoints after the first placement (default 3). Each checkpoint
starts a fresh GP/LG/DP call with fixed IO, then recomputes RC and STA. This is
staged weighting, not an observer inside
one uninterrupted Nesterov solve or virtual-buffer timing. Reports are in
`synth/place/timing_driven.json` and placement QoR; `initial_weights` and
`initial_timing_source` identify the post-synthesis seed. Missing constrained data
nets are reported as an error when timing-driven placement is requested.

`synth.place.resize=true` enables bounded sizing and buffer optimization after
legal placement. Equivalent scalar logic cells must have the same ordered pin
interface, footprint and exact Liberty truth table (up to six inputs). Drive
variants may have different widths. Sizing and redundant-buffer bypass run first,
followed by a separate design-repair pass that splits fanout on FLUTE Steiner
branches and inserts repeaters along long edges. It uses Liberty input caps,
available slew/cap/fanout limits and placement wire RC to plan candidates. Resolved
physical pins preserve individual packed-bus bits through wiring aliases.

New or resized cells first search row/site-aligned whitespace while all unchanged
cells remain fixed. If this fails, expanding local ECO placement is the fallback;
every candidate receives a whole-core legality check and fresh RC/STA. QoR marks
the fast path as `eco.single_cell=true`. Fixed cells and clock/reset networks are
protected and special power-domain cells are excluded. Each of the two passes
is limited to 24 trials and eight accepted edits. `synth/place/resize.json` and
`synth/place/repair.json` record their trials.

Steiner repair is a constructive heuristic, not optimal dynamic programming.
Each candidate is bounded to 256 new buffers and 1000 distinct FLUTE locations;
oversized or unsupported candidates are refused. Its electrical estimates guide
planning; fresh OpenTimer results control acceptance. These bounds do not
guarantee that every design violation is repaired.

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
