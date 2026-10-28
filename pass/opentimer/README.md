# pass/opentimer — OpenTimer STA on a tech-mapped module (2opt-freq D)

`lhd pass opentimer --top <module> lg:netlist cells.lib [file.sdc file.spef]`
runs OpenTimer static timing analysis on **one** `pass.abc` tech-mapped module
and reports the critical path machine-readably. It is the phase-2 (accurate,
whole-module) frequency oracle of the 2opt-freq loop; `pass.abc`'s own QoR
read-back is the cheap per-region phase-1 estimate.

## Usage

```
lhd compile design.prp --top m --emit-dir lg:g
lhd pass abc --top m lg:g --emit-dir lg:net --set synth.liberty=cells.lib
lhd pass opentimer --top 'm__c0' lg:net cells.lib --workdir W
```

- Timing files are **positional** (like `pass liberty gensim`): 1–2 Liberty
  (`.lib`, second = min corner) plus optional `.sdc` / `.spef`. `files` is a
  kernel-managed label — the kernel assembles it, `--set` cannot.
- `extra_libs` (kernel-set, comma list of `.lib`): more cells for **both**
  corners, loaded after the max/min pair of `files` and part of the STA cache
  key. `lhd synth` uses it for the provisional domino cells, whose Liberty it
  generates from the bundle (`pass/domplace/README.md`, "Timing for OpenTimer").
- **One design per run** (one `ot::Timer` holds one design): `--top` picks the
  def out of the netlist library. Time a region module (`<mod>__c<N>`), a
  single flat module (`pass.color flat` + `pass.abc` map the whole design as
  one module), or a *hierarchical* netlist top: by default (`hier=true`) the
  instance hierarchy is structurally flattened into a scratch def
  (`pass/partition/flatten.cpp`; node names keep the dotted instance path, the
  report keeps the real top name) and timed as one module, so the critical
  path spans modules. With `--set pass.opentimer.hier=false` a Sub that is not
  a Liberty cell is instead a hard error, never silent garbage — one
  tech-mapped module per run. (`hier=stitch` keeps the legacy name-stitched
  hier walk for debugging; its multi-bit module-boundary buses are not
  stitched.)
- Options: `--set pass.opentimer.critical=true` (persist critical-path
  annotations), `--set pass.opentimer.qor=FILE` (report path; `lhd pass
  opentimer` defaults it to `<workdir>/timing.json` under `--workdir`).
  `--stats` adds one row per mapped `(definition,color)` while retaining the
  whole-design critical-path report.

## Critical-path annotations and queries

```sh
lhd pass opentimer lg:net cells.lib design.sdc --top top --workdir W \
  --set pass.opentimer.critical=true
lhd tool sta lg:net --top top
lhd tool sta lg:net --top top clock=core 'slack<0' src=alu.prp
lhd tool sta lg:net --top top color=3 --diag-fmt json --max 0
```

`critical=true` marks every node on every setup path whose slack is at most
5% of its target clock period, including violations. With a 1 ns target the
threshold is 0.05 ns of slack; paths below 0.9 ns are unmarked. The threshold
is relative to the constraint, not the measured worst delay or a percentage
of cells. `pass.opentimer.critical_percent=2` changes it to 2%; zero selects
violations and paths exactly at the target. Required times propagate through
all timing arcs, so near-critical reconvergent branches are included.

SDC clocks supply the targets by default. To override their periods, use
`--set pass.opentimer.targets=core:1ns,bus:4ns`. Names select SDC clock names
or their source ports. Without an SDC, a target matching an input port binds
that physical clock; another name defines a virtual clock. A single target
also constrains otherwise unconstrained outputs. Multiple targets require
explicit `set_output_delay -clock NAME` associations for external outputs.
Periods accept `ps`, `ns`, `us`, or `s`; bare numbers use Liberty time units.
Overrides affect required arrivals as well as annotations. Requesting critical
annotations without a usable clock/target is a diagnostic error.

Timing traverses instances hierarchically (`hier=true`, the default), then
stores annotations on the original module-local nodes. A shared definition
gets the union of its instances' critical nodes, and the minimum slack per
node and clock. The `critical` flag is independent of the existing numeric
partition `color`. The `sta` string attribute stores clock, period, setup slack,
unit, critical status and a representative worst timing pin. The top input's
`sta` attribute stores the analysis summary; `timing.json` exposes the same
summary under `critical`. Native state retains the existing ideal boundary
model (zero launch arrival and zero setup), with capture endpoints on sampled
data/control pins. This does not model latch borrowing or clock-tree skew.

