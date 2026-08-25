# pass/opentimer — OpenTimer STA on a tech-mapped module (2opt-freq D)

`lhd pass opentimer --top <module> lg:netlist cells.lib [file.sdc file.spef]`
runs OpenTimer static timing analysis on **one** `pass.abc` tech-mapped module
and reports the critical path machine-readably. It is the phase-2 (accurate,
whole-module) frequency oracle of the 2opt-freq loop; `pass.abc`'s own QoR
read-back is the cheap per-region phase-1 estimate.

## Usage

```
lhd compile design.prp --top m --recipe O1 --emit-dir lg:g
lhd pass abc --top m lg:g --emit-dir lg:net --set abc.library=cells.lib
lhd pass opentimer --top 'm__c0' lg:net cells.lib --workdir W
```

- Timing files are **positional** (like `pass liberty gensim`): 1–2 Liberty
  (`.lib`, second = min corner) plus optional `.sdc` / `.spef`. `files` is a
  kernel-managed label — the kernel assembles it, `--set` cannot.
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
- Options: `--set pass.opentimer.margin=<0-100>` (criticality coloring
  threshold), `--set pass.opentimer.qor=FILE` (report path; `lhd pass
  opentimer` defaults it to `<workdir>/timing.json` under `--workdir`).
  `--stats` adds one row per mapped `(definition,color)` while retaining the
  whole-design critical-path report.

## Incremental reuse (`<workdir>/sta_cache/`)

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
Liberty/SDC/SPEF/VCD file *content*, `--top`, `hier`, `margin`, `--stats`, and
`kStaSrcSalt` (a build-time content hash of `pass/opentimer` + `pass/partition`
+ the `@opentimer` pin, so a timing-engine change drops every record with no
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

- **Flops/latches/memories are path boundaries**, not cells (pass.abc keeps
  them native; the Liberty stays combinational). Each consumed flop/latch Q or
  memory read port becomes a virtual primary input arriving at 0, so
  state-to-state segments are scored; din/en/addr cones end at their driving
  gate pins. A latch is a hard timing break: transparency and time borrowing
  are intentionally not modeled. Clock trees are not modeled. `min period ≈
  max_delay` up to setup/clock-skew terms.
- ABC's builtin tie cells (`_const0_`/`_const1_`) contribute no arrival.
- Primary inputs arrive at 0 with slew 0 unless an `.sdc` overrides them
  (`create_clock -period`, `set_input_delay/-transition`, `set_output_delay`;
  `[get_ports X]` targeting only).
- Multi-bit values traverse the netlist glue (`Get_mask`/`Set_mask`/... with
  constant masks) via the pin tracker, which rewires consumers to per-bit
  `port.N` nets. Tracker ids of trackable-node outputs are `n$`-prefixed
  internally so a region boundary port that pass.partition named after a
  source wire (e.g. a port literally called `get_mask_20`) cannot collide.

## Known limitations

- `pass.opentimer.power` (VCD-driven power) is registered but untested since
  the lgshell removal.
- The `margin` criticality coloring (`populate_table`/`backpath_set_color`)
  still has the TODO.txt bugs: it back-walks only the single worst edge and
  stops at flops, so it under-marks launch-to-capture paths. The JSON report
  does not depend on it, and neither does an STA cache hit: a hit replays the
  report but does not re-mark node colors. That marking is unobservable today
  (with the default `hier=true` it lands on the scratch flattened def, which is
  deleted, and this pass never saves its input library), so it is a limitation
  of the marking rather than of the cache — but a future consumer of those
  colors would have to be fed from the record.
- SDC support is the small subset listed above; no SPEF-less wire-load model
  (zero wire delay without SPEF).

## ODR warning (taskflow)

OpenTimer bundles its own taskflow (`ot/taskflow`). Everything else in the
`lhd` binary must use `@opentimer//:taskflow` — linking a second taskflow
version alongside corrupts `tf::Executor` at runtime. See
`packages/opentimer.BUILD`.
