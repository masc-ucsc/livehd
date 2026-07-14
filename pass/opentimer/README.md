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
- **One module per run** (one `ot::Timer` holds one design): `--top` picks the
  def out of the netlist library. Time a region module (`<mod>__c<N>`), a
  single flat module (`pass.color flat` + `pass.abc` map the whole design as
  one module — the preferred whole-design route), or pass
  `--set pass.opentimer.hier=true` on a *hierarchical* netlist: the instance
  hierarchy is structurally flattened into a scratch def
  (`pass/partition/flatten.cpp`; node names keep the dotted instance path, the
  report keeps the real top name) and timed as one module, so the critical
  path spans modules. Without either, the rebuilt netlist *top* (which
  instantiates region modules, not Liberty cells) is rejected — a Sub that is
  not a Liberty cell is a hard error, never silent garbage.
  (`hier=stitch` keeps the legacy name-stitched hier walk for debugging; its
  multi-bit module-boundary buses are not stitched.)
- Options: `--set pass.opentimer.margin=<0-100>` (criticality coloring
  threshold), `--set pass.opentimer.qor=FILE` (report path; `lhd pass
  opentimer` defaults it to `<workdir>/timing.json` under `--workdir`).

## The timing report (`timing.json`, envelope `"qor"` member)

```json
{"schema_version":1,"kind":"sta","designs":[
  {"module":"m__c0","max_delay":0.6,"critical_pin":"g96_XOR2x1:Y",
   "critical_src":"design.prp:11",
   "endpoints":[{"pin":"…","delay":…,"src":"file:line"},…]}]}
```

`max_delay` is the worst MAX-corner arrival over all gate output pins (library
time units, e.g. ns). `endpoints` lists the 10 worst arrivals. `src` resolves
each gate's `srcid` — pass.abc's source-map carry-through — so the critical
path points back at the pre-synthesis RTL line an agent should edit. Under
`lhd pass opentimer --workdir W` the report is also embedded verbatim as the
result envelope's `"qor"` member (same channel as pass.abc's `abc-map` QoR;
discriminate by `"kind"`). Every annotated gate output also gets the
`pin_delay` pin attribute in-graph.

## Timing model

- **Flops/memories are path boundaries**, not cells (pass.abc keeps them
  native; the Liberty stays combinational). Each consumed flop Q / memory read
  port becomes a virtual primary input arriving at 0, so flop-to-flop segments
  are scored; din/en/addr cones end at their driving gate pins. Clock trees
  are not modeled. `min period ≈ max_delay` up to setup/clock-skew terms.
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
  does not depend on it.
- SDC support is the small subset listed above; no SPEF-less wire-load model
  (zero wire delay without SPEF).

## ODR warning (taskflow)

OpenTimer bundles its own taskflow (`ot/taskflow`). Everything else in the
`lhd` binary must use `@opentimer//:taskflow` — linking a second taskflow
version alongside corrupts `tf::Executor` at runtime. See
`packages/opentimer.BUILD`.
