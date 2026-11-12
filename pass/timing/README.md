# Fast timing engine (`pass/timing`)

A small native timing engine for the loops of synthesis and placement, in single precision, that times
several Liberty corners in one walk. Plan: [`todo/livehd/2q-fasttimer.html`](../../todo/livehd/2q-fasttimer.html).
OpenTimer stays for the report and for validation. Namespace `livehd::timing::fast`.

This directory has steps A, B (numeric binding, the graph binder and the SDC reader), C, the first parts of D
(the Elmore solver, numeric trees from the placer) and E to G behind `synth.timing.engine`. Not here yet:
engine-owned wire trees (the rest of D), synthesis (H), calibration (I), hold repair (J), chunking (L).

## Layers

| File | What it is |
|---|---|
| `cell_types.{hpp,cpp}` | The cells of N Liberty corners as one table of types. Pins, arcs (delay, launch, setup, hold), pin capacitance and electrical limits per corner, NLDM tables pooled in `float` with shared axes. `lookup` is the interpolation of `liberty::timing::Table::lookup`. |
| `netlist.{hpp,cpp}` | A mapped netlist as numbers: cells with a type, pins as consecutive indices, nets with one driver and a list of sinks. Ports are cells of two built-in types. `finalize()` levelizes the pins; a combinational loop is cut at one pin and the cut is listed. |
| `engine.{hpp,cpp}` | Late arrival and slew forward, required time backward, for every corner; early arrival on the hold corner; endpoint slack, per clock WNS/TNS and achievable period; criticality; electrical limits; the worst path. |
| `bind_graph.{hpp,cpp}` | A flat mapped hhds graph to a `Netlist`: Liberty Sub instances, the bits of the ports, and the wiring nodes (`get_mask`, `set_mask`, `concat`, constant shifts) resolved bit by bit down to the driving pin. `Bound_names` gives the constraint reader the instances and nets of the graph by name. |
| `sdc_tcl.{hpp,cpp}` | The small Tcl that constraint files are run in: words, braces, quotes, `$var`, `[command]`, lists, `expr`, `if`, `foreach`, `for`, `while`, `proc`, `catch`, `error`, `format`, `string`, `regexp`, `lsearch`, `lsort` and the other list commands. |
| `sdc.{hpp,cpp}` | The SDC commands on top of it: a constraint file run against a design gives a typed `Sdc` (clocks, IO delays, loads, driving cells, exceptions, clock groups, limits) and the flat list of the commands it ran. |
| `sdc_resolve.cpp` | A typed `Sdc` to the engine's `Constraints` for a netlist: objects to pins, clocks to source pins and clock relations, exceptions to cut pins, excluded endpoints and launch groups. |
| `cell_types_probe.cpp`, `fast_timing_probe.cpp` | Manual tools, below. |

## What it times

Combinational delay arcs; rising-edge flip-flops with ideal clocks (clock to output, setup and hold); primary
inputs (delay, transition, a driving cell) and outputs (delay, load). Cell delay and output slew are looked up
at the input slew and the **total** load of the net (pin capacitances, plus a wire model when one is given);
rise and fall are kept apart; the worst arrival and the worst slew win at a merge. Hold is the fastest corner
with minimum arrivals. The clock is ideal: the network from a clock source to the clock pins of flip-flops is
not timed. A clock source is a port or a pin of a cell (the output of a macro, a divider), and the network of
a clock ends where another clock is made (a generated clock). An output port inside a clock network forwards
the clock and is not a data endpoint. The electrical checks are the Liberty slew and capacitance limits and
the fanout limit of the design (`set_max_fanout`).

Everything else is listed in `Report::unsupported`, never dropped silently: latches, memories and clock
gates; falling-edge elements; arcs the Liberty model could not read; sinks driven by a constant; pins behind
a combinational loop (cut, so approximate); flip-flops whose clock does not come from a clock source.

## Launch groups

