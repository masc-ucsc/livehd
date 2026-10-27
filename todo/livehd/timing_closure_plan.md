# LiveHD Timing Closure Plan

Oct 24, 2026 · @Jose Renau

**Paused by request on 2026-10-25.** The current implementation status,
remaining work and interrupted debug-validation restart checklist are in
[2t-timeclosure](2t-timeclosure.html#paused-status). This document describes
the intended design, not a claim that every phase is implemented or accepted.
Synthesis/resynthesis remains future work; hold repair remains post-route.

## Goal and constraints

A single-threaded, non-ML timing loop runs inside LiveHD global placement (DREAMPlace fork). At each overflow checkpoint it re-estimates timing, re-buffers nets and re-sizes cells, with no external timer in the loop.

- **No ML, no multithreading.** Speed comes from incremental work and a cheap timer, not parallelism.
- **Buffering is modeled early** for blockage-crossing nets, mostly with fanout below 4. Their buffered delay and area are modeled from phase 3 on, before any buffer is inserted.
- **Netlist changes come last.** Real buffers and sizing happen only in phase 7, after placement is stable; resynthesis is optional and never relied on.
- **Hold is out of scope for placement.** It is fixed best effort after routing, as a multi-corner ECO.

## Objective: minimum achievable period

The loop minimizes the achievable clock period, which is driven by WNS, and treats the SDC target as unreachable. Slack is measured against the period the design can reach now, not against the requested period (for example 1 ns on a 6 ns design).

- **Rebase slack every checkpoint.** Set T to the current critical-path delay from the proxy. Optimize toward T\_target = (1 − ε)·T with ε around 10%, so only paths within 10% of WNS are negative and get optimized.
- **Why not the SDC period.** Against 1 ns nearly every path is negative. The LR multipliers then grow on all of them, which behaves like TNS minimization: sizing and buffering effort spreads over paths that will never set the period, and area and power go up for nothing.
- **Criticality, not slack.** Each arc gets criticality = 1 − slack\_rel / T, sharpened by an exponent (as in VPR), so effort concentrates on the near-critical window.
- **Balance, not just the worst path.** Optimizing only the single worst path makes the next path the critical one on every iteration. Treat all paths within ε of WNS together. A soft maximum over endpoint arrival times, log-sum-exp with temperature γ, spans WNS-only (small γ) to TNS-like (large γ); γ is the knob. On the placer side, the 1/k² path-rank decay in the pin weights plays the same role.
- **Recover off the critical window.** Paths with positive rebased slack are candidates for downsizing, smaller or no buffers, and high-Vt cells, as long as they stay outside the window.
- **What to report.** Final WNS, i.e. achieved period or Fmax, is the acceptance metric. The window's delay spread and area/leakage are secondary diagnostics. TNS against the SDC period is not a useful metric here.

**WNS guard.** The window objective can lower the window's total delay while making the worst path slower. After each checkpoint, compare WNS with the best seen so far. Keep a snapshot of the netlist changes (buffers, sizes, LR multipliers) at the best WNS. If a checkpoint worsens WNS by more than a small tolerance, revert to that snapshot and shrink γ so the soft maximum leans toward the worst path.

```latex
D_{\gamma} = \gamma \log \sum_{e \in \mathrm{endpoints}} \exp\!\left(\frac{a_e - \mathrm{req}_e}{\gamma}\right)
```

## Timing proxy on LiveHD traversal

The proxy is a late-mode levelized STA on the LGraph that only needs to be equally wrong for every kind of path. Slack is rebased each checkpoint, so a uniform error is harmless; what must be controlled is error that differs between path styles. OpenTimer is an offline regression test, not part of the loop, and no SPEF is needed during placement.

**Graph and traversal**

- Keep the netlist as an LGraph. Map each DREAMPlace node index to its LGraph node id once, at load.
- Arrival and slew: iterate `forward()`, which visits a node only after its drivers, alternating net levels and cell levels. Flops and memories cut the cycles, so a flop's Q pin starts a path and its D pin ends one.
- Required time: iterate `backward()` over the same graph.
- Each pass is linear in nodes plus edges and needs no separate levelization step.
- Incremental mode: mark nets whose bounding box moved more than a threshold as dirty, and propagate dirty flags along the traversal. Clean nodes keep their cached values.

**Wire model: continuous between checkpoints, Steiner-corrected at them**

The FLUTE Steiner topology changes discontinuously as pins move, so timing computed on it jumps between iterations. That noise would make the WNS guard revert good steps and the λ multipliers oscillate. Fu et al. (Xplace, ICCAD'24) avoid it with a degenerated RC tree, which this plan adopts.

- **Unbuffered nets:** a driver-to-sink star (rectilinear shortest-path tree). Each sink connects straight to the driver, so load and delay are continuous functions of pin positions and need no tree construction.
- **Buffered nets:** timed on their frozen virtual-buffer tree, whose topology changes only at checkpoints, with buffer positions following the edge endpoints.
- **Checkpoint correction:** at each checkpoint, build the FLUTE tree once per net and cache two corrections. The first is a per-net load ratio r = Steiner length / star length. The second is a per-sink delay compensation for the neighbor branches that share the sink's trunk. Between checkpoints the star model times the net, scaled by the cached corrections.
- **Corrections are per net, not per arc,** so they survive sizing. Recompute them only for nets that were re-buffered or moved more than a threshold.
- **Schedule:** the first correction is taken at the first timing checkpoint (phase 3), then refreshed every 0.1 overflow drop. Fu et al. report about 6% slack error uncalibrated, zero right after each refresh, and under 1% between refreshes.

```latex
\mathrm{Load}_{\mathrm{cal}}(r) = \frac{\mathrm{StWL}_e}{\mathrm{RCWL}_e}\,\mathrm{Load}(r), \qquad \mathrm{Delay}_{\mathrm{cal}}(v) = \mathrm{Delay}(v) + \mathrm{comp}(v)
```

**Gate and transitions**

- Gate: NLDM table lookup on input slew and effective capacitance.
- Late mode only, rise and fall carried separately and propagated by arc unateness: two values per pin. Without this, inverting and non-inverting paths are not comparable. Early mode is not computed during placement.
- During the forward pass, record the worst fan-in of each pin, so worst-path extraction for the near-critical window costs almost nothing.

**Path-style consistency**

- **Net styles:** local, long unblocked, gap-crossing, over-macro, high-fanout. Each has one wire-delay factor f\_style, normalized so local nets are 1.0. Only the ratios matter.
- **Fit once per technology and routing setup,** not per run, from prior routed runs of unrelated designs or the TimingPredict sky130 set. Compare the Steiner-corrected proxy against routed-SPEF wire delay per style. The checkpoint correction removes star-versus-Steiner error; f\_style covers Steiner-versus-routed error.
- **Wire-heavy versus cell-heavy paths.** The style factors correct the wire side; the cell side is exact given its inputs.
- **Virtual versus real buffers.** Fit the linear buffered-delay model's per-unit delay against your own van Ginneken results using the same proxy, so the near-critical window does not jump at materialization.

**Absolute limits**

Slew and max-cap are absolute constraints, so they are checked with a pessimistic wire capacitance, the upper end per style, kept separate from the timing ratios. Otherwise long nets are under-buffered and fail after routing.

**OpenTimer as a regression test**

When the proxy code changes, feed identical RC to the proxy and OpenTimer and compare the ranking of endpoints within 10% of WNS. This checks the effective-capacitance and slew approximations. It does not run during placement.

**Proxy fidelity check**

- On the TimingPredict dataset (21 sky130 OpenROAD designs with DEF, SPEF and SDF), compare proxy arrival times on the placed DEF with OpenSTA on the routed SPEF.
- Report per path style: Kendall τ of endpoints within 10% of WNS, overlap of the predicted and true near-critical sets, and bias ratio relative to local nets. A good global correlation can hide one style that is consistently off.
- Report WNS error last; R² over all endpoints is dominated by non-critical paths.

## Buffering

Each checkpoint sorts nets into four classes and buffers each class with its own algorithm. Blockage-crossing nets with fanout of 3 or less, the common case here, get Chen–Pedram–Buch simultaneous routing and buffering.

| Net class | Algorithm | Reason |
| --- | --- | --- |
| Unblocked, shorter than the max unbuffered length L | None | No buffer helps |
| Unblocked, long | Early: linear optimally-buffered delay (Otten–Brayton). Late: van Ginneken with multiple buffer types (Lillis; Li–Shi) | Buffers can go anywhere along the Steiner tree |
| Crosses a blockage, fanout ≤ 3 | Chen–Pedram–Buch DP on a Hanan graph extended with blockage corners | At most 2³ = 8 sink subsets, so the near-exact DP is cheap |
| Crosses a blockage, fanout ≥ 4 | BEN: reroute the Steiner tree on a tile graph, then resource-aware van Ginneken | The DP's cost grows as 2ⁿ in fanout |

**Changes to Chen–Pedram–Buch for this use**

- Drop the original speed heuristics: the keep-2-points pruning and the early exit once a sink set reaches the source. With fanout ≤ 3 the full dominance curve is affordable.
- Replace continuous fixed-gain sizing with discrete library buffers timed by NLDM in the late stage. Keep fixed gain only for the early-stage estimate.
- Replace binary buffer admissibility with a BEN-style cost per Hanan point taken from local density, infinite inside hard blockages. Prune on (cap, required time, cost).
- Set the edge-split length L from the slew limit, not only from delay.
- Cache each net's Hanan graph. Blockage corners never move, so only pin rows and columns change between checkpoints.

**Choosing the solution at the driver.** For a net on the near-critical window, take the maximum required-time solution. For every other net, take the cheapest solution (buffer area, then wirelength) that keeps it outside the window. Both CPB and BEN already produce the full (cap, required time, cost) curve, so this is a selection rule, not a new algorithm.

**Whitespace competition across nets**

- Accumulate virtual buffer demand per tile. Raise each tile's cost from that demand and a history term, as PathFinder does for routing.
- Re-run the worst nets with the updated costs. This counters the buffer pile-up in narrow channels that per-net algorithms cause.

**Virtual buffers and removal**

- **Phases 3–6: estimates only.** Buffering exists only in the delay model and the density map. Long nets use the linear buffered delay; blockage- and gap-crossing nets use the DP's buffered estimate and reserve their buffer area in the gaps. No netlist change and no committed positions. This matters for resynthesis: unbuffered long nets would look far worse than they will be and misdirect it.
- **Phase 7: real buffers in the force field.** Activate buffers from a pre-allocated pool of masked DREAMPlace nodes rather than rebuilding placedb, and split the nets in the LGraph. Seed each buffer at its DP-chosen position and give gap-crossing buffers their gap pseudo-net. Wirelength, density and pin weights then move them, unlegalized.
- **No overflow spike.** The area was already reserved in phases 3–6, so insertion does not jump overflow. Insert in batches, most critical nets first.
- **Removal.** At each phase-7 checkpoint, dirty nets are ripped up and re-buffered from scratch; buffers no longer needed are masked out.
- **Drift check.** Wirelength pulls a buffer onto the straight line over a macro while density pushes it to the nearest edge, and neither knows the slew limit or the chosen gap. At each checkpoint, re-check slew and gap membership and re-buffer nets whose buffers drifted.
- Keep the old solution unless the new one improves the net's slack by more than a margin ε, so nets do not flip between solutions on every checkpoint.

**Criticality decides who gets whitespace**

- **Critical-sink isolation first.** For a high-fanout net in the near-critical window, keep the critical sink on the driver and move all non-critical sinks behind one new decoupling buffer. This cuts the driver's load, which is what matters, and often fixes the net without a full BEN or van Ginneken pass.
- **Slot pricing by criticality.** Buffers on near-critical paths pay the base slot price, and RSMT-RUDY inflation is capped around them. All other buffers, including decoupling buffers, pay the base price plus a congestion price from RUDY, so they land where routing and space are cheap. Their only hard constraints are slew and cap limits and staying outside the window.
- **Promotion.** At each checkpoint, if a non-critical buffer's path enters the window, for example because congestion pushed it far, it is promoted to critical pricing and re-placed.

## Halos, narrow gaps and placer guidance

Most blockages here are macro halos and narrow channels, so the scarce resource is buffer slots in gaps, not area that wires cannot cross. Buffering prices those slots, and the placer is steered to keep them free for the nets that need them.

**Floorplan model, built once per run**

- Grow each macro by its halo. The grown rectangle forbids buffers, but wires may cross it. L- or T-shaped macros are split into rectangles first.
- Extract each gap between grown macros. Its slot count is usable area divided by the buffer footprint, minus a legalization margin. A gap narrower than one buffer site has zero slots and is wire-only.
- Hanan points come from the grown corners, plus points along each gap's centerline spaced at the max unbuffered length L. Points in zero-slot gaps are dropped.
- Each gap g has a slot price that rises with demand and keeps a history term, as PathFinder does:

```latex
p_g = p_0 \left(1 + \frac{\mathrm{demand}_g}{\mathrm{slots}_g}\right)^{k} + h_g
```

**Buffering through gaps and over macros**

- Chen–Pedram–Buch and BEN use p\_g as the per-buffer cost and prune on (cap, required time, cost, slew).
- Slew is part of the DP state. A candidate that exceeds the slew limit at a sink or at the next buffer input is infeasible, so an over-macro crossing longer than L is rejected rather than merely penalized. Slew and cap checks use the pessimistic wire capacitance from the timing section.
- Segments over a grown macro use upper-layer RC. Each macro has a crossing capacity, and over-macro edges pay a congestion price from the same negotiation loop, so critical nets do not all pile over one macro.
- In each negotiation round, re-buffer nets in the near-critical window first so they claim gap slots at base price. Other nets then take the cheapest solution at current prices, which usually avoids contested gaps.

**Guiding the placer**

- **Reserve slots in the density map.** Add the area of virtual buffers that window nets and slew-forced nets place in a gap to the DREAMPlace density bins covering that gap. Standard cells are then pushed out of exactly the slots buffers need. Reserve nothing for nets that will probably stay unbuffered, or the placer over-spreads.
- **Lower gap target density only where there is demand.** Set each gap bin's target density from its reserved buffer area instead of the global target, so uncontested gaps still pack normally.
- **Pull critical endpoints toward the chosen gap.** For a window net whose DP solution goes through a gap, add a pseudo-net from its driver and sinks to the gap entry point, weighted by λ. Endpoints then settle near the route the buffers use, which shortens detours. Drop the pseudo-net when the net leaves the window. For window nets, the gap pseudo-net takes priority over the driver-to-sink term, which would otherwise pull the driver toward the sinks' center rather than toward the gap.
- **Rebuild targets at each checkpoint.** Gap reservations, target densities and pseudo-nets are recomputed from the latest buffering, so a gap freed by one net becomes available to others.

**After materialization**

- Recheck slot occupancy per gap. Any buffer that legalization moved out of its gap, or by more than a set displacement, marks its net dirty.
- Re-buffer those nets with that gap's slot count reduced, then rerun the proxy.

## Sizing

Sizing is single-threaded Lagrangian relaxation (LR). One set of multipliers per timing arc drives sizing, net weights and buffering together.

- **Multipliers.** Keep λ on each late arc, rise and fall separately; there are no early-arc multipliers. Use the min-max-delay LR form of Chen, Chu and Wong (ICCAD'98): endpoint multipliers sum to 1 and follow rebased slack, so they concentrate on the near-critical window instead of growing everywhere. Project for flow conservation at each node as in Flach et al. (TCAD'14).
- **Sizing step.** For each cell, in `forward()` order, pick the library cell that minimizes the λ-weighted delay of its arcs plus its area or leakage cost. Off the critical window the λ terms are near zero, so the area term dominates and cells downsize. Use Sharma and Chu's update schedule (ICCAD'17) to converge in fewer LR iterations.
- **Pin weights for DREAMPlace.** The λ on each driver-to-sink arc becomes that sink's pin weight in the placer (next section), replacing DREAMPlace 4.0's per-net momentum weighting. Mangiras et al. (TCAD) use the same multipliers as relocation forces.
- **Buffering link.** A sink's required time in the buffering DP is taken from the LR-adjusted slack, so buffering and sizing do not work against each other.
- **Scope.** Sizing runs only in phase 7, alongside buffering; Vt swap after legalization. Sizes chosen by resynthesis are starting points, not fixed.

## Placer weighting

The placer gets weights per driver-to-sink connection, not per net, through an added driver-to-sink wirelength term. The weights combine the LR multipliers with worst-path information and are smoothed per pin.

**Driver-to-sink wirelength term**

- Add RCWL, the weighted sum of driver-to-sink distances, to DREAMPlace's wirelength objective with a global weight t (Fu et al. use 0.25). It is smoothed the same way as the existing weighted-average wirelength, one driver-sink pair at a time.
- A critical sink pulls only itself and the driver, not the whole net. The term also centers the driver among its sinks, which lowers sink delays for the same Steiner length.
- It matches the star wire model above, so the placer optimizes the same quantity the proxy times.

```latex
\min_p \sum_{e} \left( \mathrm{WL}_e(p) + t \sum_{v \in S_e} \omega_v \, \mathrm{WL}_{r_e v}(p) \right) + \lambda_D D(p)
```

**Hybrid pin weight**

- **Graph term:** the LR multiplier on the sink's driver-to-sink arc, normalized to \[0, 1\]. Zero outside the near-critical window.
- **Path term:** for every endpoint inside the window, extract its worst path from the recorded worst fan-ins. Path k in rank order adds delay/k² to the pin each arc depends on: the sink for a net arc (load, position), the input pin for a cell arc (slew). The 1/k² rank decay keeps WNS dominant while still covering the window. Fu et al. use a window of 3/4 of WNS, which corresponds to 25%; this plan uses 10%.
- ω = α · graph + path, with α ≈ 0.5 as a starting point.
- The same influencing-pin rule picks the fix at a checkpoint: a sink-side pin points to moving or re-buffering, an input pin points to upsizing its driver.

**Per-pin weight smoothing**

Each pin's weight is blended with its previous value using its own factor η. η grows with the size of a rising change, so weights react fast when a pin becomes critical and stay stable otherwise. This replaces uniform momentum and damps oscillation in the window. Starting values from Fu et al.: η₀ = 0.4, a = 5, b = 2.

```latex
\eta_i = \eta_0 \, a^{\max(\Delta\tilde\omega_i, 0)^{b}}, \qquad \omega_i^{(m+1)} = \eta_i \tilde\omega_i^{(m+1)} + (1-\eta_i)\, \omega_i^{(m)}
```

## Hold

Hold is not handled during placement. It is fixed best effort after routing, as an ECO over all corners, because it needs real clock skew and every corner, which placement does not have.

**Post-route ECO, in order of cost**

1. Useful skew: solve setup and hold together with the LP of Wu, Wong et al. (DAC'14). DiffCCD (ICCAD'25) is the recent version that also sizes clock buffers.
2. Downsize or swap to high-Vt cells on hold-critical paths that keep setup slack in every corner.
3. Insert delay where path commonality is highest, so one buffer near a shared fanin fixes many endpoints (the HoldAdvisor idea). Insert only where setup slack, checked across corners, exceeds the added delay.
4. Use a wire detour on free metal instead of a buffer (DAC'17 free-metal-segment allocation), useful in dense regions near gaps.

Optional and cheap during placement: do not raise net weights on short flop-to-flop paths, so the placer does not make them shorter than needed.

## Schedule

Placement runs as seven latched phases inside a hard iteration budget. A gate controller advances to the next phase as soon as its metrics are stable, or at a latest-start deadline. Timing starts in phase 3 as a delay model and pin weights; the netlist changes only through resynthesis (phases 4–5, optional) and buffering and sizing (phase 7).

&#91;embedded content: per-checkpoint loop from phase 3 on · re-buffering and sizing act only in phase 7\]

Between checkpoints DREAMPlace runs unchanged. CPB is Chen–Pedram–Buch.

1. **Initial placement.** Wirelength, density, fillers and local spreading; no timing feedback. The proxy and RUDY only observe.
2. **Memory growth and RSMT-RUDY feedback.** Two independent gates, either may fire first. Memory growth (if enabled) ramps memories to their full footprint with coupled local-density decay. RUDY feedback enables congestion-driven spreading with bounded inflation. Density control stays on throughout.
3. **Timing-driven stabilization, no netlist change.** Requires the RUDY gate to have fired and memory growth to be complete, because the halo, gap and slot model needs final macro footprints. The proxy drives pin weights; long and blockage-crossing nets use buffered delay estimates with reserved area. Ends on stability, or at an overflow floor of about 0.3 when resynthesis is enabled, so cells can still move.
4. **Resynthesis decision (if enabled).** Subblocks with paths still in the 10% window are candidates. With an infeasible target, resynthesis pushes for maximum frequency.
5. **Resynthesis loop (if enabled).** Rounds are cumulative: a subblock is never reverted. After each round, a dwell of 20–50 iterations, then a phase-3 stabilization. Stop when stabilized WNS no longer beats the previous round by more than its noise σ, when resynthesis has no options, or when the budget cannot admit another round.
6. **Final stabilization.** Pin weights until WNS, TNS, HPWL and RUDY are stable. Without resynthesis, phases 3 and 6 merge into one stabilization with no overflow floor.
7. **Buffering, sizing and removal.** Real buffers and LR sizing in the unlegalized force field, inserted in batches. Ends on stability (WNS, HPWL, RUDY) or at the budget. Then legalization, Vt swap, OpenROAD routing, signoff WNS and the post-route multi-corner hold ECO.

Buffering and sizing never assume resynthesis ran: whatever it already sized or buffered is a starting point that phase 7 may change.

### Gate controller

Each gate checks whether its metrics have stopped moving. "Stopped" means the trend expected over the next H iterations is small compared with how far the metric has already moved since its history started, and with its noise. This replaces the moving-average 5% rule, which passes on a slope that is still descending and fails near zero.

**Sampling.** Every c GP iterations (start with c = 10), record from the proxy: WNS, TNS within the 10% window, the near-critical endpoint set, HPWL, and full-footprint RSMT-RUDY overflow. Also log SDC TNS, current-footprint RUDY, density overflow, filler charge, inflation and the density and local-density weights. Sampling never changes weights, cells or the netlist. Raw and virtual-buffer timing are never mixed in one history; clock and reset nets are excluded.

**Stability test for one scalar metric y**, run every N/2 samples on its history (start with N = 5, H = 50 iterations, θ = 0.03, k = 2):

1. Skip if the history has fewer than N samples.
2. R = max(y) − min(y) over the whole history: how far the metric has moved since the history started.
3. s = Theil–Sen slope of the last N samples (median of pairwise slopes, per iteration). Robust to single-sample WNS jumps.
4. σ = 1.4826 · median absolute deviation of the residuals around that slope: the noise level.
5. Turning-point check: fit the slope on the first and second halves of the window. If their signs differ and both exceed kσ/H, the window straddles a peak and fails.
6. Pass if |s| · H ≤ max(θR, kσ) and no turning point.
7. Stable if this evaluation and the previous one, at least N/2 samples earlier, both pass.

```latex
\text{pass} \iff |s|\,H \le \max(\theta R,\; k\sigma) \;\wedge\; \text{no turning point}
```

The near-critical endpoint set is stable when its Jaccard overlap with the set at the previous evaluation is at least 0.9. Range normalization makes the test unit-free, so no clock-period or endpoint-count floors are needed.

**Gate firing.** A gate fires when all its required metrics are stable, or when the iteration reaches the gate's latest start. On firing:

1. Latch the phase; never toggle it back when curves fluctuate.
2. Log the reason (stable or deadline), each metric's slope, range, noise and value, and the density state.
3. Start a dwell for the triggered feature: the memory ramp length plus settle time, 20–50 iterations after resynthesis, a configured interval for RUDY inflation, timing-weight activation and each phase-7 buffer batch.
4. During the dwell, keep sampling and logging, but evaluate nothing.
5. When the dwell ends, restart the histories of the affected metrics. Transient bumps then cannot inflate R and loosen the test.

| Gate | Required stable metrics | Extra preconditions | Action |
| --- | --- | --- | --- |
| Memory growth (if enabled) | WNS, TNS, HPWL | — | Start growth ramp |
| RSMT-RUDY feedback | WNS, TNS, HPWL | — | Enable congestion spreading |
| Timing (phase 3) | RUDY, HPWL | RUDY gate fired; memory growth complete | Enable proxy pin weights and virtual buffers |
| Resynthesis round | WNS, near-critical set, window TNS | Enabled; budget admits a round | Resynthesize candidate subblocks |
| Repair (phase 7) | WNS, TNS, HPWL, RUDY | Timing active plus a response interval; resynthesis done or not admitted | Start buffer and sizing batches |
| End of placement | WNS, HPWL, RUDY | Phase 7 active | Legalize |

**Budget.** A hard total B in GP iterations that depends on the enabled features: 800 by default, 1000 with resynthesis. Latest starts are fractions of B so they rescale with it; starting points are 0.25B for memory growth and RUDY, 0.45B for timing, and B − R for repair, with R = 0.25B reserved for phase 7. A resynthesis round is admitted only if the remaining budget covers the round, its dwell and stabilization (measured from the previous round), and R. Every phase exits early on stability, so with an infeasible target the run usually ends well before B.

## Risks and open questions

The largest risk is a path-style bias the factor table does not capture, which silently mis-ranks one class of paths.

- **Path-style table portability.** The factors are fit per technology and routing setup. A new metal stack, router settings or macro style can shift the ratios. Refit when any of those change, and check the per-style bias ratio on one routed design first.
- **Single-thread runtime.** Re-buffering touches only dirty nets, but a full `forward()` and `backward()` pass still visits every node. On large designs the passes, not the buffering, may dominate. Measure before tuning K.
- **Nets that wrap around blockages.** Chen–Pedram–Buch can route a net around a blockage. Its wirelength then exceeds the Steiner estimate the placer uses, so the HPWL objective and the timing model disagree for those nets.
- **Fanout boundary.** At fanout 4 the DP state is 2⁴ = 16 subsets and may still be affordable. Measure where BEN starts to win on your designs rather than fixing the cut at 3.
- **Virtual-to-real buffer mismatch.** If the linear buffered-delay model and real NLDM stages disagree, the near-critical window jumps at materialization and the WNS guard may revert good work. Compare the window before and after the first materialization.
- **Open:** which prior routed runs are representative enough to fit the gap-crossing and over-macro factors for this technology?

* **Window width ε and temperature γ.** Too narrow, and the critical path changes every checkpoint, so net weights oscillate. Too wide, and the loop drifts back toward TNS minimization. Start with ε around 10% of T and widen it if WNS stalls while the identity of the critical path keeps changing.

- **Pseudo-nets versus wirelength.** Gap pseudo-nets can pull endpoints away from their other connections. Cap their weight relative to the net's own λ weight, and check that HPWL on non-window nets does not grow after they are enabled.
- **Slot estimates in narrow gaps.** Area divided by footprint overstates capacity in gaps that are only one or two sites wide. Count discrete legal sites per row instead for those gaps.

* **Run-to-run noise.** DREAMPlace's GPU kernels are not deterministic, so WNS varies between identical runs. Measure that spread and set the WNS-guard tolerance above it, or the guard reverts on noise. Xplace offers a deterministic mode for this reason.
* **Star model on spread nets.** The driver-to-sink star overestimates load on spread multi-sink nets until the first Steiner correction. Early weights for those nets are pessimistic; that is acceptable while placement is coarse, but do not start buffering decisions before the first correction.

- **Phase 3 ending too late or too early.** Stabilizing until overflow is low leaves resynthesized blocks no room to move; stopping too early sends subblocks to resynthesis that pin weights alone would have fixed. Track both the WNS trend and changes in the near-critical endpoint set, and keep the overflow floor.
- **Resynthesis on the wrong blocks.** If the buffered delay estimates in phases 3–6 are too optimistic for blockage-crossing nets, real logic problems hide behind them; too pessimistic, and resynthesis chases wire problems. Compare the phase-3 estimate for those nets with their phase-7 buffered delay on the first designs.

* **Gate tuning.** θ, k, N, H and the dwell lengths are starting points. Replay the controller over existing CNN and Eyeriss traces first, to check trigger times against the observed plateaus; replay cannot predict the trajectory after a changed activation, so confirm with live runs.
* **Budget fractions.** The original fixed deadlines put repair at iteration 800, equal to the base budget. With budget fractions, repair starts by B − 0.25B at the latest. Check that timing (0.45B) leaves enough room for stabilization before repair.

## References

Linked entries were opened or found in search for this plan. Unlinked entries are cited from memory, so verify their venue and year before quoting.

**Buffering under blockages**

- W. Chen, M. Pedram, P. Buch, "Buffered Routing Tree Construction Under Buffer Placement Blockages," ASP-DAC 2002. [pdf](https://coldflux.usc.edu/~massoud/Papers/buf-route-blockage.pdf)
- C. Alpert, M. Hrkic, J. Hu, S. Quay, "Fast and Flexible Buffer Trees that Navigate the Physical Layout Environment" (BEN), DAC 2004. [pdf](https://www.cs.york.ac.uk/rts/docs/SIGDA-Compendium-1994-2004/papers/2004/dac04/pdffiles/p024.pdf)
- C. Alpert et al., "Porosity Aware Buffered Steiner Tree Construction" (PABST), ISPD 2003 / TCAD 2004.
- H. Zhou, D. F. Wong, I. Liu, A. Aziz, "Simultaneous Routing and Buffer Insertion with Restrictions on Buffer Locations," DAC 1999.
- J. Cong, X. Yuan, "Routing Tree Construction Under Fixed Buffer Locations," DAC 2000.
- D. W. Hightower, "A Solution to Line-Routing Problems on the Continuous Plane," DAW 1969.
- C. Alpert, G. Gandham, J. Hu, J. Neves, S. Quay, S. Sapatnekar, "A Steiner Tree Construction for Buffers, Blockages, and Bays," TCAD 20(4), 2001. The bays are the narrow gaps between blocks.
- Timing-Driven, Over-the-Block Rectilinear Steiner Tree construction with pre-buffering and slew constraints (UT Austin, Pan group; venue believed ISPD 2014, unverified). [pdf](https://www.cerc.utexas.edu/utda/publications/C159.pdf)
- "Cost-Distance Steiner Trees for Timing-Constrained Global Routing," arXiv 2503.04419. LR-weighted delay plus congestion cost in the tree itself. [arXiv](https://arxiv.org/html/2503.04419v1)
- M. Papa et al., "RUMBLE: An Incremental, Timing-Driven, Physical-Synthesis Optimization Algorithm," ISPD 2008 (buffering-aware placement pull). [pdf](https://web.eecs.umich.edu/~imarkov/pubs/conf/ispd08-rumble.pdf)

**Buffer insertion on a tree**

- L. van Ginneken, "Buffer Placement in Distributed RC-Tree Networks for Minimal Elmore Delay," ISCAS 1990.
- J. Lillis, C.-K. Cheng, T.-T. Lin, "Optimal Wire Sizing and Buffer Insertion for Low Power and a Generalized Delay Model," JSSC 1996.
- Z. Li, W. Shi, "An O(bn²) Time Algorithm for Optimal Buffer Insertion with b Buffer Types," TCAD 2006.
- R. Otten, R. Brayton, "Planning for Performance," DAC 1998 (linear delay of optimally buffered lines).
- A. B. Kahng, Y. Liu, Z. Wang, "Recursive Learning-Based Virtual Buffering for Analytical Global Placement" (MLBuf-RePlAce). [arXiv 2506.17247](https://arxiv.org/abs/2506.17247v1). Use its OpenROAD virtual-buffering baseline (TD-RePlAce) as the non-ML comparison.

**Timing models and calibration**

- C. Chu, Y.-C. Wong, "FLUTE: Fast Lookup Table Based Rectilinear Steiner Minimal Tree Algorithm," TCAD 2008.
- C. Alpert, A. Devgan, C. Kashyap, "RC Delay Metrics for Performance Optimization" (D2M), TCAD 2001.
- J. Qian, S. Pullela, L. Pillage, "Modeling the Effective Capacitance for the RC Interconnect of CMOS Gates," TCAD 1994.
- W. Li, Y. Kukimoto, G. Servel, I. Bustany, M. Imani, "Calibration-Based Differentiable Timing Optimization in Non-linear Global Placement," ISPD 2024. [program](https://ispd.cc/ispd2026/slides/ispd2024.html)
- T.-W. Huang, M. D. F. Wong, "OpenTimer: A High-Performance Timing Analysis Tool," ICCAD 2015.
- Z. Guo, M. Liu, J. Gu, S. Zhang, D. Z. Pan, Y. Lin, "A Timing Engine Inspired Graph Neural Network Model for Pre-Routing Slack Prediction," DAC 2022. Used here only for its model structure and its benchmark data, not as a model. [code](https://github.com/TimingPredict/TimingPredict) · [dataset](https://github.com/TimingPredict/Dataset)
- "E2ESlack: An End-to-End Graph-Based Framework for Pre-Routing Slack Prediction," arXiv 2501.07564 (required-time estimation before CTS). [arXiv](https://arxiv.org/pdf/2501.07564)
- LiveHD / LGraph: "A Productive Open Source Hardware Development Flow" (LiveHD project).

**Sizing and placement-coupled timing**

- G. Flach, T. Reimann, G. Posser, M. Johann, R. Reis, "Effective Method for Simultaneous Gate Sizing and Vth Assignment Using Lagrangian Relaxation," TCAD 2014.
- A. Sharma, C. Chu, "Rapid Gate Sizing with Fewer Iterations of Lagrangian Relaxation," ICCAD 2017. [pdf](https://home.engineering.iastate.edu/cnchu/pubs/c92.pdf)
- D. Mangiras et al., "Timing-Driven Placement Optimization Facilitated by Timing-Compatibility Flip-Flop Clustering," TCAD. [record](https://gnosis.library.ucy.ac.cy/handle/7/63422)
- P. Liao et al., "DREAMPlace 4.0: Timing-Driven Placement with Momentum-Based Net Weighting and Lagrangian-Based Refinement," TCAD 2023. [pdf](https://cseweb.ucsd.edu/classes/fa23/cse248-a/papers/placement/dreamplace4.pdf)
- Z. Guo, Y. Lin, "Differentiable-Timing-Driven Global Placement," DAC 2022. [ACM](https://dl.acm.org/doi/10.1145/3489517.3530486)
- N. Viswanathan et al., "ITOP: Integrating Timing Optimization within Placement," ISPD 2010. [pdf](https://home.engineering.iastate.edu/cnchu/pubs/c58.pdf)
- L. McMurchie, C. Ebeling, "PathFinder: A Negotiation-Based Performance-Driven Router for FPGAs," FPGA 1995.
- B. Fu, L. Liu, M. D. F. Wong, E. F. Y. Young, "Hybrid Modeling and Weighting for Timing-driven Placement with Efficient Calibration," ICCAD 2024. Source of the star wire model, cached Steiner correction, driver-to-sink wirelength, hybrid pin weights and per-pin smoothing. [pdf](https://bunchgrape.github.io/docs/iccad24_tdp.pdf) · [code (Xplace)](https://github.com/cuhk-eda/Xplace)
- G. Chen, P. Tu, E. F. Y. Young, "SALT: Provably Good Routing Topology by a Novel Steiner Shallow-Light Tree Algorithm," ICCAD 2017 (shortest-path trees).
- C.-P. Chen, C. Chu, D. F. Wong, "Fast and Exact Simultaneous Gate and Wire Sizing by Lagrangian Relaxation," ICCAD 1998 (min-max delay LR). [record](https://scholars.hkbu.edu.hk/en/publications/fast-and-exact-simultaneous-gate-and-wire-sizing-by-lagrangian-re/)
- A. Marquardt, V. Betz, J. Rose, "Timing-Driven Placement for FPGAs," FPGA 2000 (criticality with an exponent).

**Hold and skew**

- P.-C. Wu, M. D. F. Wong, I. Nedelchev, S. Bhardwaj, V. Parkhe, "On Timing Closure: Buffer Insertion for Hold-Violation Removal," DAC 2014. [record](https://scholars.hkbu.edu.hk/en/publications/on-timing-closure-buffer-insertion-for-hold-violation-removal/)
- "Power and Area Efficient Hold Time Fixing by Free Metal Segment Allocation," DAC 2017. [record](https://scholars.lib.ntu.edu.tw/handle/123456789/427634)
- HoldAdvisor path-aware min-timing fixing (path commonality). [record](https://www.infona.pl/resource/bwmeta1.element.ieee-art-000006081378)
- "A PUS Based Nets Weighting Mechanism for Power, Hold, and Setup Timing Optimization," Integration 2022. [record](https://www.sciencedirect.com/science/article/abs/pii/S0167926022000141)
- Y. Ji, Y. Lu, Z. Zhang, Z. Guo, Y. Lin, B. Yu, "DiffCCD: Differentiable Concurrent Clock and Data Optimization," ICCAD 2025. [pdf](https://www.cse.cuhk.edu.hk/~byu/papers/C291-ICCAD2025-DiffCCD.pdf)

**Benchmarks to compare against**

- A. B. Kahng et al., "Invited: Post-Placement Buffering and Sizing Contest," ISPD 2026. [pdf](https://vlsicad.ucsd.edu/Publications/Conferences/421/c421.pdf)
- Y.-C. Lu et al., "2025 ICCAD CAD Contest Problem C: Simultaneous Gate Sizing, Buffering, and Cell Relocation." [pdf](https://research.nvidia.com/labs/electronic-design-automation/papers/yichen_iccad25_contest.pdf)
