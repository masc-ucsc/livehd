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