The pass saves marks to its input graph library, or to `--emit-dir lg:marked`
when supplied. `lhd synth` also accepts these options and saves the annotated
mapped netlist. Each successful marking run replaces previous annotations in
the selected top's reachable definitions. Cache hits restore the same marks;
annotation options and local node/source identities participate in the key.

`lhd tool sta` reads the saved evidence without running timing. It lists only
critical rows by default; `critical=false` lists passing rows and
`'critical~.*'` lists both. Filters include `module`, `src`, `name`, `color`,
`clock`, `slack`, and `period`; numeric timing filters use the reported unit.
Use `--attr critical,clock,slack,src` to select pretty-print columns,
`--diag-fmt json` for JSONL records, and `--max 0` for all matches. Each row
includes the owning module and node ID. An analyzed design with no critical
paths returns an empty result; an unannotated design produces a diagnostic.
Annotations describe the last analysis; rerun marking after changing a design.

Marking rejects unsupported SDC commands/modifiers, unresolved or ambiguous
clock bindings, unsupported clock-domain crossings, native multi-clock memories,
unmodeled native combinational cuts and `hier=stitch`. Specify a Liberty model
for such native operators/memories. Unclocked `set_output_delay` retains its
legacy meaning for ordinary reporting, but marking requires its `-clock`.

## Incremental reuse (`<workdir>/sta_cache/`)

Liberty macros retain their vector ports: every bus bit connects to its own
timing pin, including upper bits consumed through slices. Bus-level attributes
and timing arcs are inherited by member pins. Lookup tables can inherit their
axes from a named template; malformed value counts are rejected before writing
outside the allocated table. The OpenTimer dependency patch is included in the
STA cache salt. Grouped `related_pin` lists produce an arc for every listed
input, so required-time propagation includes every near-critical branch.

With a user-named `--workdir` and `lhd.incremental` (default true) the pass
keeps a **persistent STA result cache** — the third reuse tier next to the
compile cache and `pass.abc`'s `abc_cache/`. It exists because
`pass.opentimer` is 70–97% of a warm `lhd synth` on the large blocks: with
`pass.abc` reusing every colored region and landing at a few seconds, re-timing
the identical netlist from scratch was the whole remaining cost, which is why
whole-flow synthesis reuse measured 1.2–1.4× while its dominant pass had no
cache at all.

The key is **the netlist itself** — `semdiff::canonical_digest` over the timed
top, Merkle-folded through every region body — plus the timing environment: the
Liberty/SDC/SPEF/VCD file *content*, `--top`, `hier`, `margin`, critical options,
`--stats`, and
`kStaSrcSalt` (a build-time content hash of `pass/opentimer` + `pass/partition`
+ the `@opentimer` sources (the `../OpenTimer` fork), so a timing-engine change drops every record with no
human in the loop). Keying on the netlist rather than on the upstream sources is
what makes it sound: `pass.abc`'s intra-run cross-name reuse means a cold and a
warm run can legitimately produce slightly different netlists, and only the
graph about to be timed decides the timing.

A hit replays the pass's **whole** observable output — the report block, the
`slowest delay:` summary line, the per-color `--stats` rows and the
`native-comb-boundary` warning — and never parses the Liberty or builds a timing
graph (the same lazy-startup rule `pass.abc` applies to `Abc_Start`/`read_lib`).
`resynth` is the one field NOT taken from the cache: it says what *this* run's
`pass.abc` did, not what the netlist is, so the hit path re-stamps it from the
graph. Only an error-free analysis is stored — an incomplete timing graph
already failed the run, and caching its numbers would replay a failure as a
pass. A netlist with an anonymous state cell has no reproducible identity
(`digestable:false` in the telemetry) and always re-times.

Counters ride the report's `incremental` member and the result envelope's
`incremental.sta` (`enabled`, `hits`, `misses`, `digestable`, `lookup_ms`).
At most 32 analyses are kept per workdir, oldest insertion dropped first, so an
option sweep over one design cannot grow the cache without bound. Measured on
`xs_renametable` (464 colors, 1.4 M gates): STA 50.2 s → 1.4 s, whole-flow
`lhd synth` 65.7 s → 10.9 s and peak RSS 23.3 GB → 1.9 GB; on `minion` (545
colors, 2.0 M gates) 86.4 s → 2.5 s and 31.6 GB → 1.1 GB.

