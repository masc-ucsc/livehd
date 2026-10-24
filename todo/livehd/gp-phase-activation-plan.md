# Stability-driven final-placement phases

Status: proposed controller; not implemented. The separately approved fixed
schedule uses 60% initial memory width/height, growth after iteration 70, and
25 growth steps (full size at 95) when vanished memory is enabled.
Scope: LiveHD final global placement, with fixed macros/IO, before legalization.

## Stability rule

For each tracked metric, compare the current GP sample with the mean of the
**previous three samples**, excluding the current sample:

`change = abs(current - mean(previous_3)) / max(abs(current), abs(mean(previous_3)), floor)`

A transition is ready when every required metric has `change <= threshold`.
Start with **5%**, configurable independently for memory growth, routing,
timing feedback, and physical repair. History length is also configurable.

Use a clock-period-relative floor for WNS and an endpoint-count × clock-period
floor for TNS, avoiding division by zero and technology-specific time constants.
Also bound variation within the window so alternating high/low values cannot
look stable merely because their mean matches the current sample.
Missing samples cannot satisfy a gate. A poor plateau may trigger the next
optimization stage; stability does not imply good QoR.

Initially sample every GP iteration, as requested. Make observation cadence
configurable for runtime experiments; three samples only mean three GP
iterations when the cadence is one. Observational STA/RUDY must not update
weights, insert buffers, or otherwise influence the placement trajectory.

## Activation gates

| Stage | Required stable metrics | Action | Initial latest-start deadline |
|---|---|---|---:|
| Initial placement | None | Wirelength + density/fillers/local spreading; uniform initial timing weights | Start |
| Memory growth, if enabled | TNS, WNS, HPWL | Begin the configured growth ramp and coupled local-density decay | 300 |
| RSMT-RUDY feedback | TNS, WNS, HPWL | Enable congestion-driven spreading with bounded inflation | 300 |
| Timing feedback | RSMT-RUDY | Enable timing weights using the proposed virtual-buffer timing estimate | 600 |
| Physical repair/sizing | TNS, WNS, HPWL, RSMT-RUDY | Enable bounded physical buffer insertion/removal and sizing | 800 |

Deadlines force activation if stability has not occurred. They are initial
proposals: memory/timing use the suggested conservative limits; repair doubles
the current first repair checkpoint. Routing has no current fixed start, so its
300 deadline is experimental.

Memory and routing gates run **independently** after initial placement. Either
may fire first; separate thresholds/deadlines allow routing before memory growth.
Skip memory growth when disabled. Density control remains active throughout.

For the first experiment, timing feedback additionally requires completed memory
growth, when enabled. Validate deadline configurations so this dependency can
finish before the timing deadline. Physical repair requires timing feedback to
have started; leave a configurable response interval before enabling repair.

## State and measurement discipline

- Latch activated stages; do not toggle them on/off when curves fluctuate.
- Use fresh RUDY with **full physical memory footprints** for cross-phase
  stability decisions. Also record the current-footprint estimate.
- After memory changes, timing-weight changes, or netlist edits, invalidate
  affected stability histories and collect fresh samples. Deadlines remain in
  absolute GP iterations and cannot be postponed by resets.
- Keep routing-update cadence independent of memory-growth cadence. Bound
  inflation and repair work per update, then allow GP recovery.
- Track full-memory density overflow, filler charge, inflation, local-density
  weight, and density weight. Record these alongside every transition; a density
  gate must not silently postpone a configured deadline.
- Do not mix raw and virtual-buffer timing in a stability window. Continue
  excluding clock/reset networks from preCTS optimization.

## Density recovery alongside every phase

Density forces already run every GP iteration; the adaptive global density
weight is checked every 20 iterations and currently only increases or holds.
The density field itself weakens locally as cells leave crowded bins. Explicit
hard-cap cross-bin relocation currently happens during terminal legalization.

Track physical overflow and per-bin excess as well as stability. Above-target
but improving density can continue; above-target stalled density should trigger
bounded pressure increases and a response interval, not count as density success.
During vanished-memory growth, record both effective and full-memory capacity.
Persistent hotspots may warrant a separate capacity-aware redistribution before
legalization, followed by GP settling; that is a proposed experiment, not an
existing GP feature. Recovery must not silently postpone phase deadlines or
label an iteration-limit stop as density convergence.

## Virtual-buffer stage and validation

The previous `virtual_timing` implementation was removed. Stage four therefore
requires a separately implemented and validated virtual-buffer timing model;
it must not create physical cells or erase wire delay. Record raw placement-RC
WNS/TNS alongside the virtual-buffer estimate and identify which series gates
each transition. Do not silently substitute physical repair for this stage.

Replay candidate gates over existing CNN traces to inspect trigger times, then
run the controller on CNN and Eyeriss against fixed scheduling. Replay cannot
predict the trajectory after changing an activation. Plot transition events,
reasons, thresholds, and metric changes. Test noisy/zero/missing metrics,
deadlines and memory-disabled operation. Validate final QoR after legalization
and CTS/routing.
