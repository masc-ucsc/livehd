# usyn Domino Gates in LiveHD Placement — High-Level Plan

## Goal
Use usyn during the LiveHD flow so that domino-terminated pipeline stages are placed and timed with provisional domino cells, before a real domino cell builder (true LEF/Liberty) exists.

## Assumptions (agreed)
- **usyn runs one-shot before placement**, not inside the placer's resynthesis loop. Domino cell widths are static during placement.
- **usyn emits a Boolean cover per domino gate**; we derive the pull-down network (PDN) ourselves.
- **Pipeline structure:** `flop → CMOS/wire (½T) → domino + output register (½T)`, and `domino-reg or flop → CMOS/wire (T) → flop`. A domino gate always terminates a stage; plain CMOS+DFF stages also exist where usyn chooses.
- **usyn fills the domino half as much as possible**; leftover logic is CMOS in the first half.
- **Domino output is a pulse latch; treat it as an edge register.** No transparency, no time borrowing.
- **Polarity:** single-rail only for now. A gate needing both polarities becomes two single-rail macros (area overestimate accepted).
- **Geometry:** double-height (2 rows), no rail-parity constraint, vertical flip allowed.
- **Area model:** fixed overhead of 7 inverter *areas* (latch + precharge/keeper/foot), plus 1 inverter area per NMOS pair in the PDN. Area-equivalent, so double height halves the width.
- **Sizing:** domino cells get drive variants that the placer's sizing step can choose.
- **Q7 default:** placement only buffers/sizes; no usyn rerun. The proxy still reports per-stage slack so an outer loop can be added later.

## Pre-task 0: track-direction-aware placement feedback
**Rule (as given):** prioritize cell placement so that odd cell tracks carry vertical traces and even cell tracks carry horizontal ones. Long crossing wires (when using satlegalize) go over odd (vertical) tracks, with at most one long wire per track, so power/clock pins keep access.

**Working interpretation (to confirm):** each track has a direction class (odd = vertical, even = horizontal). The placer gets feedback that:
- aligns cell pins with the track class they will route on;
- treats each odd track as having capacity 1 for long wires, a per-track resource rather than an area density.

**Why before the domino work**
- The domino abstract generator (task 2) fixes pin offsets. Pins must land on the right track class, or every domino cell starts misaligned.
- Domino CLK pins are numerous and heavy. They are the pins most hurt by a long wire occupying their access track, so the one-long-per-track rule bites hardest in domino-dense regions.

**Deliverables**
- Track-class map derived from the row/site grid, including how it maps onto 2-row domino cells.
- Pin-offset rule consumed by the abstract generator.
- Placement cost/feedback:
  - pin–track-class misalignment penalty;
  - per-odd-track long-wire occupancy, with an overflow penalty above 1.
- Definition of a "long" wire: a length threshold in tracks or GCells, possibly tied to whether it crosses a blockage.
- satlegalize interface: how long-wire track assignment is exposed to and constrained by the legalizer.

**Open questions / weak points**
- **Physical meaning of "track".** In ASAP7-class nodes each metal layer is unidirectional (SAQP), so a single layer cannot alternate direction per track. Is the odd/even split across placement-site columns, across layers, or a pin-access discipline? This decides whether the rule is a placement constraint or only a router hint.
- **Capacity 1 per odd track is very tight.** In congested or domino-heavy areas long nets will overflow. Decide whether overflow is a soft penalty (GP) or a hard constraint (satlegalize). A hard constraint can make legalization infeasible.
- **Interaction with RUDY.** Per-track occupancy is a finer resource than RUDY. Keep both, or replace RUDY in the stabilization phase?
- **Power/clock access** must be checked against the actual rail and CLK pin positions of the 2-row domino abstract. Flipped cells change which tracks the pins see.

## Flow
1. **Synthesis (LiveHD + usyn)**
   - usyn identifies domino-terminated stages; the rest maps to standard CMOS + DFF.
   - Each domino node carries: cover, polarity, derived stack height, drive variant (initial X1).
   - Cover → factored-form PDN (not flat SOP, to avoid overcounting). Split gates whose stack height exceeds a cap (default 4).