A cache hit is also where the pass's MEMORY goes: it never builds a timing
graph, and the whole cost of a cold analysis is downstream of that. On minion,
stage peaks are 1.0 GB entering the pass, 3.4 GB after the hierarchy flatten,
12.8 GB after `build_circuit` and 29.7 GB after `compute_timing` — so the
netlist and the design library are a rounding error next to `ot::Timer` and the
per-bit net bookkeeping, and reuse is the only lever that removes them.

## The timing report (`timing.json`, envelope `"qor"` member)

```json
{"schema_version":1,"kind":"sta","designs":[
  {"module":"m__c0","max_delay":0.6,"critical_pin":"g96_XOR2x1:Y",
   "critical_src":"design.prp:11",
   "endpoints":[{"pin":"…","delay":…,"src":"file:line"},…],
   "colors":[{"module":"m__c0","color":0,"cells":42,
               "max_arrival":0.6,"resynth":1},…]}]}
```

`max_delay` is the worst MAX-corner arrival over all gate output pins (library
time units, e.g. ns). `endpoints` lists the 10 worst arrivals. `src` resolves
each gate's `srcid` — pass.abc's source-map carry-through — so the critical
path points back at the pre-synthesis RTL line an agent should edit. Under
`lhd pass opentimer --workdir W` the report is also embedded verbatim as the
result envelope's `"qor"` member (same channel as pass.abc's `abc-map` QoR;
discriminate by `"kind"`). Every annotated gate output also gets the
`pin_delay` pin attribute in-graph.

With `--stats`, `colors` contains every mapped color, including colors with no
Liberty cells. `max_arrival` is the largest end-to-end arrival observed at a
cell output belonging to that color (so it includes upstream-color delay);
`cells` is occurrence-weighted in the selected timed top. `resynth` is carried
from the ABC-produced netlist: a full/cold build is 1, while an incremental ABC
cache hit is 0. Pretty mode renders each object on one `sta[stats]` line.

## Timing model

- **Native flops/latches/memories are path boundaries**. Each consumed flop/latch Q or
  memory read port becomes a virtual primary input arriving at 0, so
  state-to-state segments are scored; din/en/addr cones end at their driving
  gate pins. A latch is a hard timing break: transparency and time borrowing
  are intentionally not modeled. SDC clocks are ideal unless `set_propagated_clock`
  (see SDC below). `min period ≈ max_delay` up to setup/clock-skew terms.
- Liberty-mapped registers use clock-to-Q and setup tables. Critical marking
  rejects missing setup coverage instead of dropping capture paths.
- ABC's builtin tie cells (`_const0_`/`_const1_`) contribute no arrival.
- Primary inputs arrive at 0 with slew 0 unless an `.sdc` overrides them
  (`create_clock -period`, `set_input_delay/-transition`, `set_output_delay`;
  `[get_ports X]`, `[all_inputs]` and `[all_outputs]` targeting).
- `pass.opentimer.io_load` sets primary-output load in fF; a negative value
  preserves the timer default. The load participates in the STA cache key.
  Outputs attach to resolved driver nets after wiring-node traversal, so output
  aliases and internal readers all contribute load to their shared producer.
- Timing JSON includes physical cell area/count, native-state and opaque-logic
  counts, and cell-timing/constraint coverage flags. Missing Liberty area is
  omitted rather than counted as zero. These flags support the whole-design
  `pass.usyn` comparison; they do not certify physical signoff. A single ideal
  virtual clock can certify combinational I/O constraints as described below.
  Physical/multiple clocks, ignored commands/modifiers, unmatched ports and
  SPEF coverage remain uncertified.
- Multi-bit values traverse the netlist glue (`Get_mask`/`Set_mask`/... with
  constant masks) via the pin tracker, which rewires consumers to per-bit
  `port.N` nets. Tracker ids of trackable-node outputs are `n$`-prefixed
  internally so a region boundary port that pass.partition named after a
  source wire (e.g. a port literally called `get_mask_20`) cannot collide.

## Single virtual-clock I/O timing

For a completely mapped combinational design, the reader supports one
`create_clock -name NAME -period T` with no physical source or custom waveform.
Every primary input and output must have explicit `set_input_delay -clock NAME`
and `set_output_delay -clock NAME` values covering both min/max corners and both
rise/fall transitions. Omitting the selectors applies a value to both corners
and transitions. Separate values and subsequent per-transition overrides work.
Input transitions can still be specified independently.

