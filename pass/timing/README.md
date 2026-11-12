# Fast timing engine (`pass/timing`)

A small native timing engine for the loops of synthesis and placement, in single precision, that times
several Liberty corners in one walk. Plan: [`todo/livehd/2q-fasttimer.html`](../../todo/livehd/2q-fasttimer.html).
OpenTimer stays for the report and for validation. Namespace `livehd::timing::fast`.

This directory has steps A, B (numeric binding and graph binder; the SDC reader is not done) and C (zero
wire). Not here yet: wire trees (D), the `Feedback`/`Whatif` contracts (E to G), synthesis (H), calibration
(I), hold repair (J), chunking (L).

## Layers

| File | What it is |
|---|---|
| `cell_types.{hpp,cpp}` | The cells of N Liberty corners as one table of types. Pins, arcs (delay, launch, setup, hold), pin capacitance and electrical limits per corner, NLDM tables pooled in `float` with shared axes. `lookup` is the interpolation of `liberty::timing::Table::lookup`. |
| `netlist.{hpp,cpp}` | A mapped netlist as numbers: cells with a type, pins as consecutive indices, nets with one driver and a list of sinks. Ports are cells of two built-in types. `finalize()` levelizes the pins; a combinational loop is cut at one pin and the cut is listed. |
| `engine.{hpp,cpp}` | Late arrival and slew forward, required time backward, for every corner; early arrival on the hold corner; endpoint slack, per clock WNS/TNS and achievable period; criticality; electrical limits; the worst path. |
| `bind_graph.{hpp,cpp}` | A flat mapped hhds graph to a `Netlist`: Liberty Sub instances, the bits of the ports, and the wiring nodes (`get_mask`, `set_mask`, `concat`, constant shifts) resolved bit by bit down to the driving pin. |
| `cell_types_probe.cpp`, `fast_timing_probe.cpp` | Manual tools, below. |

## What it times

Combinational delay arcs; rising-edge flip-flops with ideal clocks (clock to output, setup and hold); primary
inputs (delay, transition) and outputs (delay, load). Cell delay and output slew are looked up at the input
slew and the **total** load of the net (pin capacitances, plus a wire model when one is given); rise and fall
are kept apart; the worst arrival and the worst slew win at a merge. Hold is the fastest corner with minimum
arrivals. The clock is ideal: the network from a clock port to the clock pins of flip-flops is not timed.

Everything else is listed in `Report::unsupported`, never dropped silently: latches, memories and clock
gates; falling-edge elements; arcs the Liberty model could not read; sinks driven by a constant; pins behind
a combinational loop (cut, so approximate); flip-flops whose clock does not come from a clock port.

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
`Rct`. `sdc.hpp`: one typed reader for the SDC the flows use (`set`, `$var`, `[expr]`, `[get_ports]`, `[all_inputs]`,
`lsearch`, `create_clock`, IO delays, `set_load`, `set_input_transition`, port false paths); numbers are in the
Liberty units; every command or form it does not model is listed in `Sdc::unsupported`. `physical_rank.hpp`: the engine
behind the placement ranking callback, selectable with `--set synth.timing.engine=opentimer|fast|compare` (compare runs
both and logs the agreement per call in the placement's `pass.opentimer.incremental` log). `--set synth.timing.engine=fast`
also runs the detailed evaluations and the what-if edits of repair on the engine (`Physical_ranker::whatif()`: resize,
add_cell, set_net with rollback, settled incrementally; OpenTimer stays for the final report).

## Tools

```
bazel build //pass/timing:cell_types_probe //pass/timing:fast_timing_probe
cell_types_probe ss=ss.lib tt=tt.lib ff=ff.lib                         # a .gz is read through zcat; a corner may list several files
fast_timing_probe --lg lg_dir --top top --corner tt=tt.lib --clock-port clk --period-ps 10000 \
                  [--corner ss=ss.lib ...] [--no-input-launch] [--opentimer-json timing.json]
```

`lg_dir` is `lhd synth ... --emit-dir lg:lg_dir` with `--set synth.place=false`; `timing.json` comes from
`lhd pass opentimer lg:lg_dir tt.lib design.sdc --top top --workdir W`.

## Tests

`bazel test --config=all //pass/timing/...`: `fast_timing_test` (the tables against the model, arc kinds and
senses, a missing cell or arc in a corner; propagation against hand-computed values in a reference model,
two corners, setup, hold, worst path, criticality and its window, rebasing, false paths, loops, electrical
limits, wire model, depth margin, ideal clock through buffers, the Elmore solver, the SDC reader),
`bind_graph_test` (wiring resolution on a small graph) and `physical_rank_test` (wire delay in the ranking, rebind on a
structural change or a cell swap, cuts).
