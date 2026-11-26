# PCRS routing experiments

These tools reproduce the frame check and the positions-only experiment of the
first route-first plan (2026-11-23; its page is in the git history of
`todo/livehd/3r-routefirst.html`, up to commit 4d31a8b7e). That experiment kept
OpenROAD as the data router and is superseded by `rf_route` below. The runs use a
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

Use `retained_metal --all-layers layout.def geometry.tsv tech.lef cells.lef`
to compare a native handoff with the final design independently of DEF path
spelling. Each row contains the net, layer and four rectangle coordinates
in DEF units. This mode includes via cuts and both metal landings.

## Native track router (experimental)

`lhd route` routes every unrouted signal net of a legal DEF on M2 to M7 and writes
the DEF back with `ROUTED` wiring and the cells it moved. The method, the rule
table and measurements below use the original worker flag names. The public API and typed inputs are in [the driver reference](../../lhd/README.md#detailed-routing); what is still to do is
in [the route-first plan](../../todo/livehd/3r-routefirst.html). It calls no
external tool. The rule limits in the source are for ASAP7 and were measured with
OpenROAD's checker on two-wire layouts.

```sh
bazel build -c opt //lhd
bazel-bin/lhd/lhd route def:frame.def lef:asap7_tech.lef lef:asap7_cells.lef timing:legroute_plan0/timing.tsv \
  --set route.hweight=1.05 --set route.via_cost=120 --set route.pd_alpha=0.3 --set route.budget=300000 \
  --set route.shift=3 --set route.shift_reach=6 --set route.chain=4 --set route.flop_reach=2 \
  --set route.ripup=3 --set route.rip_width=6 --set route.final_passes=6 --set route.crit_frac=1 \
  --set route.tile_move=true --set route.reserve=true --set route.anchor_frac=0.1 --set route.prelegal=true \
  --workdir W --emit-dir def:routed
```

The options above are the measured setting, not defaults: with no options the
tool runs plain shortest-first routing with no repair.

| Option | Meaning |
| --- | --- |
| `--order short\|long\|pins\|fewpins` | net order; clock nets always go first |
| `--timing F`, `--crit-frac X` | pin slacks from the placer (`"inst:pin" slack_ps`); the worst fraction X of the nets goes first by slack, the rest by `--order` |
| `--long-frac X` | the longest fraction X of the data nets (by pin box) goes ahead of the rest, longest first |
| `--spread F`, `--spread-rounds N` | with `--prelegal`: before the row plan, the places the floating cells wish for move out of tiles filled above F of their free row area into less dense neighbours (cell shifting on the 32-track tiles, at most N rounds); the cells are not moved, the row plan and the sites still decide |
| `--pin-footprint P` | with `--prelegal`: a floating cell asks its row for the room its signal pins need at P pins per um2, on its right; given up for the whole design if the rows cannot hold it |
| `--pin-cost K`, `--pin-tile N` | M2 and M3 wire costs `1 + K * (pins / N - 1)` times more over a 32-track tile with more than N pins (default 27, 20 pins per um2 on ASAP7), so wires that only pass a pin-dense tile go over it on M4 and above; off at K = 0 |
| `--prelegal` | movable cells float at their input pose and take the nearest free legal site when a net first needs them; give the pose before legalization |
| `--clock-nets F` | clock nets, one per line, for a pose written before they were tagged `USE CLOCK`: a net name, or an endpoint `cell/pin` (or a port) that keeps its name when nets are renamed |
| `--space-frac X`, `--space-min-len N`, `--space-min-layer L` | the worst fraction X of the nets by slack whose pins span at least N nm keep the tracks next to their wires empty where still free, from layer index L up (0 is M2; default 5000 nm, M3) |
| `--verify-report F` | `--verify` plus one line per finding: kind, layer, nets and rectangle |
| `--time-limit S` | stop routing new nets after S CPU-seconds; what is left stays unrouted |
| `--only F` | route the nets listed in F alone and leave the rest unwired; for debugging one net on an empty chip |
| `--tile-move` | a cell that cannot be served in place moves to the cheapest free site of a nearby 32-track tile |
| `--anchor-frac X` | nets among the worst fraction X by slack fix their cells once routed |
| `--reserve` | in the final passes all pins of a blocked cell take their pads together |
| `--verify` | read the written DEF back and report rule violations, duplicate cuts and cell overlaps as `verify_*` in the statistics |
| `--hweight W` | A* heuristic weight; raised per search only when the budget runs out |
| `--via-cost N`, `--m2-cost`, `--m3-cost`, `--hi-cost` | via cost in nm of wire, cost per nm on M2, M3 and M6/M7 |
| `--pd-alpha A` | Prim-Dijkstra pin order from the driver for nets of 4 pins or more |
| `--shift N`, `--shift-reach S`, `--shift-cap C` | slide the cell of a blocked pin: tries per cell, sites per slide, sites from the input pose |
| `--chain K` | push a run of up to K cells by one site when no site is free |
| `--flop-reach S` | sites a clock sink may move; 0 keeps flops in place. Clock drivers never move |
| `--ripup N`, `--rip-width W` | rip-ups allowed per net, nets ripped per failure |
| `--final-passes P` | extra passes over the leftovers with a fresh allowance and a wider rip-up |
| `--budget N` | state expansions per search |
| `--report F`, `--explain N`, `--trace NET` | per-net status, the neighbourhood of N failed pins, the paths of one net |

`lhd route verify def:routed.def lef:tech.lef lef:cells.lef` checks an existing DEF. The check covers the
track rules, cut spacing and duplicates, the conductor check of `check_connections`, nets without wires, V1 landings
inside the pin they reach, and each cell's footprint against the original rows, placement blockages and taller cells.
It does not read OpenROAD's patch rectangles and expects landings inside the pin metal, so it over-reports on
OpenROAD routes.

`stats.json` has CPU time, routed and failed nets, wire length per layer, vias
and repair counts. A net left unrouted is written without wiring. The `--report`
file has one line per net with its pin count, box, status, the failing pin and
reason, and the search expansions and routing calls spent on it. `--trace NET`
also prints, for every failed search of that net, where the search went and
what the tracks hold around the pin it aimed at.

Check a result with an independent tool. `check_connections` covers
continuity. For design rules the review used OpenROAD's `drt::check_drc` on two
gcell grids and kept the markers both runs report, because that checker flags a
via pair under one wire when the pair straddles one of its box boundaries. The
scripts are in `repros/3r-routefirst/rf-review/` (local): `check2.sh`,
`conn.sh`, `sta.sh`, and the rule oracle `oracle.py` with `t2.py` and `t3.py`.

The rule model uses the ASAP7 constants at 1,000 DEF units per micron. It
requires preferred-direction tracks on M2 through M7 and 54-DBU site rows. A
row may come as several ROW statements on one site grid; the space between
them, hard placement blockages and cells taller than a row close the sites
they cover. A macro (a block, or any cell taller than a row) is no cell of a
row: every stretch it covers is closed. Rectangular routing blockages are
obstacles. Blockages with spacing, width, density or mask produce diagnostics.
A nondefault rule is served when it keeps the default widths and asks for at
most the spacing one empty neighbour track gives: such a net keeps the tracks
beside its wires empty where they still are (`ndr_spaced_nets`); any other
assigned rule is refused.

A macro pin is a bar on a routing layer at the macro edge, usually off the
track grid and under a blockage that covers the whole macro. The bar is
extended 200 nm out of the macro and reached by a via from a track of the
layer above or below that crosses it (`edge_pin_stubs`); the whole bar is
written as a wire of the net. One track above and one below keep a 300 nm
stretch in front of each such pin for its net (`edge_pin_landings_kept`), so
that other nets cannot wall the pin in.

The search state is one stop of one track, kept in hash tables that hold what a
search touched, so memory does not grow with the die (a 1.4 mm die has about
10^10 states). A connection gets a budget and a window in proportion to its
span. The search keeps the cheapest arrival at a position; when a connection
fails it is searched once more with arrivals kept apart by how far they are
from the via that began their run (`exact_run_retries`), because the cheapest
way onto a track, a via from the next track over, can never take a via of the
same cut layer one track on.
Wired signal endpoints stay at their input poses. With `--prelegal`, clock
drivers and sinks already on free legal sites keep them. Unwired clock cells
that need legalization take sites before data routing. Subsequent flop moves
are bounded relative to the legalized site. Names supplied in `--clock-nets`
must occur in the input DEF. Each net in a limited run’s TSV is labeled
`routed`, `failed` or `unattempted`.

Run the repository-owned CLI regressions with
`bazel test --config=backend //tools/routefirst:rf_route_test`.

## From placement to a routed design

What happens after global placement on eyeriss (times are wall seconds of the LiveHD run; each step was checked
against the source and the run's artifacts). The routing steps are `rf_route`, which is not a LiveHD pass yet.

1. Expand virtual buffers (only where a first-order cap or slew estimate fails), then wirelength resynthesis on
   cloned graphs; a batch is kept only if timing does not degrade and wirelength drops (339 s).
2. First PCRS clock tree, legalized, so timing sees a propagated clock (3 s). Since 2026-11-25 this step runs only
   with `synth.place.pcrs.initial=true`; by default steps 3 to 5 time an ideal clock. The times and the refusals
   below are of the run with the first tree. Without it the buffer sweeps of steps 4 and 5 are accepted (5 of 9) and
   the routed design times at 378 MHz against 161 MHz.
3. Timing-driven re-place and region refinement (685 s; the slack-weighted re-placement ran no round here).
4. Gate sizing and buffer-tree repair (197 s). The buffer sweep was refused by the hold guard.
5. Repair loop, three iterations of re-place, sizing and buffer repair (1,250 s). All 15 buffer sweeps refused by
   the hold guard.
6. Final PCRS clock tree: strip and rebuild with balancing from the optimized positions (69 s). Cells and clock
   nets, no wires.
7. Final frame and legroute planning (about 1,210 s): split rows, taps and endcaps, the power grid as obstacles, a
   2-D global route that rates every net (pin slacks, pose before legalization, congestion), feedback rounds kept
   only if routing improves. No wires are written.
8. Terminal legalization: critical nets first, rest by slack, legality check (37 s).
9. Power grid, IO pins snapped to tracks, wire RC, DEF written (about 470 s).
10. IO check and fast timing estimate (11 s). End of the flow. The DEF has no signal or clock wires.
11. `rf_route` setup: tracks, obstacles, clock nets, pin access (51 s).
12. `rf_route`: clock nets first, data nets by slack, repair ladder, optional route-first legalization (1,518 s).
13. `rf_route`: write the routed DEF and run the native check (80 s).
14. Referee outside the flow: connectivity, `drt::check_drc` on several grids, OpenROAD extraction and timing.

## rf_route: method

- Every wire is an interval on a track with an owner. A wire is inserted only
  if it keeps the four rules below, so there are no shorts to negotiate. Facing
  ends on the next track are repaired by lengthening one wire past the other.
- A pin is reached by a V1 whose M1 pad stays inside the pin metal (any x,
  18 nm stops on M2), or directly on its M2 strap when the cell has one (full
  adders, clock gates, scan flops). Macro pins are described above.
- Clock nets go first, then data nets by worst slack when the placer's pin
  slacks are given, else shortest first. Pins join in Prim-Dijkstra order from
  the driver. Each pin runs A* to the tree with a heuristic weight of 1.05,
  raised only when a search runs out of budget.
- Repair, in this order: slide the cell of a blocked pin by up to 6 sites, push
  a run of up to 4 cells by one site, move the cell to the cheapest free site of
  a nearby tile, then rip up the nets that hold the tracks. Clock drivers stay.
  Flops move at most 2 sites and their leaf net is rerouted like any other.
- Tiles are 32 M2 tracks on a side. A tile knows the track capacity the fixed
  metal leaves, the wires routed in it, an L-shape estimate for the nets still
  to route and the pins waiting for a pad. The site cost of a cell is its
  displacement plus the crowding of the tile.
- A net among the worst 10% by slack fixes its cells once routed.
- With `--prelegal` every movable cell floats at its input pose. Each has a row
  and a place in the left-to-right order of that row, and takes its site when
  the first net that needs it comes up, so the critical nets choose first.
- A leftover pin on a cell clears the tracks over that cell and all pins of the
  cell take their pads together. The pads stay as metal of their nets.

Rules for standard-width wires on tracks and the default vias (nm), from
two-wire layouts checked with OpenROAD. They hold inside one net as well.

| Layers | Same track, end to end | Next track, facing ends: forbidden gap | Minimum piece | Cut spacing |
| --- | --- | --- | --- | --- |
| M2, M3 | 31 | 0 to 19 | 37 | 18 (V1, V2) |
| M4, M5 | 48 | 0 to 39 | 84 | 34, Euclidean (V3 to V6) |
| M6, M7 | 48 | 0 to 39 | 69 | 34 |

## rf_route: measurements (2026-11-24)

One thread for `rf_route`. Fmax is from OpenROAD extraction and timing of each
routed DEF with the same constraints; every closed result passes the conductor
check and has no `check_drc` marker off a box line of the checker.

Small designs (LiveHD synthesis, placement, PCRS clock, power grid; OpenROAD on
one thread):

| Design | Nets | Flow | CPU-s | Wire (mm) | Vias | Fmax (MHz) |
| --- | --- | --- | --- | --- | --- | --- |
| minimax | 15,317 | legalized, OpenROAD | 1,247 | 78.3 | 173,127 | 1,038 |
| | | legalized, rf_route | 34 | 82.7 | 157,725 | 1,136 |
| | | route-first, rf_route | 37 | 80.3 | 154,388 | 1,142 |
| sha3 | 18,668 | legalized, OpenROAD | 2,179 | 152.1 | 236,346 | 1,207 |
| | | legalized, rf_route | 80 | 157.8 | 206,733 | 1,478 |
| | | route-first, rf_route | 73 | 147.3 | 190,662 | 1,543 |
| ternip | 21,055 | legalized, OpenROAD | 1,655 | 85.4 | 214,182 | 1,127 |
| | | legalized, rf_route | 53 | 89.8 | 201,825 | 1,299 |
| | | route-first, rf_route | 46 | 79.6 | 190,237 | 1,304 |

Eyeriss (1,419 um die, 1,040 FakeRAM macros, 2.5 ns clock; OpenROAD on 32
threads, its CPU is global plus detailed routing):

| Frame | Router | Nets routed | CPU-s | Wire (mm) | Fmax (MHz) |
| --- | --- | --- | --- | --- | --- |
| LiveHD flow, 282k cells | OpenROAD | all | 79,471 | 4,456 | 174.9 |
| | rf_route, legalized | 293,938 of 293,938 | 1,567 | 4,604 | 161.4 |
| | rf_route, route-first | 293,938 of 293,938 | 1,681 | 4,462 | 160.9 |
| | rf_route, route-first, `--space-frac 0.02 --pd-alpha 0.6` | 293,937 | 2,257 | 4,605 | 309.6 |
| ORFS netlist and macros, LiveHD placement, 415k cells | OpenROAD | all | 94,413 | 9,211 | 192.2 |
| | rf_route, legalized | 433,393 of 433,445 | 6,092 | 9,391 | not complete |
| ORFS placement, OpenROAD clock tree, 421k cells | OpenROAD | all | 84,809 | 10,927 | 159.0 |
| | rf_route, legalized | 437,773 of 437,773 | 4,195 | 11,106 | 237.2 |

What the measurements say:

- **Net order.** With slacks from the placer, worst slack first is best (sha3:
  75 s and 1,529 MHz against 136 s and 1,400 MHz for shortest first). Longest
  first is not faster on eyeriss and leaves more nets at the same time limit.
  The longest 5% of the nets ahead of the rest (`--long-frac 0.05`) on the
  LiveHD eyeriss frame: 14% less CPU, the same wire, 2% lower Fmax.
- **Tree order.** `--pd-alpha 0` (shortest tree) halves Fmax on sha3 and on
  eyeriss; 0.3 is best on the small designs. On the LiveHD eyeriss frame, whose
  worst paths go through unbuffered nets of 2 to 3.6 mm, 0.6 gives 224 MHz
  against 161, and 1.0 costs 79% more wire.
- **Spacing.** `--space-frac 0.02` gives 3 to 6.5% on the small designs and 28%
  on the LiveHD eyeriss frame for under 0.2% of wire. It still leaves 1 to 4
  nets unrouted on eyeriss.
- **Density.** `rf_route` closes what OpenROAD closes on sha3 (to 0.97
  utilization) and ternip (to 0.89, the placer's limit); on minimax both close
  0.87, OpenROAD closes 0.88 where route-first leaves 9 nets, and neither
  closes 0.92. Route-first shows no density advantage.
- **Route-first legalization** moves cells 21 to 50 nm on average against 519
  to 591 nm for the terminal legalizer, and uses 3 to 11% less wire than
  routing the legalized placement.
- **Repair.** Unrouted nets with no repair, rip-up, cell slide, both: minimax
  50, 8, 9, 3 and sha3 132, 50, 81, 38. Joint pads on 8 to 29 cells per design
  removed the last ones.
- **Checker.** `drt::check_drc` reports markers on the lines of its 7x7-gcell
  boxes that move with the gcell grid. At eyeriss size two grids share 40 to
  130 of them where their box lines coincide; a third grid leaves none.

Scripts and run directories are local: `repros/3r-routefirst/rf-review/` and
`domplace_runs/rf_eval/` (`eyeriss/` for the large design, `tools/ey_tab.py`
prints its table).

### Independent routing workers and agent feedback

For an ASAP7 LiveHD post-PCRS DEF, run a grid of separate native routers and route
the assembled design with the same binary. The inputs are the unrouted legalized
`out.def` and the LEFs exported beside it. Supply the tech LEF first.

```sh
bazel build -c opt //lhd
bazel-bin/lhd/lhd route chunk def:placed --set route.grid=2x2 --set lhd.jobs=4 --workdir W
bazel-bin/lhd/lhd route repair def:placed route:W/route --set route.radius=540 --workdir W
```

Each worker owns nets whose endpoints fall in one rectangle. It returns only
wires whose emitted paths stay 128 DBU inside its boundary. The coordinator
routes clocks, crossing nets, spills and failures on the original design
with the accepted metal present. With `--trunks-first` it routes clocks and
crossings before dispatch. The workers currently load the complete placement
and LEFs. This experiment measures routing parallelism and does not enforce
the placement worker's memory ceiling. Use `--plan fp_chunks.json` to route the rectangular or stepped regions of an
existing placement plan. The coordinator handles exterior pins and reports
blocked pin coordinates and failure counts by placement home.

`assembly.json` records ownership and reuse. `finish/feedback.tsv` gives the
failed pin, reason and search effort for each attempted net. `findings.tsv`
checks the whole written design for connectivity, placement and modeled
geometry violations. A zero process exit means the run finished. Check both
`unrouted` and `violations` in `feedback.json` for closure.

Use `--reuse trial --release 1 --out alternative` with the same inputs and
grid to try handing tile 1 back to the coordinator. A local repair instead
starts from the last routed DEF. It releases only paths that pass near the
reported blocked pins, routes the failed nets first, then reroutes displaced
nets and checks the whole design. `--all-pins` expands the windows to all
endpoints of the failed nets. Each trial uses a new directory, so a worse
result can be discarded. The macro LEFs, placement, and clock constraints
must match for extraction and timing comparisons.

With `rf_chunks --agent` or `rf_route --agent`, the final router keeps its
placement, conductors and search data resident. A JSON line reports routed
and pending mutable nets and up to twenty failures with coordinates. Send
one command per line. `status` reads the current state. `release NET` drops
one mutable route. `retry 300000 3 4 60` continues with an expansion budget
of 300000, three extra passes, four rip-ups and a 60 CPU-second round budget.
`finish` writes the DEF and requested checks. The route timer excludes time
spent waiting for agent commands. Frozen worker metal can be reconsidered
through the release or localized repair operations above.

For the same Unix-socket client as placement, pass
`--agent-socket /path/to/router.sock` in a private directory. Send requests
with `tools/lhd_gui.py --socket /path/to/router.sock status`,
`retry budget=300000 passes=3 ripup=4 seconds=60` and `finish`.
`rf_feedback --pin-neighbors` gives all ordinary nets on a blocked cell a
joint priority pass. This lets a pin move without stranding another pin
on the same cell. The repair also releases named wires from the native
geometry report and uses the marker and blocked-pin coordinates.
When two nets alternate as the last open, add
`--protect-failures earlier/feedback.tsv` from the same placement. The
priority pass routes both sets of nets together. On the independently
placed two-home NVDLA M frame, protecting the preceding round released
36 of the existing routes and closed the final open in 5.52 seconds wall.

With no placement plan or explicit grid, the ASAP7 starting grid is two
side-by-side homes through 150,000 placed components and four homes above
that size when four workers are available. Two homes gave fewer first-pass
opens on NVDLA M, A and P. On O four homes reduced the first-pass residual
from 4,811 to 1,651 at the same worker and assembly budgets. P has 100,603 components and O has 245,635. With a fixed total
worker budget of 480 CPU-seconds, two homes on O gave 1,413 opens in 595
seconds wall and four gave 1,706 in 473 seconds. Four homes remain the
starting point for that size when time to first feedback matters. Set `--grid NX NY` to explore a
different cut, or `--plan` to keep an existing floorplan's home regions.
A resident status reply counts deadline-deferred nets separately and lists
attempted failures first. Within clock and data groups, retry gives nets
that missed the preceding deadline a first search.

For a congested assembly, put selected open net names from `feedback.tsv`
in a file and pass `rf_feedback --targets nets.txt --pin-neighbors`. The
repair pass reopens nearby routes and works on that cohort. A fresh
whole-design readback preserves open nets outside the selected region in
the feedback for subsequent rounds.

`rf_feedback --reserve-first` tries to give the previously blocked cells
compatible M1 access pads before other priority nets take those sites.
It also enables the incident-net priority pass. Applied to the same
four-home NVDLA M checkpoint as the ordinary repair, this removed the
last open in one round. On two O cohorts it helped one and slightly hurt
the other, so the default continues to reserve on demand.

Use `rf_feedback --pin-access-layers` to limit spatial release around a
blocked pin to M1 through M3, including vias starting on those layers.
Incident nets requested with `--pin-neighbors` and nets named by a geometry
finding remain eligible on every layer. On the same 80-net O cohort, this
released 1,067 neighboring nets and left 372 opens. The full-layer window
released 1,858 and left 395. On C the two windows left 1,846 and 1,845 opens.

After a connected route, try a timing ECO from its saved DEF and verifier
statistics. Supply native placement pin slacks, the maximum number of
mutable signals, the previous route directory and the original LEFs.

```sh
lhd route repair def:placed route:W/route timing-eco:timing.tsv --set route.net_count=12 --set route.pd_alpha=0.6 --workdir timing-trial
```

The agent selects ordinary signal nets by worst endpoint slack, breaking ties
by routed wirelength, and retains the surrounding signal routes and all clock
wires identified by DEF or the timing file. The output includes the selected
net names, slacks, wirelengths, router command and whole-design findings.
Compare extraction on the original and candidate DEFs before accepting the
timing change. On the two-home A route, 12 signals raised the extracted core
frequency from 1,314.54 to 1,334.57 MHz with hold slack of −4.34 ps in both
runs. The same setting lowered frequency on the M and P routes.

## Measurements of 2026-11-25: steps after placement, pin density, NVDLA

Moved here from `todo/livehd/3r-routefirst.html` when that page was cut back to what is still to do. The page of that
day is in the git history at commit 132ec87bc.

### The steps after placement: what to change (2026-11-25)

The user annotated the list of steps from global placement to a routed design (`tools/routefirst/README.md`, "From placement to a routed design") with five proposals. Each was checked against the code and the eyeriss and NVDLA runs. The step numbers are those of that list.

| Step | Proposal | What the code and the runs say | Decision |
| --- | --- | --- | --- |
| 2 | Do not build the first clock tree. Time the first optimization steps with an ideal clock, because a tree pins the flops. | The tree does not pin anything: clock nets have no placement edges, there is no pull, and the final build strips it. What it does is switch both timers to a propagated clock on an unbalanced tree. On eyeriss hold slack is then −337 ps, and the hold guard refused all 19 buffer sweeps, although some of them took setup from −8,689 ps to −305 ps. Most of that hold is not skew: of 696 negative hold endpoints in the final report, the 279 worst have an input port in their cone, where a 500 ps input delay meets 1.2 to 1.5 ns of clock insertion. The tree also costs about 214 s and 357 µm² of the growth allowance. | **Yes, and it is the default since 2026-11-25.** `synth.place.pcrs.initial` (new) is false by default, so the first tree is skipped; `true` brings it back. NVDLA O agrees in direction (502.2 MHz against 490.4, setup −98 ps against −163 ps, hold −514 ps against −548 ps). Measured on eyeriss: routed and timed by the referee, 378.1 MHz against 161.4 MHz (setup slack −145 ps against −3,695 ps, one failing endpoint against 8,818), every net routed (309,039), 4,613 mm of wire against 4,604, and the placement flow 16% shorter. The buffer step accepted 5 of 9 sweeps, where the baseline accepted none of 19. Hold is worse, −518 ps against −451 ps and 22,774 failing endpoints against 8,115; 19,601 of them (95% of the total violation) are launched by an input port, and the register-to-register ones go from 2,328 (worst −166 ps) to 3,173 (worst −239 ps). Two fixes are needed whichever way that goes: shift the IO delays by the clock insertion once the clock is propagated, and let the hold guard trade a hold loss on an IO path. |
| 5 | Mix global routing and a pin-density measure into the repair loop, so the loop pushes cells apart where routing is impossible or pins are too dense. | The gap is real. On NVDLA O the pins the router fails on sit in 1 µm bins at the 97th percentile of pin density (median 30 pins/µm² against 12.9 for the design and a placer target of 8.3), in a core that is 26% full. Global placement measures congestion and pin density on a fixed 64 by 64 grid, 5.5 µm bins on O and 22 µm on eyeriss, so it cannot see these clumps, and every legalizer packs them again. But the existing vehicle is wrong: the plan's feedback widens cells and resumes global placement over the whole design, and its second round routes an overlapping pose against a legal one, so it was rejected on eyeriss, O and C (residual 1,712 to 11,241 on O). | **Yes, in the final legalizer and in the router; not through the placer's existing hook.** See pin density below for the measurements and the new options. |
| 6 | Assumed: registers are already attracted by clock skew or their clock gate before the tree is rebuilt. | Not by default. The only attraction is `synth.place.pcrs.leaf_pull`, default 0; the clock-cluster force is called only from its test. On eyeriss (no clock gates, 54,787 sinks) a sink is 9.4 µm from its leaf buffer on average, 85 leaves reach past 50 µm and 18 past 100 µm, and balancing was tried and rejected (offset 272 ps over a 125 ps limit). The PCRS page says the pull is on by default (K18); the code says 0, and the sweep that should pick the value (G5) is open. | **Keep it optional; pick the value in the PCRS sweep (G5).** Measured on eyeriss with 0.25: alone it gives no frequency (156.9 MHz against 161.4), a better hold slack (−362 ps against −451 ps), 2.7% less routed wire and a worse balance attempt (offset 605 ps over the limit against 272 ps). Together with `pcrs.initial=false`, where the buffer sweeps are accepted, it is a clock-quality lever and not a frequency one: 370.4 MHz against 378.1, setup skew 324 ps against 369 ps, register-to-register hold violations at 2,203 endpoints and −62 ns in total against 3,173 and −163 ns, 1.6% less wire. |
| 7 | The late plan should use pin density as well, and only update or check what an earlier plan did. | The late plan has no pin model ("Pin/via access is not modeled"). Its feedback round was rejected in every run, so today it only measures, at the price of two frame preparations and a discarded placement: about 1,210 s on eyeriss. The plan at the first clock tree, on planned obstacles, is cheap and clean (residual 55 on O, nothing unrouted); the 914 unrouted nets of O's final plan are static capacity cuts that spreading cannot fix. | **Yes.** Make the late step measure-only, add the pin channel, and move the acting part earlier (step 5). |
| 10 | Assumed: this step covers the chunk IOs; add a quick check across regions to fix them. | It does not. It checks synthesis colors (their arrival and required times at the color ports), and its result is discarded at the final stage. On eyeriss 49 of 79 colors meet the trigger and all are locked out because their earlier synthesis missed its target. Eyeriss was not chunked (one chunk). Region boundary pins are refined once, before sizing, and released; nothing looks at them after final legalization, and their bus order mostly did not survive even their own legalization (447 of 738 groups unordered). | **Not as a resynthesis.** A color rewrite after legalization needs a re-placement. For chunked runs, first observe: run the existing boundary observation on the final placement and compare crossing order and per-edge demand with the refined ones. That belongs to routing by chunks, where the slots are what the workers must respect. |

Findings on the way that are not in the proposals: the growth allowance refused 21,601 of 23,501 sizing candidates on eyeriss, the legalizer's padding is zero for the 10% most critical cells, and `timing_place` ran no round in either design because its evaluation budget was spent before it started.

### Pin density (2026-11-25)

NVDLA partition O (232,280 routable nets, core 25% full) is where the native router does not finish: 706 nets are open after 9,000 CPU-seconds (387 after its second extra pass), and OpenROAD routes the same placement completely. The 407 pins it fails on lie in 23 tiles of 20 µm, in bins whose median is 29 signal pins per µm² (the 96th percentile of the design), mostly inputs of AO222x2, AO221x1 and AO32x1. Over such a cell every M3 track is taken, many by wires one to two microns long that only pass: each pin needs its own way up through M2 and M3, so pin density is M3 demand. Partition C, reported as failing, routes completely without the 1,500 s cap of that campaign (280,621 of 280,621 nets, 2,820 CPU-seconds).

| Placement of O | Bins of 1.08 µm at 30 pins/µm² or more | Placement HPWL | Open nets (9,000 s limit) | Core clock, referee |
| --- | --- | --- | --- | --- |
| Baseline | 1,643 | 1.333 m | 706 (387 after pass 2) | 490.4 MHz (opens carry no wire); OpenROAD's route: 461.7 |
| Baseline, pads of 11,851 pin-dense cells reserved first | 1,643 | 1.333 m | 558 (354) | 490.0 MHz |
| `synth.place.density=0.5` | 1,618 | 1.357 m | 1,100 | not timed |
| `place.grid.route=320`, `place.route.pin_capacity=20` (the placer's pin inflation on a 1.1 µm grid) | 1,623 | 1.354 m | not routed |  |
| `place.legalize.padding_sites=2` | 899 | 1.382 m | 542 (251 after pass 3) | 501.8 MHz |
| `place.grid.density=256` | 334 | 1.719 m | 41, converged in 6,326 s | 451.6 MHz, hold −846 ps against −548 |
| `place.legalize.pin_density=20` (new) | 742 | 1.391 m | not routed |  |
| `place.legalize.pin_footprint=30` (new) | 1,018 | 1.367 m | 253 (228 after pass 3) | not timed |
| `place.legalize.pin_footprint=25` (new) | 761 | 1.379 m | 175 (128 after pass 4) | not timed |
| `pin_footprint=25`, `pin_padding_critical=true` | 153 | 1.389 m | 194 (120 after pass 5) | not timed |
| `pin_footprint=20`, `pin_padding_critical=true`, `pin_padding_max=5` | 21 | 1.413 m | 51, converged in 6,362 s | 469.9 MHz |
| The same with `synth.place.density=0.45` (spread earlier, in global placement) | 16 | 1.400 m | 103 (55 after pass 5) | not timed |
| Route-first from the pose before legalization, plain (`route.prelegal`) | 4,742 after the cells settle | routed wire 1,758 mm | 568 (470 after pass 2) | not timed |
| Route-first from the pose before legalization: `route.prelegal`, `route.spread=0.8`, `route.pin_footprint=25` (new) | 26 after the cells settle | routed wire 2,125 mm against 1,789 | 27, converged in 5,562 s | 461.9 MHz |

What this says. Spreading by pin density works (41 open nets against 706) and moderate spreading costs no frequency, but spreading the whole design does (8% on O). A lower density target does nothing. The placer's own pin inflation does nothing either, on any measurement grid: before legalization the pose has clumps of 90 to 500 pins per µm² with it and without it, because the placer's forces act on the 64 by 64 density grid and cannot shape what is inside a 5.5 µm bin. What the final placement shows is what the final legalizer enforces, and that legalizer gives its 10% most critical nets' cells (33,277 of 230,262 on O) no padding at all.

New, all off by default:

- `synth.place.legalize.pin_footprint=N`: a cell is at least as wide, with its padding, as its signal pins need at N pins per µm². It depends on the cell alone. `pin_padding_critical=true` applies it to the critical cells too, `pin_padding_max` caps the extra sites per side, and a cell gives the extra up when it adds more than `pin_padding_rows` rows to its move.

- `synth.place.legalize.pin_density=N`: extra padding only for cells whose input pose lies in a bin above N pins per µm². Weaker than the footprint: a cell pushed into a cool bin carries no padding.

- `route.pin_cost=K`, `route.pin_tile`: M2 and M3 wire costs more over tiles with more pins than the limit, so wires that only pass go over them on M4 and above. No loss on C (all nets, 8% less CPU, 319.2 against 320.5 MHz). On O it lowers the open nets after the first pass by a quarter to a third (753 to 555 or 539) and does not change where the run ends.

The user's direction is to have pin density in global placement from the start, before any timing iteration, so that the flow adapts to the wire it costs. The footprint is the form of it that can be given to every step: the same number as a floor on the cell's charge in global placement from the first iteration, and as padding in each legalizer. The placer half needs a per-cell inflation floor in DREAMPlace and is not built. One sample points the same way: with the placer's fine pin grid on and the legalizer padding, O ends at 2.0% more wirelength and +128 ps of estimated setup slack, against 3.0% and −44 ps with the legalizer padding alone.

Afternoon of the same day, after the zoom on five unrouted pins (in each, the M3 track above the pin carries another net and 83% to 100% of the M3 tracks over the cell are in use). The open nets follow the number of bins at 25 pins per µm² or more: 4,153 bins and 387 nets, 3,008 and 228, 2,068 and 128, 1,131 and 120, 317 and 45. Three ideas of the user were tried:

- **Flip a cell whose pin stays blocked**: no gain, and removed again. The router flipped 12,628 cells on the baseline placement, 7,926 on the footprint one and 8,537 in route-first, and ended at 435 open nets after pass 2 against 387, at 59 against 51 and at 40 against 27. The M3 over the whole cell is full, so another column of the same cell is taken too. A cell that comes mirrored in the input still keeps its mirror when it moves.

- **Avoid the clouds earlier, in global placement**: a lower density target under the same legalizer footprint gives 1% less wirelength and 12% less legalizer movement, and routes no better. Not worth a placer change on this design.

- **A translation layer for route-first** (`route.spread`): before the row plan, the places the floating cells wish for are moved, not the cells. Tile boundaries shift from a tile over the target fill into a less dense neighbour, which is compressed, and the wishes move with them (the cell shifting of FastPlace). With the room each cell asks for its pins (`route.pin_footprint`) this is the best result on O: 27 open nets, the frequency of OpenROAD's route, 19% more wire. Plain route-first on the same pose stays at 470 to 568. A cap of 20 instead of 25 gives 34 open nets for 29% more wire, so the cap is not the lever any more.

What is left is not the clouds. The 27 nets end in bins of 19 pins per µm² at the median, and 16 of them on an input of an AO222x2 (B1, B2, C1, C2), mostly on select nets of 18 to 34 pins: four pins on consecutive M1 columns that share two or three M2 tracks. That needs the router to give the pads of such a cell together and to hold their way up, which is not built.

Route-first on O needed one repair first. Floating flops never took a site, and 2,151 nets failed for it: the test of a cell's own M2 bars against the metal already there used the wide-metal spacing for every shape and refused the bars 27 nm from the M2 power rail of every row. The test now reads a shape the way it is entered (a standard bar on a track meets only what is on that track).

Found on the way: at density targets 0.45 and 0.5 the placer died with "kernel moved a fixed node". Its runaway guard returned an older sample in which the boundary terminals had not yet been moved by a terminal update. Fixed in DREAMPlace 385d8e0.

### Explored and closed

- **A share of the longest nets first** (user, 2026-11-24). All longest first is not faster and leaves more nets. A share ahead of the rest (`--long-frac`) was measured on the LiveHD eyeriss frame: with the longest 5% first the run took 1,349 CPU-seconds against 1,567, with the same wire (4,604 mm), every net routed and 157.9 MHz against 161.4. So it saves 14% of the CPU and costs 2% of the frequency; the setting is neutral on timing and is not a default. The same runs on the other two frames were stopped before they finished.
- **Long nets without buffers.** LiveHD leaves nets of 2 to 3.6 mm behind one buffer on eyeriss, and the worst paths run through them in OpenROAD's route and in `rf_route`'s. In the eyeriss run every buffer-repair sweep was refused by the hold guard (4 in the repair step and 15 in the three repair-loop iterations): hold slack would have fallen from −337 ps to −410 to −610 ps, against an allowance of 50 ps. Setup would have improved. Either the guard needs to trade hold against setup per path, or hold repair must come after routing (see 3s-postroute), or the sweep must buffer the long nets alone. This belongs to placement (2w-wlresynth and the virtual buffer trees); the router can only limit the damage.

### Chunk routing on NVDLA (grid workers, feedback repair)

**Native grid experiment.** `rf_chunks` routes tile interiors in separate processes, reserves 128 DBU at seams and gives crossing nets and spills to the coordinator. It can also route the crossing nets first. The resident router reports failed pins to the agent. `rf_feedback` releases nearby wires, routes the failed nets first, and verifies the assembly. Four interior workers on ASAP7 NVDLA M retained 18,545 routes. The first assembly had nine opens. A 540 DBU repair released 231 nets and left one open; a 1,400 DBU repair released 95 and closed it. Extraction at the 1,500 ps constraint gave 1,141.81 MHz, versus 1,148.21 MHz flat and 1,108.20 MHz for four-thread OpenROAD on the same PCRS placement. M2–M7 routing, extraction and timing used identical LEFs and constraints. The workers currently load the whole input frame. The two-terminal boundary-slot experiment below exercises independent halves. Multi-terminal boundary trees and memory-bounded routing packets remain open.

The native router now runs inside `lhd route`. `lhd route chunk` consumes the same synthesis DEF bundle and optional placement regions, schedules separate workers using the running driver, and verifies the assembly. `lhd route repair` accepts the preceding checkpoint. The driver reference gives the public commands and socket protocol.

On NVDLA M, a second 4 by 2 run reused all eight workers. Repair closed the ten remaining nets, and the exported DEF passed `lhd route verify`. Its bytes matched the preceding standalone router on the same placement and settings. A separate run using the two floorplanner regions left thirteen opens after assembly. Pin-neighbor repair reduced those to one, and pin-access reservation closed the last net. Readback through `lhd route verify` reproduced the published checkpoint findings on all five NVDLA partitions. M, A and P were complete. O and C had 268 and 802 open nets respectively, with no other native geometry findings.

### ASAP7 NVDLA placement and route feedback

The comparison starts both routers at the same native synthesized, powered PCRS frame and uses the same LEFs, clocks, extraction and timing reader. LiveHD produced all signal routes in the native columns. Every frequency below has a completed whole-design route and extraction. Independent detailed-rule markers remain sensitive to checker grid placement.

| Partition | Flat native MHz | Chunk native MHz | Matched OpenROAD MHz | Chunk setup / hold, ps |
| --- | --- | --- | --- | --- |
| M | 1148.21 | 1142.44 (8 homes) | 1108.20 | +582.67 / +10.27 |
| A | 1307.47 | 1335.15 (2 homes, timing ECO) | 1269.38 | +283.68 / −4.33 |
| P | 819.56 | 825.84 (2 homes), 842.80 (4 homes, timing ECO) | 804.20 | −239.35 / −302.34 (2), −271.31 / −353.52 (4) |
| O | 3,871 opens | 268 opens | 461.70 | Incomplete route |
| C | 12,025 opens | 802 opens | 264.14 | Incomplete route |

The final native readback reproduced the complete M, A and P DEFs byte for byte with zero connectivity or geometry findings. At O (268 opens on 232,280 routable nets) and C (802 on 280,621), the remaining native findings were open nets. OpenROAD fully routed all five starting placements with zero final router-reported markers. On C, eight independent workers completed a search for each of the 656 nets deferred by the preceding pass. Their 51 additional routes assembled without any new geometry findings. The O reference ran with 24 threads and the C reference with 32. Their core setup / hold were −165.93 / −740.41 ps and −2285.93 / −3005.94 ps, respectively. At 12 AO222x2 cells on O the agent reopened 79 incident nets. The original poses left 268 total opens and the legal mirrored poses left 270. Both repaired eight of the 20 targeted opens.

The grid workers initially left fewer opens with two homes on M, A and P. Four homes reduced O’s first-pass residual from 4,811 to 1,651 under the same budgets. The grid starting point uses the smaller cut through 150,000 components and four homes above it when four workers are available. Explicit geometric regions still follow the supplied floorplan. The primary comparison ranks connectivity and geometry first and reports setup, hold, frequency and wirelength separately.

With four available processes, 480 aggregate worker routing CPU-seconds and 300 coordinator CPU-seconds, O left 1413, 1706 and 2698 opens for 2×1, 2×2 and 4×2 in 595, 473 and 475 seconds wall. C left 29,172, 29,766 and 34,377 opens for 2×2, 4×2 and 4×4 in 565, 595 and 654 seconds. More homes helped C when each extra home received a full budget. At a fixed aggregate budget, four homes gave the best C result. P has 100,603 components and O has 245,635, on opposite sides of the starting-grid threshold.

Placement feedback also changed the result. With the same home boundaries and saved macro handoffs, 950 final-placement iterations gave the lowest assembled cell-origin HPWL among 0, 80, 300, 600, 950 and 1,500 on each of A, C, M, O and P. For M, the 300-iteration native route gave 1067.10 MHz and −44.46 ps hold. The 950-iteration native route gave 1092.53 MHz and −35.07 ps hold. OpenROAD on the latter placement gave 1079.26 MHz and −41.59 ps hold. This experiment also changed the wire routing and repair sequence. Use extracted timing to decide between the legal finalists.

Timing feedback can also be applied after connectivity closes. On A, selecting 12 ordinary nets by their worst native endpoint slack and rerouting them raised frequency from 1314.54 to 1334.57 MHz. A second 24-net trial reached 1335.15 MHz. The same 12-net policy reduced frequency on M and P. Each trial retains its parent DEF and runs a whole-design connectivity and geometry check before extraction.

On the independently placed P frame, OpenROAD pin access failed on six SRAMs. Each failure was the bottom M3 pin centered 216 DBU from the macro's left edge. Moving the six legal footprints by 9–27 DBU horizontally aligned those pins to M3 tracks. The native route still missed one of the six. A vertical sweep found several legal positions where it could route the final pin. On the selected placement, both independent pin access and native routing of all six signals passed. Reopening the 606 incident nets in an existing partial assembly retained the rest of its signal wiring. Total opens went from 4986 to 4851 with no native geometry findings.

A centerline audit measured the fraction of routed signal nets spanning the two-by-two grid and their share of total routed wire. M measured 4.62% / 37.00%, A 3.03% / 40.90%, and P 2.46% / 21.58%. On the partial O and C controls the corresponding values were 1.67% / 16.53% and 2.12% / 13.38%. At 32×32, crossing nets carried 83–90% of routed wire across the five controls. The percentage counts the whole routed tree of each crossing net. Use this when estimating how much work an interior-only split leaves with the coordinator.

For eight sampled two-terminal crossings per partition, independent left and right native workers routed to a shared terminal. Replacements that conflicted after assembly rolled back to their control wires. Across M, A, P, O and C, initially estimated slots gave respectively 3, 2, 1, 3 and 6 accepted replacements. Reassigning failed slots using neighboring-wire feedback raised those counts to 7, 3, 6, 4 and 6. Using the actual terminal from a native routed control gave 8, 8, 8, 8 and 7. Production chunking still gives crossing nets to the coordinator.

[NVDLA routing evolution and retained experiment results](https://chatgpt.com/space/page_c10e89cb8eb081919d2328dac8806bdb)
