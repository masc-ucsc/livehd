# Native route-first router exploration

These were options raised in the first router review. The full-DEF connectivity check, split-row handling, rectangular blockages, and localized rerouting now have implementations and regression coverage. The remaining technology and placement integration work is tracked in [3r-routefirst](todo/livehd/3r-routefirst.html).

## Verify connectivity and geometry independently

The track checker measures spacing on emitted metal. It currently omits M1 pin enclosure, some upper-layer pin shapes and pairs of off-track rectangles. A count of zero also says nothing about an omitted wire. The separate conductor checker already reads the DEF and LEFs. Two possible next steps are to call its underlying library from verification, or to retain a separate checker executable and have a strict closure command combine both results. The library avoids an extra parse. The separate process gives an independent readback and preserves the fast experimental CLI. Start with a complete two-pin net, an omitted net, a dangling V1 and two foreign nets touching a terminal. Require the net and offending geometry in each diagnostic.

Keep each OpenROAD checker grid’s findings along with their intersection. A marker seen on one grid can be real. Crop each disputed location with the conductors and vias it touches and recheck away from the box boundary. Compare against a clean control with the same shapes. This makes the box-boundary hypothesis testable per marker.

## Represent physical constraints independently of the track search

The current row planner uses one interval at each Y and one width per cell. The review adds explicit diagnostics for split rows, DEF blockages and nets assigned nondefault rules. For larger designs, one option is to subtract placement exclusions and macro footprints from each affected row and model the remaining site intervals directly. Another is to consume the existing placement legalizer’s legal-site intervals and use them for every slide, chain push and initial site assignment. Test a macro spanning two rows, a gap between equal-Y subrows, and a clock sink whose nearest space lies across a tap. Verify the final footprint against the original rows and exclusions.

For routing, start by projecting supported rectangular layer blockages into occupancy and deriving widths and via enclosures from LEF and per-net rules. An alternative is to build each proposal with the existing physical-rule helpers and retain the interval representation for the common default-width case. Replay the same fixtures through both paths. Include a wide clock leaf beside a default-width data net and a blocked via landing.

## Make cell-move trials reversible and incremental

A failed slide can rip every incident net and queue the same unchanged cell again. Removing a cell’s obstruction scans tracks and cut buckets across the chip. One option is to index the shapes owned by each movable cell and journal changes to its sites, wires, vias and tile demand. Accept or undo a trial as a unit. A cheaper first experiment is to probe candidate sites against occupancy with the incident owners temporarily excluded, then rip and reroute only if a viable site exists. The latter has less bookkeeping but estimates interactions among the replacement branches.

Replay blocked-pin windows from the same routed checkpoint. Record incident nets ripped per accepted move, unsuccessful retries, search expansions, CPU through independent closure and peak memory. For a rejected move, compare the complete before/after geometry and tile demand. For an accepted flop move, measure clock skew, setup and hold against the same frozen-driver control.

## Compare wire and cell choices with fresh timing

The native router consumes a single pre-route slack file. The saved minimaxp clock list named 101 nets. Only the primary clock name matched the prelegal DEF. That DEF named the 100 buffer-driven nets with the `snapshot_net_` prefix. For the review replay we obtained their identities from the actual clock-buffer output connections. Persist a netlist fingerprint and stable endpoint identities with clock ownership and timing inputs. Then compare static slack ordering with a refresh after a batch of accepted moves, or update only timing cones touched by changed branches. Use the same PCRS frame and independently connected, legal outputs. Report clock skew, worst setup and hold slack, CPU and the number of endpoints actually covered before choosing a refresh policy.

## Retain a crossing net as independently routed branches

The ASAP7 NVDLA audit counted every native routed tree whose centerline spans a grid seam. Four homes placed only 1.67–4.62% of the measured nets on seams, yet those nets accounted for 13.38–40.90% of routed wire. At 32×32 the wire share exceeded 82% on every control. O and C used partial native routes.

A two-worker experiment selected eight ordinary, two-pin nets with one long horizontal crossing on each control. Each worker received its local endpoint and a virtual terminal at the shared track. Assembly removed the virtual terminals, merged collinear metal and duplicate vias, restored rejected candidates and checked the original chip. Retaining a terminal estimated from endpoint positions alone lost several crossings. Reassigning failed terminals using nearby occupied tracks recovered some of them. The full experiment and its individual net lists are linked from the route-first plan.

The next comparison should use multi-pin data nets and clocks crossing multiple homes. One option is a coordinator-owned tree of boundary terminals, with each worker returning a connected subtree. Another is retaining a whole-chip trunk and giving workers only its local leaves. For either one, record the upstream driver, terminal layer and metal extent, home ownership, pin-to-trunk connectivity, and the geometry version used by every neighboring worker. Accept a replacement only after the original chip passes connectivity and physical checks. Compare hold, setup, extracted wirelength and total wall time on the same placement.

## Feed SRAM track alignment back to macro placement

In NVDLA P, six failed pin-access reports identified the same local position on several FakeRAM masters. Their M3 data pin lay on the lower macro edge, 216 DBU from its left boundary. Moving the SRAM footprints by tens of DBU produced valid access with the existing LEFs. Rebuilding native power geometry and reopening the 606 incident nets repaired all six named signals without restarting placement. The candidate still had other open nets.

A macro worker can propose nearby legal poses from actual LEF pin geometry, track offsets and placement clearance, then rank them by accessible pins across the whole macro. Recompute the power grid and verify incident signal routes before accepting a pose. The failed-pin list alone can overfit one edge at the expense of another.

## Use pin ownership to choose cell-level repairs

The O checkpoint before the in-row mirror trial has 272 open signals. Standard-cell pins account for 271 failed endpoints. Of the 179 source-blocked endpoints, 114 belong to AO222x2. The final C checkpoint has 802 open signals. Of its 530 source-blocked endpoints, 203 belong to AOI22x1. SRAM alignment addresses a different part of the residual.

An O trial selected 12 blocked AO222x2 cells and reopened their 79 incident nets, including 20 opens. Mirroring the 12 footprints horizontally on their legal rows left 270 total opens. The same repair with the original poses left 268, and both connected eight of the targeted opens. Keep the original poses for this cohort. One further option is to try alternative legal sites for a blocked cell and reopen its incident nets. Another is to try permuting inputs only when the mapped cell's Liberty function proves those inputs logically interchangeable. An accepted permutation would need a corresponding mapped netlist edit, updated pin-level timing and equivalence verification. For both choices, count newly reachable pins and lost neighbors, then check the assembled geometry, connectivity, setup and hold. The local concentration on two masters gives a bounded test cohort.