2. **Provisional domino abstract generator** (runs at synthesis time)
   - `N_nmos`, stack height, and leg count from the PDN.
   - Width:
     ```
     A_domino = (7 + ceil(N_nmos / 2)) * A_inv  [+ drive-variant increment]
     W_domino = ceil(A_domino / (2 * H_row * W_site)) * W_site
     ```
   - Drive variants X1/X2/X4; each adds output-stage area (a few A_inv).
   - Bin by `(n_inputs, N_nmos, stack, drive)` to keep a small number of unique cells.
   - **Outputs:**
     - LEF-like abstract: size, 2-row height, pin offsets (inputs, CLK, Q).
     - Timing sidecar: `t_cq`, `t_setup`, `t_hold` (= pulse width + skew), internal delay `t_latch + k(stack, legs) * FO4` with k ∈ [2, 4], `R_drive` per variant, `C_in` per pin, `C_clk` (precharge + foot + latch, grows with drive).
   - The interface must stay stable so that real LEF/Liberty can replace it later.

3. **Timing proxy (extend existing LiveHD topological proxy)**
   - Startpoints: DFF and domino registers, all edge-launched.
   - Endpoints and budgets:
     - into a domino gate: `T/2 − t_setup` (CMOS + wire only)
     - into a DFF: `T − t_setup`
   - Separate check: domino internal delay ≤ `T/2`. Fixed after synthesis; the placer cannot fix it.
   - Achieved period: `T = 2 * max(worst CMOS segment into domino, worst domino internal)`, combined with the full-cycle DFF paths.
   - Emit per-stage slack (segment class, slack, stage ID) for a future usyn outer loop.

4. **Placer (DREAMPlace-based)**
   - **Global placement:** correct area and pin offsets for double-height cells; density model unchanged.
   - **Legalization:** mixed-height Abacus extension; a domino cell is a cluster spanning two adjacent rows, any row pair, flip allowed.
   - **Detailed placement:** v1 treats domino cells as fixed anchors; v2 adds 2-row-aware moves and swaps.
   - **Sizing/buffering:** domino drive variants are treated like standard-cell sizes. Buffers after a domino register only when the largest variant is insufficient.
   - **Clock:** model the domino `C_clk` explicitly (much heavier than a DFF CLK pin) in any clock-aware weighting.

5. **Evaluation**
   - Baseline: all-CMOS + DFF flow on the same designs.
   - Metrics: achieved T, total area, HPWL/RUDY, domino and DFF clock sink count, total clock-pin cap.

## Work breakdown
| # | Task | Depends on | Output |
|---|------|-----------|--------|
| 0 | Track-direction classes (odd = V, even = H), pin-offset rule, per-track long-wire occupancy feedback, satlegalize hook | — | track map + placement cost terms |
| 1 | Cover → factored PDN → `N_nmos`, stack, legs; stack-cap splitting | usyn cover format | PDN library + unit tests |
| 2 | Abstract generator: width formula, drive variants, binning, pin offsets | 0, 1 | LEF-like abstracts |
| 3 | Timing sidecar: internal delay, `R_drive`, `C_in`, `C_clk`, setup/hold | 1 | sidecar format + loader |
| 4 | LiveHD netlist: domino node type + attributes, mixed with CMOS/DFF | 1 | LGraph representation |
| 5 | Proxy: two endpoint classes, domino internal check, per-stage slack report | 3, 4 | extended timing proxy |
| 6 | Placer GP: double-height cells | 2 | DREAMPlace input path |
| 7 | Mixed-height legalizer (2-row clusters, flip) | 6 | legalizer |
| 8 | DP: domino anchors (v1), 2-row moves (v2) | 7 | DP changes |
| 9 | Sizing/buffering over domino variants | 3, 5 | sizing integration |
| 10 | Experiments vs all-CMOS baseline + sensitivity sweeps | all | results |

## Risks and required experiments
- **Uncalibrated area model.** Sweep the 7× overhead and the per-pair cost ±25%; report whether conclusions flip.
- **Clock load and power.** Domino CLK pins are heavy, so a frequency gain may be offset by clock power and CTS. Always report total `C_clk`.
- **Optimistic delay.** Charge sharing, keeper contention and noise are ignored. Run a pessimistic corner with k = 4+ FO4.
- **Half-cycle imbalance (fixed 50% duty, no borrowing).** If CMOS halves sit well below ½T, usyn is absorbing too much or too little. Feed this back to usyn rather than counting it as a placement win.
- **Hold.** Pulse-latch hold = pulse width + skew, slightly worse than a DFF. Remains post-route, but check short CMOS segments into domino gates early.
- **Dual-rail overestimate.** Two macros instead of one shared-overhead macro inflate area on dual-rail gates; quantify how often usyn needs both polarities.
- **Legalization at scale.** Watch displacement and runtime as the domino area fraction grows.

## Deferred
- Real domino cell builder (true LEF/Liberty) replacing the provisional abstracts.
- Single dual-rail macro sharing the latch/precharge overhead.
- Outer usyn loop driven by per-stage slack.
- Duty-cycle knob and time borrowing.