The virtual clock's launch edge is zero. Input delays are arrival times relative
to it. An output's MAX required arrival is `T - output_max_delay`; its MIN required
arrival is `-output_min_delay`. A negative minimum output delay therefore creates
a positive hold requirement. The report's `clock_constraints` object includes
the period, worst setup/hold slack and a completeness flag. These fields are
retained on STA cache hits. The existing `max_delay` remains the raw maximum
internal arrival; it does not include the external output delay.

`pass.usyn` uses `period - setup_slack` (clamped at zero) for its clocked delay
comparison, includes the clock period as a timing target, and respects any
stricter explicit mapper delay target. It rejects introducing a hold violation
when the baseline meets hold, or worsening a baseline hold violation. This
retains the existing policy of preferring an improved delay when neither design
meets setup; a published improvement is not a claim that timing is met.

This certificate requires both corner libraries' cell timing coverage and no
native state, physical clock pins, or sequential/non-combinational Liberty arcs.
Missing corner/transition constraints, unknown references, physical or multiple
clocks, waveforms, falling-clock modifiers and other unsupported semantics keep
`constraints_complete=false`. Unclocked output delays retain their legacy raw
required-arrival interpretation and cannot certify a QoR replacement.

## Known limitations

- `pass.opentimer.power` (VCD-driven power) is registered but untested since
  the lgshell removal.
- The legacy `margin` numeric coloring still back-walks only a single worst
  edge and is not restored on cache hits. Use `critical=true` for persistent
  path annotations and `lhd tool sta` queries.
- SDC support is the restricted subset described above; no SPEF-less wire-load model
  (zero wire delay without SPEF).

## ODR warning (taskflow)

OpenTimer bundles its own taskflow (`ot/taskflow`). Everything else in the
`lhd` binary must use `@opentimer//:taskflow` — linking a second taskflow
version alongside corrupts `tf::Executor` at runtime. See
`../OpenTimer/BUILD.bazel`.

The OpenTimer dependency reads every `library(...)` group in a concatenated
Liberty input. Each group keeps its own bus declarations, templates and defaults;
time and capacitance are rebased independently by the timer before merging. This
allows a picosecond/femtofarad logic library and nanosecond/picofarad SRAM models
to share the mapping file without dropping SRAM cells or mis-scaling their arcs.
Each LUT owns its template axes, including across library copies and colliding
template names. Per-library input/output thresholds, slew bounds and derates
remain attached to cells; net propagation converts slew and arrival to the
receiving library's convention, including primary ports in the first library.
The positional one/two-file maximum/minimum-corner convention remains unchanged.

The native SDC reader supports scalar `set` variables, variable-held port
selectors, `${name}` references, and numeric `[expr ...]` with `+ - * /` and
parentheses. It also applies output `set_load` in Liberty capacitance units and
binds single-source physical clocks to their input pin. Output required times
subtract the output delay from the referenced clock period. This remains a
restricted SDC reader: arbitrary Tcl and generated clocks
are not modeled; unsupported constraints are reported, and physical-clock
coverage remains conservative. `$name` is substituted outside braces, as in
Tcl, and inside `[expr {...}]`; `[expr]` divides two integers with flooring
integer division (`[expr 5/2]` is 2, `[expr 5/2.0]` is 2.5). A line whose
command is not modelled, or that cannot be expanded (`$::env(X)`), warns once
and marks the report incomplete; a modelled command with a bad substitution is
an error. `set_load` and `set_driving_cell` with an unmodelled option
(`-pin_load`, `-library`, ...), a selector such as `[get_nets]`, a port of the
wrong direction or an unknown `-lib_cell` are skipped whole the same way
(fatal only with `critical=true`).

SDC clocks bound to a source port are ideal in every flow, including
`pass.opentimer.power`: the clock port, clock buffers and ICG outputs get zero
delay and zero slew, and `set_input_transition` on the clock port has no
effect. Clock-to-Q arcs, ICG enable checks and data-net RC stay timed.
`set_propagated_clock [all_clocks]` or `[get_clocks {a b}]` (names may be
braced, quoted or listed) opts those clocks back into propagated timing.
`set_false_path -from [get_ports X]` and `current_design` are also handled.

