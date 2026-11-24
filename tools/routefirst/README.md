# PCRS routing experiments

These tools reproduce the frame check and the positions-only experiment in
[the route-first plan](../../todo/livehd/3r-routefirst.html). The runs use a
site-legal ASAP7 DEF with complete PG, legal frozen PCRS anchors and independently
connected FIXED clock/reset wires. Each ordinary data net remains available to
the signal router, including nets at flop D and Q pins.

Build the experiment binaries from the repository root.

```sh
bazel build -c opt //tools/routefirst:pin_options //tools/routefirst:frame_trees //tools/routefirst:check_connections \
  //tools/routefirst:row_probe //tools/routefirst:retained_metal
```

To redistribute existing whitespace, run the native pin-access oracle through
the F1 driver. Use the same technology and cell LEFs that produced the placement.
The driver handles single-height horizontal rows, reads the M2 tip spacing and
site pitch, and preserves cell order, all firm anchors and routed geometry.

```sh
python3 tools/routefirst/place_for_access.py \
  --def frozen.def --lef asap7_tech.lef --lef asap7_cells.lef \
  --oracle bazel-bin/tools/routefirst/pin_options --out trial
```

Route both `frozen.def` and `trial/positions.def` in OpenROAD using identical
libraries, SDC, layer settings, seed and threads. The primary runs used four
threads, routing from M2 to M7 for signals and M4 to M7 for clocks, and a layer
adjustment of 0.25. Add the CPU time in `trial/selection.json` to the positions
arm. Each candidate batch and its pin accesses remain in the output directory.

To inspect one failing net, select it with `--net`. Each output row identifies
the cell, pin, net, search exhaustion flag and feasible escape intervals.
The filtered invocation also reports rejected accesses on standard error.

```sh
bazel-bin/tools/routefirst/pin_options --net net_497 frozen.def access.tsv asap7_tech.lef asap7_cells.lef
```

To replay the original timing priorities on the frozen frame, pass the selected
legroute round's pre-legalization DEF and pin timing file. The replay uses actual
protected metal and via landings and the native PG geometry. The tree TSV lists
each physical terminal and its tile, including nets whose terminals share one
tile. Tree edges carry tile IDs in the grid described by the JSON report.

```sh
bazel-bin/tools/routefirst/frame_trees frozen.def legroute_plan0/input.def legroute_plan0/timing.tsv \
  trees.tsv capacity.json asap7_tech.lef asap7_cells.lef
```

Check the finished DEF against actual library pins. Every multi-terminal
regular net has a row in the output. A zero in `connected` identifies a
disconnected or unsupported case. Check design rules separately on the
routed ODB and DEF. In the partial-FIXED experiment OpenROAD reported a routed
design while the far ends of 37 nets remained open.

```sh
bazel-bin/tools/routefirst/check_connections routed.def nets.tsv asap7_tech.lef asap7_cells.lef
```

Run-specific command lines, source hashes, independently checked marker
reports and comparison tables are retained in `repros/3r-routefirst/`.

The fixed-choice row solver accepts a finite ordered site domain for each cell
or M2 piece end. A threshold implication encodes each difference constraint.
A graph cut minimizes the signed linear wire cost. Infeasible input produces
a positive cycle for incompatible bounds, or a discrete-site conflict. The
row probe accepts `V "name" integer_cost site ...` and
`E "from" "to" integer_gap "reason"` records. A gap requires the second
coordinate to exceed or equal the first plus that gap. Run its fixture with
`bazel test -c opt --config=backend //tools/routefirst:row_solve_test`.
The planted full-design result and its control live under
`runs/leaf-revision/minimax/positions-clean/plant/` in the experiment report.

In the external planted replay, the standalone pin-escape model omitted valid
contacts. The retained-M2 variant enumerates V1 sites against merged strips and
the oriented pin library. A local independent DRC scan selects alternatives.
The final full-layout DRC, frozen-metal polygon comparison, and net continuity
result are in `plant/checked-access/accepted2/physical-audit.json`.