Every start point launches in one **group**: the flip-flops of a clock in the group of that clock, an input
port in the group of the clock its delay names. The arrivals, slews and required times of the groups are kept
apart (the state of a pin is per group, per corner and per transition), and an endpoint checks each group
against the relation between that group and its capture clock:

- the setup relation is the time from a launch edge to the next capture edge (the period, for one clock;
  the smallest distance over the common period for two clocks of different periods), the hold relation the
  capture edge that must not catch the data;
- a false path or an asynchronous group between two clocks makes the relation infinite: no check;
- a false path from some start points to some endpoints removes those endpoints for that group only;
- `set_max_delay` is a relation too: between two clocks, or of some endpoints (for every group, or for the
  group of the ports it starts from).

Two clocks get the same group when they relate to every capture clock in the same way, so a design with one
clock, or with several clocks of one period and no exception between them, has one group and costs what it
did before. With G groups the per-pin state and the walks are G times larger; a group that does not reach a
pin costs no table lookup there. `Engine::arrival(pin, corner, transition)` is the latest over the groups and
`required` the earliest; `slack` pairs each group's arrival with its own requirement.

## Criticality

Per clock, the achievable period is the period minus the worst setup slack over the setup corners. When the
target is not met (slack negative) slack is rebased so the worst path has zero, per clock and per corner;
when it is met the SDC period is the reference. Criticality is `clamp(1 - slack / window, 0, 1)` with
`window = window_fraction * achievable period` (10% by default), and the largest over the corners. `rank()`
leaves the required times rebased; `evaluate()` ends with the true ones.

## Measurements

