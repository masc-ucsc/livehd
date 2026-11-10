# pass.pcrs — power, clock, reset and scan-chain networks

Only the reset network exists so far. The placer calls it (`synth.place.pcrs`, default on); `pass/pcrs/pcrs.hpp` is the API.

## Reset

A reset network is one reset signal and everything it reaches through identity buffers and inverters. Its roots are the
drivers carrying the synthesis reset role and the drivers of asynchronous clear/preset pins; each root is walked up to the
top of its chain (graph input, design flop, any other driver). The pass **rebuilds the distribution from its sinks every
call**: the previous buffers, inverters and pass flops are stripped, the loads are grouped, and a new tree is minted.

* **One balanced tree per (reset root, clock domain).** A sink's domain is the root clock of the flop it is (an async reset
  pin) or of the flops its reset logic feeds (the D pins within four cells, majority). The root clock is found through
  integrated clock gates and identity buffers, so a gated flop uses the ungated clock and the reset still arrives while
  the clock is stopped. Reset crossing domains is a clock-domain-crossing problem and is not synchronised here.
* **N flop levels, same N for every sink of a domain.** The domain's reset is delayed exactly N cycles everywhere, so the
  design behaves as before after a longer reset. N is a timing choice (0 is a plain buffer tree). Both polarities of a
  domain share N; an inverter in front of the tree (or the parity of an inverting-Q flop family) keeps each sink's
  polarity. Asynchronous reset pins are served the same way: the reset is registered to the clock edge.
* **Fanout.** No driver of the tree carries more than `synth.place.reset_fanout` loads (default 32). Level sizes follow
  from the sink count: `ceil(sinks/f)` leaf flops, `ceil(that/f)` above, and a buffer tree in front of level 1 if needed.
  Sinks cluster by placement when positions are known (recursive bisection), else by instance name (source hierarchy):
  the placer passes the positions of the placement a call starts from, and the first such call rebuilds every tree
  once (`Options::regroup`) so a tree made before placement does not keep its name-order groups. The root serves
  every clock domain of its network; when the domains together put more than the cap on it (many small gated-clock
  domains), its loads go behind one buffer tree. A graph output behind a buffer returns to the root undelayed.
* **Cells.** The smallest plain posedge flop, identity buffer and inverter of the Liberty that the LEF can place. Names
  are `<hierarchy>.__pcrs_rst{f<level>|b<level>|i}<n>`. Flops carry the node attribute `pcrs_stage` (low 16 bits the level,
  bit 16 = inverted Q).

### Depth

`Depth_state` holds the wanted depth per domain (`root@clock`), shared by every placement call of a flow
(`Options::pcrs_depth`). The default policy keeps the depth already in the graph and starts a new domain flat when one
driver can serve it, with one flop level otherwise. A network whose domains all keep their depth is not touched, so its
cells keep their names and placement.

* `retarget_for_wns`: when a cell of a domain is on the worst setup path, add one level to that domain (one domain per
  call). TNS is never consulted: the clock goal may be unreachable.
* `retarget_shrink`: at the last timing round, take one level from a domain whose flop D/Q pins have slack beyond their
  longest hop arrival (merging two hops costs about one more hop).

`timing_place` applies both before each warm round; the round's placement then rebuilds from the new depth.

### Placement and timing

With pcrs on (`Prects_library::reset_is_data`) a reset is ordinary data: it is placed, timed and repaired like any net,
and only clock networks stay excluded. Sizing and buffering do not need to touch the reset because pcrs picks the flop
sizes. `qor.place.pcrs` reports `networks`, `sinks`, `flops_added`/`buffers_added`/`removed` (this call), `flops` (in the
placed netlist), `max_depth` and per-domain `depth`/`sinks`/`flops`/`buffers`/`kept`. Post-placement simulation must hold
reset for at least `max_depth + 1` cycles.

### Formal

`bypass_stages()` replaces every tagged flop by a wire (a `Not` for an inverting Q) when `lhd lec` loads an `lg:` side, so
LEC compares the design as if the tree had no stages whatever depth timing chose. The `lg:` directory on disk is not
changed. A placed netlist written as Verilog loses the tag and is not bypassed.

### Not handled (yet)

Domino storage reset pins (`synth.place.reset_pins`) keep the legacy tree. Loads that are not placeable library cells
(graph outputs, macros) keep their direct connection to the root. Depth changes happen between timing rounds, not inside a
GP checkpoint. Clock, power and scan networks.