`set_driving_cell` supports `-lib_cell`, `-pin`, `-from_pin`, min/max and rise/fall
selection, and `-input_transition_rise`/`-input_transition_fall`. It adds the
loaded-minus-unloaded driver delay to the external input delay and recomputes
loaded slew whenever net capacitance changes. It includes sequential output
arcs when a register is the specified external driver. A later
`set_input_transition` replaces the driving-cell waveform for its selected
corners/edges. This model does not insert a physical input buffer.

Early and late timing use the corresponding `rise_capacitance_range` and
`fall_capacitance_range` values when provided. Scalar combinational Liberty
functions with up to six inputs propagate constants and restrict timing arcs
to sensitizable transitions. Constant cells, buffers and controlling inputs
keep their physical capacitance while impossible paths are removed. Known
Liberty `when` conditions select conditional arcs and suppress their default
fallbacks; unknown conditions remain conservative. Sequential state functions
and three-state outputs are excluded from this combinational evaluation.

Physical timing reports sum TNS once per endpoint, choosing its worst check and
transition. `tns_model=worst_per_endpoint` and `negative_setup_endpoints` expose
that convention; OpenTimer's underlying `report_tns()` API retains its original
per-transition behavior. `case_analysis` reports propagated constants and arc
restrictions. Cells with unused outputs remain present as upstream pin loads.

Timing-engine comparisons should import a complete mapped placement, preserve
masters/coordinates/orientations, and use identical Liberty, SDC and parasitics.
The native `physical_repair` diagnostic accepts `timing_only=true` and
`source_def=true` to import mapped DEF connectivity without HDL optimization or
legalization. `rc_file` supplies serialized native parasitics; `zero_wire=true`
removes wire R/C while retaining characterized pin loads. This path does not
certify the imported layout's legality. Its `timing.json` retains the path and
the 256 worst physical pins; `timing_pins` adds explicitly requested pins.
The smaller `timing_probe` diagnostic times a structural Verilog/Liberty/SDC/SPEF
reproducer through the same adapter. An optional `pins` list restricts its
per-transition report, including arrival, required time, slew, slack and load.

The native interconnect model reduces each RC tree to a driving-point Pi model
and computes a bounded charge-equivalent capacitance for characterized drivers.
It fits the driver ramp to Liberty timing, evaluates threshold crossings on the
driver/wire waveform, and honors each library's rise/fall thresholds and slew
derate. Invalid or load-independent characterized drivers retain full-load NLDM
timing, Elmore wire delay and characterized slew (converted to the sink
library conventions). This avoids inventing a waveform for constant-delay SRAM
tables. Uncharacterized input ports use a finite-ramp one-pole response.
`set_driving_cell` uses the same model. Physical capacitance and electrical-load
limits still use the full load; the selected effective capacitance is available
separately through OpenTimer's `Net::effective_load` and the diagnostic probe.
This remains an RC approximation. Keep its residual differences separate from
Liberty/constraint errors and differences between placement RC estimators.

Compare a persistent session with a fresh session before changing the timing
model. Then compare engines with zero wire RC and with an identical exported RC
network. Inspect one common path by transition, not just each engine's own worst
path. A matching zero-wire result isolates interconnect behavior from Liberty
tables and setup constraints. A single-pole test provides an analytic reference:
at 50% voltage the delay is `tau * ln(2)`, while a step's 20–80% slew is
`tau * ln(4)`. Finite input slew and each library's threshold/derate conventions
must be included when comparing wire responses. Driver effective capacitance is
a separate model from converting an RC moment to a voltage crossing; correcting
one does not establish agreement in the other. Do not compensate for an
unexplained model difference by adjusting critical-net fractions or sizing limits.

## Placement feedback in memory

`livehd::opentimer::Physical_session` owns one OpenTimer for a placement flow.
Construct it with the Liberty/SDC options, then call `evaluate(graph, rc)` with
its flat mapped graph and placement parasitics. It returns
`livehd::timing::Feedback` directly, including physical-pin timing, electrical
violations, area, endpoint WNS/TNS and the path used by physical repair.
No timing JSON is generated, written or read by `evaluate`. Explicit timing
reports remain output artifacts, separate from feedback.

Placement callers pass their existing thread budget as the constructor's second
argument. The session applies it to OpenTimer's lazy worker pool and preserves
it across recovery and report-triggered rebuilds. `worker_threads` in session
statistics records that budget. Omitting the argument retains the standalone
timer's hardware-concurrency default.