sky130 hd and asap7 RVT (OpenROAD's test copy), 2026-11-11, one thread.

**Cell tables** (`cell_types_probe`, parity against `liberty::timing::Table::lookup` on random queries
including extrapolation):

| Corners | Liberty text | Load | Tables | Axes | Pool | Worst error |
|---|---|---|---|---|---|---|
| sky130 ss, tt, ff | 38.6 MB | 0.63 s | 17,274 | 798 | 5.3 MB | 0.005 ps (3.5e-6 relative) |
| asap7 RVT ss, ff (5 files each) | 92.4 MB | 1.43 s | 41,772 | 8 | 10.2 MB | 0.002 ps (2.7e-6) |

**Designs** (`fast_timing_probe`, mapped by `lhd synth synth.place=false`, tt corner, zero wire, primary inputs
launched at 0, clock 10 ns). OpenTimer is `lhd pass opentimer` on the same netlist and library.

| Design | Cells | Evaluate (1 corner) | Evaluate (3 corners) | Engine B/cell (1 / 3 corners) | Longest arrival vs OpenTimer |
|---|---|---|---|---|---|
| picorv32 | 9,463 | 7 ms | | 312 | +1.1% (top ten endpoints +1.0..+2.0%, same order) |
| dino | 30,375 | 36 ms | 61 ms | 315 / 584 | +1.3% (top ten +1.0..+1.3%, same order) |
| minion | 648,980 | 0.73 s | 1.35 s | 307 / 568 | +0.4% (top ten +0.40..+0.41%, same order) |

OpenTimer on minion: 45 s and 6.4 GB for the pass (about 6 KB per cell is its timer model). The engine's state
is 199 MB for one corner, plus 64 MB for the netlist (27 B per pin); the process was at 1.1 GB, almost all of it
the flat hhds graph and the library that the binder reads (`bind` is 3.3 s, flatten 2.2 s: graph access, not
timing). The engine is consistently 0.4 to 2% above OpenTimer, which is the cost of taking the larger of the
rise and fall pin capacitance and the worst slew at a merge.

Minion has 4,004 combinational loops (mostly state built from gates); OpenTimer cuts them silently and its ten
longest endpoints are all behind them. Without the cut those pins were untimed and the comparison was empty.

## Wire, SDC and placement ranking

`wire.hpp`: the Elmore delay and impulse variance of one RC tree (Ω, fF, ps), the same moments as OpenTimer's
`Rct`. `sdc.hpp`: the constraint reader, below. `physical_rank.hpp`: the engine
behind the placement ranking callback, selectable with `--set synth.timing.engine=opentimer|fast|compare` (compare runs
both and logs the agreement per call in the placement's `pass.opentimer.incremental` log). `--set synth.timing.engine=fast`
also runs the detailed evaluations and the what-if edits of repair on the engine (`Physical_ranker::whatif()`: resize,
add_cell, set_net with rollback, settled incrementally; OpenTimer stays for the final report).

## The constraint reader (SDC)

An SDC file is a Tcl script and the benchmark files use it as one, so `read_sdc` runs the text in a small Tcl
interpreter against the design and returns typed data. It is the one reader of the flow: the fast engine takes
the typed `Sdc` through `resolve`, and `pass.opentimer` takes `Sdc::commands` (the script as the flat list of
the SDC commands it ran: no variables, expressions or control flow; object lists as `[get_ports {a b[3]}]`)
into its own command handlers, so both timers accept the same files. Numbers are in the Liberty units.

Nothing is dropped silently. A command or option that is not modelled, a Tcl error (also the file's own
`error`) and an object that names nothing are listed in `Sdc::unsupported` (`Report::unsupported`, the
`fast unsupported:` lines of the timing log). A failed command is skipped and the script goes on with the
next top-level command.

| Command | What the fast engine does with it |
|---|---|
| `create_clock -name -period [-waveform {r f}] [-add] [ports or pins]` | A clock on ports or on pins of cells; without a source, a virtual clock for IO delays. |
| `create_generated_clock -source [-master_clock] [-divide_by] [-multiply_by] [-edges] [-invert] target` | A clock of its own period on the target pin or port; the master is the clock on the source (or the only clock). On an output port: a forwarded clock, the port is not a data endpoint. |
| `set_clock_groups -asynchronous / -logically_exclusive / -physically_exclusive -group ..` | No check between clocks of different groups (one group: against every other clock). |
| `set_false_path -from / -to` clocks | No check from the launch clock to the capture clock. |
| `set_false_path -from` ports, pins, cells | The start points are removed (the nets stay wired and loaded). |
| `set_false_path -to` ports, pins, cells | The endpoints are not checked. |
| `set_false_path -through` nets, pins, cells | No path through those pins. |
| `set_false_path -from` points `-to` points | An endpoint exclusion when nothing else reaches those endpoints, a cut when the start points reach nothing else, otherwise a launch group for those ports. |
| `set_max_delay D [-from ..] [-to ..] [-ignore_clock_latency]` | The paths may take `D` from their launch edge, in place of the time to the capture edge; the setup time or the output delay and the uncertainty still come off, an input delay is part of the arrival, hold is unchanged. Between clocks it is their relation; to ports, pins or cells it is the relation of those endpoints; from ports it is the relation of a launch group of those ports. An output port without an output delay becomes an endpoint of such a path. A false path or clock groups win over it; of two delays the smaller. A block with no clock at all is reported under a virtual clock named `set_max_delay`. |
| `set_disable_timing` ports, pins | No path through them. |
| `set_input_delay / set_output_delay [-clock] [-min] [-max] [-add_delay]` | Late and early launch of an input; the requirement of an output (`-max`). |
| `set_input_transition`, `set_driving_cell -lib_cell -pin [-input_transition_rise/fall]` | The slew of an input, or the driving cell: its delay at the load of the port's net less its delay without load, and its output slew. |
| `set_load` on output ports | The load of the port. |
| `set_clock_transition`, `set_clock_uncertainty [-setup]` | The slew at clock pins; the setup uncertainty. |
| `set_max_fanout N [current_design]` | The fanout limit, counted with the electrical violations. |
| `get_ports`, `get_pins`, `get_nets`, `get_cells`, `get_clocks` (`-quiet`, `-regexp`, `-nocase`, `-hierarchical`), `all_inputs [-no_clocks]`, `all_outputs`, `all_clocks`, `current_design` | Object queries; an object in a Tcl value compares by its name. |
| `current_design`, `set_ideal_network`, `set_ideal_latency`, `set_ideal_transition`, `set_propagated_clock`, `set_wire_load_*`, `set_units`, `set_operating_conditions`, `group_path`, `set_dont_touch`, `set_dont_use` | Accepted without effect (the clocks are ideal; the `ideal` commands have no effect in OpenSTA either). |
| `set_multicycle_path`, `set_min_delay`, `set_max_delay` with `-through`, without `-from` and `-to`, or from a register to some of its endpoints, `set_case_analysis`, `set_clock_latency`, `set_timing_derate`, `set_max_transition`, `set_max_capacitance`, edge-specific options (`-rise_from`, `-clock_fall`, ..), `-through` together with `-from`/`-to`, a false path from a register to some of its endpoints, `set_load` on a net, hold at output ports | Listed as unsupported. |

Names. A port is a bit (`d[3]`); a bus name or a pattern selects its bits. A pin is `instance/PIN`, with the
flattened instance name of the graph (`u_core.u_fifo.count[2]`). A file written against a Yosys netlist names
a register by its fine-grain cell (`path.reg[3]$_DFF_PP0_/D`) and a register net by the register (`path.q`):
both resolve to the flip-flop instance `path.reg[3]` (`path.q`) of the mapped graph. All 76 `constraint.sdc`
files of the HighTide designs (asap7, gt2n, nangate45, sky130hd) read without an unsupported item
(`sdc_survey`, each file alone).

## Tools

```
bazel build //pass/timing:cell_types_probe //pass/timing:fast_timing_probe
cell_types_probe ss=ss.lib tt=tt.lib ff=ff.lib                         # a .gz is read through zcat; a corner may list several files
fast_timing_probe --lg lg_dir --top top --corner tt=tt.lib --clock-port clk --period-ps 10000 \
                  [--corner ss=ss.lib ...] [--no-input-launch] [--opentimer-json timing.json]
fast_timing_probe --lg lg_dir --top top --corner tt=tt.lib --sdc design.sdc    # the constraints of an SDC file
bazel run //pass/timing:sdc_survey -- a.sdc b.sdc ...   # what the reader holds and could not honor, per file
```

`lg_dir` is `lhd synth ... --emit-dir lg:lg_dir` with `--set synth.place=false`; `timing.json` comes from
`lhd pass opentimer lg:lg_dir tt.lib design.sdc --top top --workdir W`.

## Tests

`bazel test --config=all //pass/timing/...`: `fast_timing_test` (the tables against the model, arc kinds and
senses, a missing cell or arc in a corner; propagation against hand-computed values in a reference model,
two corners, setup, hold, worst path, criticality and its window, rebasing, false paths, loops, electrical
limits, wire model, depth margin, ideal clock through buffers, the Elmore solver; the Tcl interpreter, the
SDC reader on the forms of the benchmark files, two clocks of different periods, false paths and asynchronous
groups between clocks, generated and forwarded clocks, a clock on the pin of a cell, false paths through a net
and from a port to a pin, maximum delays (a block without a clock, port to port, between clocks, to a pin),
driving cells, the fanout limit, two launch groups under what-if edits),
`bind_graph_test` (wiring resolution on a small graph) and `physical_rank_test` (wire delay in the ranking, rebind on a
structural change or a cell swap, cuts). End to end: `//lhd/tests:lhd_fast_timing_sdc_smoke` runs a two-clock design
constrained like the benchmark files through `lhd synth` with placement on both timers (`synth.timing.engine=compare`,
then `fast`), and `//pass/opentimer:pass_opentimer_test` reads the same forms into OpenTimer.
