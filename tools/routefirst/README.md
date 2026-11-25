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

`rf_route` routes every unrouted signal net of a legal DEF on M2 to M7 and writes
the DEF back with `ROUTED` wiring and the cells it moved. The method, the rule
table and the measurements are at the end of this file; what is still to do is
in [the route-first plan](../../todo/livehd/3r-routefirst.html). It calls no
external tool. The rule limits in the source are for ASAP7 and were measured with
OpenROAD's checker on two-wire layouts.

```sh
bazel build -c opt //tools/routefirst:rf_route
bazel-bin/tools/routefirst/rf_route --hweight 1.05 --via-cost 120 --pd-alpha 0.3 \
  --shift 3 --shift-reach 6 --chain 4 --flop-reach 2 --ripup 3 --rip-width 6 --final-passes 6 \
  --budget 300000 --timing legroute_plan0/timing.tsv --crit-frac 1 \
  --tile-move --reserve --anchor-frac 0.1 --verify --prelegal \
  --report nets.tsv frame.def routed.def asap7_tech.lef asap7_cells.lef > stats.json
```

The options above are the measured setting, not defaults: with no options the
tool runs plain shortest-first routing with no repair.

| Option | Meaning |
| --- | --- |
| `--order short\|long\|pins\|fewpins` | net order; clock nets always go first |
| `--timing F`, `--crit-frac X` | pin slacks from the placer (`"inst:pin" slack_ps`); the worst fraction X of the nets goes first by slack, the rest by `--order` |
| `--long-frac X` | the longest fraction X of the data nets (by pin box) goes ahead of the rest, longest first |
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

`rf_route routed.def - tech.lef cells.lef` only runs the check of `--verify` on an existing DEF. The check covers the
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
2. First PCRS clock tree, legalized, so timing sees a propagated clock (3 s).
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
bazel build -c opt //tools/routefirst:{rf_chunks,rf_feedback}
bazel-bin/tools/routefirst/rf_chunks --grid 2 2 --workers 4 --out trial frame.def tech.lef cells.lef
bazel-bin/tools/routefirst/rf_feedback --radius 540 --out repair trial/finish tech.lef cells.lef
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
side-by-side homes through 100,000 placed components and four homes above
that size when four workers are available. Two homes gave fewer first-pass
opens on NVDLA M, A and P. On O four homes reduced the first-pass residual
from 4,811 to 1,651 at the same worker and assembly budgets. The threshold
lies between the tested design sizes. Set `--grid NX NY` to explore a
different cut, or `--plan` to keep an existing floorplan's home regions.
A resident status reply counts deadline-deferred nets separately and lists
attempted failures first. Within clock and data groups, retry gives nets
that missed the preceding deadline a first search.