GP ranking uses `Feedback::Ranking`: a dense slack/clock array and a shared,
immutable physical-pin/net layout. The session binds timer pins once per
topology; coordinate-only updates reuse those bindings. Gate/topology changes
and what-if access invalidate the bindings before the next ranking sample.
No per-pin ordered map or detailed pin-time breakdown is built for ranking.
Feedback owns its layout and exact RC snapshot, so it remains usable after
scratch graphs and later timer generations are destroyed. Net weighting uses
numeric pin indices for that snapshot and resolves labels for other snapshots.
Reset filtering masks a copied array without changing the original sample.
Cell/terminal/SAT legalization consume the same rank and clock information.
Detailed timing and report consumers retain the ordinary pin records.

At the final `pass.opentimer` boundary, `lhd synth` can transfer the session
through the transient `Eprp_var::physical_timing_session` field. `write_report`
reconciles the final RC, masters, connectivity and reset cuts, rolls back pending
what-if edits, and renders the ordinary report from that timer. It does not
rebuild the timing graph or refit unchanged nets. The report preserves per-color
statistics and populates the ordinary STA cache for later CLI invocations.
`physical_session_reused` identifies this path; cache hits set it to false.
Criticality annotation, nonzero margin, legacy hierarchy stitching and changed
timing options or timing-file contents use the standalone path. Input contents
are hashed once at initialization and once at reporting; iterative evaluations
do not reread the files. Successful reporting consumes the
session; another evaluation starts a fresh timer.

Coordinate-only updates compare an owned cell-identity snapshot and net
connectivity before updating RC. They reuse the existing cell and connection
indexes; resize, cell-order and topology changes use the full delta path.
Both paths flush changed RC batches after 65,536 capacitance/resistance records.
Nets are indivisible, so the last net can take a batch beyond that threshold.
Batches carry one name per RC node and numeric segment endpoints through
OpenTimer's indexed in-memory API, avoiding repeated endpoint strings and
lookups. Each batch replaces the timer's old RC trees immediately and releases
its temporary payload. Node insertion and segment order match the SPEF path
to preserve floating-point results. Timing propagates once after the complete
update, retaining all batches' frontiers. External SPEF and what-if rollback
continue to use the named API.
RC-tree construction now belongs to reconciliation time rather than propagation
time; compare total timing cost when reading statistics across this change.

What-if queries cache physical-to-timer pin-name translations within the session.
Gate insertion, gate rollback and a new `evaluate` invalidate those translations.
The cache holds names only: slack, slew, loads and other timing values continue
to come from the synchronized timer. Before a new evaluation, rollback restores
pending wire RC even when the incoming accepted net is unchanged. The journal
and its owning RC snapshot are then released; the public what-if proxy remains
valid and creates a fresh journal on demand. RC restoration queues propagation
with the incoming evaluation rather than triggering an extra timing pass.

Placement passes a third `data_exclusions` argument containing physical pins on
excluded non-clock nets. The session masks those net timing arcs before propagating
arrival/required times, so registered reset paths cannot dominate downstream data
slack. Clock networks stay active for launch and capture. This analysis-only mask
preserves net connectivity, physical capacitances and RC trees. Replacing the set
or passing an empty set restores the SDC/Liberty case masks before applying the
new exclusions; topology changes reapply them. Standalone STA also applies the
typed reset cuts carried by its physical RC or explicit pass inputs.

The session loads Liberty and SDC once. Later evaluations apply gate repowering,
buffer insertion/removal, connectivity changes and changed RC nets to the same
timer. Cell identities use physical instance names, independent of scratch-graph
node indices. Evaluating the retained placement after a rejected candidate
reverses its deltas; it does not assume that the last timed candidate was kept.
Parasitic snapshots supplied to the session must remain immutable. A changed
snapshot is a new `Parasitics` object, as produced by the placement RC estimator.
Changing net membership invalidates its old RC tree before another timing
evaluation; attaching the new parasitics restores the updated tree. Repowering
retains connectivity and recalculates the load-dependent driver model.
Top-level ports and native memory/state boundaries remain the same during the
session. Synthesis criticality-color annotation and external SPEF use the
standalone pass rather than this physical-feedback interface.

This is incremental timing within a running optimization. It is independent of
the on-disk STA result cache used by separate CLI invocations. The session's
statistics count builds, evaluations, edited gates, updated RC nets and timing
wall time without per-evaluation report files. Edit counters include candidate
trials and rollback operations; they are not final netlist change counts.
