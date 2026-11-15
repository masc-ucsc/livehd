# Interactive placement with `lhd synth --gui`

Start a placement with `--set synth.place=true --gui`. Add `--set synth.place.gui.pause=true` to stop at the first gate. The log prints the browser URL and a mode-0600 Unix socket. The commands below accept the socket path or its entire log line. With a named workdir, `--workdir W` reads the socket from `W/synth/place/gui_agent.json`.

```sh
export LHD_GUI_SOCKET="$socket"
tools/lhd_gui.py hello
tools/lhd_gui.py help
tools/lhd_gui.py find scope=macros regex=.
tools/lhd_gui.py export what=macros
```

Use `hello` to identify the LiveHD build, technology, design, inputs, options, controller and worker limits. Keep the same executable for the whole session: replay starts a fresh process and rejects measurements from a different build. Use `describe name=ram0` for the placed body, allowed orientations, pin-side counts and exact signal pin coordinates. The macro pin layer shows the oriented access points. Socket positions are lower-left coordinates in DEF database units. Divide by `dbu_per_um` for micrometers. The page labels editable core dimensions in micrometers.

Check `hello.capabilities` before choosing an experiment. `h2d_intent=false` means the H2D solver ignores placement hints; use native macro GP to evaluate a relationship. With rigid macro magnet contraction, absolute positions can seed the uncontracted design, but the solver cannot apply relative hints to the contracted nodes (`rigid_macro_magnets=false`). Region forks do not support macro buses (`region_macro_buses=false`); evaluate their effect on the full design.

Build the GUI-enabled driver from the LiveHD checkout.

```sh
bazel build -c opt //lhd
```

To check process cleanup and snapshot/replay on your host (including macOS), run:

```sh
bazel test -c opt --config=backend //lhd/tests:lhd_gui_lifecycle_smoke //lhd/tests:lhd_gui_native_continuation_smoke
```

## HighTide on ASAP7

For a HighTide checkout with `Makefile.sample`, the sample prepares the five-SRAM LiteEth design, with its generated RTL, ASAP7 libraries and design SDC. From the HighTide directory, `make -f Makefile.sample detail RUN=runs/reference-01` replays the supplied `designs/asap7/liteeth/livehd-placement.json`. To save and replay a different placement:

```sh
make -f Makefile.sample explore
# Export → Design metadata + exact macro coordinates; save the file as design.json.
make -f Makefile.sample detail METADATA=design.json RUN=runs/replay-01
```

The second command replays the positional JSON in a fresh LiveHD workdir. OpenROAD reads the complete LEF/DEF handoff and verifies the macro inventory, master names, fixed positions and orientations. It runs detailed placement and checks legality and the poses again before writing `detail.def` and `detail.odb`.

Choose **Geometry** to move macros on a private frame. Export a gap or alignment as a relationship when it should adapt to another floorplan; export exact poses to reproduce the current arrangement.

## Edit and measure a frame

At a paused placement gate, read `state`. A client can send `expect` with the published `frame` and optional `target`, `intent` revision or `baseline` identity. If an edit depends on a timing result, also set `expect.evaluation` to the observed `state.evaluation_seq`. A fresh full timing attempt advances this sequence even when the placement frame is unchanged. Direct edits and wrapped page commands return a queue sequence. Query `command_status target=ID sequence=N` for its outcome. Applied moves return the resolved DBU coordinates, pin state and conflicts of each addressed instance. A revision that changes before execution yields `stale`. Command receipts are retained for up to 128 commands, with a 2 MiB history budget plus space for the latest receipt. Read the resulting frame before requesting a measurement. `cmd=help` describes the live command vocabulary.

```sh
tools/lhd_gui.py policy fast=true
tools/lhd_gui.py move 'nodes=["ram0"]' 'dx=[5000]' 'dy=[0]' final=false
tools/lhd_gui.py geometry
tools/lhd_gui.py evaluate
tools/lhd_gui.py state
tools/lhd_gui.py evidence
```

For a chosen edit on main, send its final coordinates with `final=true` (the default). That records the released move in main’s action trace and requests fresh timing. Absolute moves with `final=false` are intermediate drag samples: you can inspect and export that frame, but the public action trace omits them.

An explicit macro move may produce an overlap, halo conflict, blocked channel or out-of-core body. The GUI displays **ILLEGAL GEOMETRY** and names the conflicting objects. A pinned macro keeps its anchor while a conflict suspends enforcement. A fresh pin requires clear geometry. `legalize` invokes the physical legalizer and reports every displacement or the failure reason in `state.legalization`. Its result is applied on success. The `geometry` response includes the usable rectilinear outline, periphery, facing macro strips, their entrances and rasterized dead-space components. The pitch in the response specifies the resolution of the dead-space metric.

Choose **Macro channels → Measure channels** to see the smallest gap between a macro halo and its nearest outline edge. Select a macro under **Macro faces** to see the signal-pin count and available row heights or site widths on each face. These are geometric upper bounds, not counts of free placement sites. The socket exposes the same values in `pin_access_proxy` for each side. In `geometry.macros`, `edge_halo_clearance_dbu` is the signed gap on `halo_boundary_side`. With unequal horizontal and vertical halos, this can differ from the nearest body edge reported by `boundary_side`. A negative value means the halo crosses that edge. `edge_pitch_capacity` gives the maximum number of row heights (top/bottom) or site widths (left/right) that fit in the gap; it is null when the pitch or boundary is unknown. A positive gap can still be too shallow for one row. Other obstacles or grid alignment can further reduce usable space.

To reuse a candidate's relative moves, choose **Export → Relative moves → Download JSON** on its page or run `tools/lhd_gui.py export target=x1 what=relationships`. The ordered `moves` array contains the displacements and named edge, alignment or gap commands authored on that target. It uses stable instance names and includes orientations supplied with a relative move. Absolute positions are available under **Macro coordinates**. A child exploration starts its own move list at its parent’s captured placement.

Paste the downloaded JSON into **Evaluate this frame → Placement patch** and submit the candidate, or pass its `moves` array as the socket’s `explore.patch`. At a live gate, `batch` accepts the array under `moves` and an optional common `expect`. Entries execute in order, so later alignments see earlier displacements. An alignment measures the macro body by default. Add `"edges":"halo"` to measure from the configured macro halo on each axis. Use `dbu_per_um` in the download to convert lengths when transferring between technologies.

Open **Pin anchors** to see the requested DBU coordinates and whether each GUI pin is active. Active macro pins keep their positions when ordinary placement resumes. Macro alignment, row legalization and detailed placement treat them as fixed obstacles. A standard cell held between sites stays at its requested coordinate during global placement; row legalization places it on an available site. A change at the legalization gate takes effect even when global placement ran zero iterations. Before packing, whole-design legalization checks suspended anchors together. It can restore an anchor by moving a free neighbor away; a conflict with another anchor, an exclusion or fixed geometry keeps it suspended. Dragging another macro through it suspends enforcement and lists the conflict. Once it clears, the page identifies the restored pin at its original anchor. The socket exposes the same state under `pin_intent` and the causes under `geometry_conflicts`.

Open **Macro channels**, choose **Measure channels**, then select a macro pair. The canvas outlines the channel and marks its open entrances in green. Each measurement belongs to the indicated frame. The socket returns the usable channel area in `free_dbu2`, the count of intersecting standard cells in `standard_cells`, their occupied area in `standard_cell_area_dbu2` and the occupied fraction in `standard_cell_utilization`. It clips each cell at channel edges, other macros and floorplan exclusions. Overlapping cells each contribute their occupied area, so a pre-legalization frame can exceed 100%. Each cluster also reports its member count, bounding box and half-perimeter.

The implementation panel and `state.slow_work` show the terminal operations configured for this recipe, their triggers and policy deferrals. The socket also reports the remaining one-shot trial budget and the last measured timing, RC extraction and STA durations in seconds.

The three mutation controls are `no_resynthesis`, `no_sizing` and `no_buffer_edits`. Set each independently with `policy`. The fast preset closes all three. A gate becoming eligible follows its configured stage trigger. A timing refresh can rank critical pins. For WNS/TNS in ps, require `has_timing`, a full evaluation (`timing_est=false`) and `evaluation_frame == frame`. A null measurement means unavailable. `summary` gives the timing, congestion and configured quality score with their coverage. `score_config` accepts an estimator and weighted loss components. The score guides inspection and has no role in flow acceptance.

Open **Configure quality score** to choose the routing estimator, enter loss weights that sum to one and set each half-score loss in the indicated units. The socket accepts the same settings through `score_config`. A queued candidate retains the settings from submission. The routing summary measures the configured estimator independently of the congestion map selected for display.

## Try candidates and discard them

Choose **Begin exploration** to open a private foreground evaluator. **Evaluate this frame** submits a background geometry, timing, relaxation, repair or downstream-routing candidate from the displayed live frame. Select the operation and its budget. For bounded relaxation or sizing, **Limit to selected cells** resolves the current selection to exact names when the candidate is submitted. A terminal repair can use sizing, buffering and resynthesis when the build supports them. For a scoped repair, only sizing is available. Inspect the saved membership with `export what=scope target=ID`.

In **Explorations**, use **Cancel** on a queued or running job, or **Discard** to release a completed or failed job. Compare two open T0 geometry candidates to see their cell-density differences, displacements and orientation changes. **View** opens the candidate’s frame and identifies it in the toolbar. Beginning a foreground exploration opens the interactive scratch page as soon as the evaluator is ready. Discarding it returns to main. Closing a candidate releases its maps and retains its summary in the ledger.

Repeated `explorations` queries can send the previous response’s `epoch` and a `known` object mapping candidate IDs to `terminal` (completed background work), `closed` or `evicted` (compacted history). The response lists matching IDs under `unchanged` and sends full records for everything else. The browser uses this to preserve focus and skip transferring completed patches and timing evidence on each refresh.

After discarding a foreground run, choose **Save record** on its row or request `explorations` with its ID. The record includes its private action segment and the last successful full timing measurement, if one was requested and available. The measurement names the worker epoch, frame, trace sequence, placement, topology, masters and policy. A subsequent drag can appear later in the segment. Discard preserves the earlier measurement at its original frame. Compacted history retains the segment count and hash alongside measured totals.

The foreground worker replays to a named checkpoint in a private workdir. An agent can send `explore target=ID` at a stable foreground gate. The new job inherits the scratch edits, including its in-progress drag, and reports the foreground ID as its parent. Main holds its original frame throughout. The fast timing engine can run T1 translations and iteration-bounded T2 relaxations, either over all movable objects or in an explicit scope in a copy-on-write snapshot. A mid-GP pause records its phase, iteration and occurrence for replay. On first use the controller starts a single-threaded state owner, restores the physical arrays, Liberty corners, resolved constraints and net RC, and verifies baseline WNS and TNS. Each subsequent candidate forks that owner and computes its own touched-net RC, full timing and maps. Use `worker=snapshot` or `worker=replay` on an `explore` request to choose explicitly. The default `synth.place.explore.worker=auto` selects replay for other timing engines, loaded URoute models, native intent or orientation changes, flag changes and implementation runs. The controller continues to serve requests during evaluation. `hello.capabilities` reports the snapshot lifecycle available in this build. At a stable gate, a replay checks the scene, positions, pin anchors, intent, policy and physical input identity before applying its patch. At a native global-placement callback, `state.optimizer_identity` also identifies the private particle positions, gradient history, holds and convergence state; outside those callbacks it is null. A replay with the same displayed positions but a different optimizer identity returns `stale`. An unavailable baseline returns `stale`.

A T2 iteration budget starts a fresh relaxation from the displayed positions. To continue the original solver, request `explore tier=T2 until=gp_end` at a mid-placement pause. When `state.native_gp_snapshot` is true, the serial numeric owner can fork from that iteration with its optimizer history intact. The candidate accepts private translations and pin changes, runs the remaining iterations, and evaluates full timing and congestion. Discarding it leaves the main solver at its original iteration. When the snapshot is unavailable, `state.native_gp_reason` identifies the missing condition. Check the ledger’s `worker`, `work` and `replay_work`: an automatic request replays if the checkpoint cannot be retained; an explicit snapshot request reports why it cannot run. Pending main edits must reach a solver poll before they can be part of a native checkpoint. Threaded or host-dependent solver recipes, changes to earlier flags, and stops beyond this GP solve use replay.

| Command | What it measures |
| --- | --- |
| `explore tier=T0` | A proposed frame and its geometry findings. Runs in the controller. |
| `whatif` | Frozen-netlist timing for up to 64 small moves or ordered batches. Each touched net receives fresh RC including exterior loads. The timer verifies rollback after each candidate. |
| `explore tier=T1` | One full placement-RC evaluation of the candidate at frozen coordinates. |
| `explore tier=T2` | Bounded global placement and legalization followed by timing. Use `budget.iterations`, optionally `budget.active_cells`, and a movement scope. To advance the ordinary full placement flow, use `until=gp_end`, another named placement gate, or `until=end`. |
| `explore tier=T3` | One terminal implementation-repair experiment. Specify an explicit sizing, buffering or ABC cone resynthesis policy and a bounded number of repair trials. Inspect the resulting implementation identities and consumed trials. |
| `explore tier=T4` | Route the proposed macros using the configured benchmark recipe. Returns the applied macro poses, OpenROAD measurements, status and evidence hashes. |
| `explore_begin` | An interactive foreground evaluator at the same gate. Issue ordinary commands in its private session and discard when finished. |

T4 passes the worker’s thread allowance to OpenROAD and caps subsequent Tcl requests at that allowance. The validation receipt records the thread counts OpenROAD reported. When scoring manually, omit `--threads` to use the benchmark’s own thread settings.

```sh
tools/lhd_gui.py explore tier=T1 label=reference 'patch=[]'
tools/lhd_gui.py explore tier=T1 label=east 'patch=[{"cmd":"move","nodes":["ram0"],"dx":[5000],"dy":[0]}]'
tools/lhd_gui.py --timeout 65 explore_wait 'ids=["x1","x2"]' timeout_ms=60000
tools/lhd_gui.py compare reference=x1 candidate=x2
tools/lhd_gui.py export target=x2 what=macros
tools/lhd_gui.py explore_discard id=x2
```

A short T2 run answers whether a move is worth keeping. Before routing a selected floorplan, replay its **exact macro state** in a clean run and let whole-design standard-cell placement use its normal budget and density grid. Check the reported placement overflow and the router's congestion separately. A legal placement can still concentrate cells enough to prevent routing. If the fixed poses, orientations or constraints change, it is a different floorplan; record it separately.

Use the returned IDs. For T2, choose `scope.nodes`, `scope.box` in DBU, `scope.hierarchy`, a named `scope.cluster` from `clusters`, or a current `scope.region`. An optional `halo_dbu` includes neighboring cells. The default scoped solver minimizes incident-net weighted HPWL for the active cells with cached exterior pin extrema. Full-model legalization and timing retain exterior obstacles and loads. A box or region also bounds the final cell bodies. An infeasible legalizer reports failure. For a frozen-exterior reference, set `scope.kernel` to `full_reference` with the same membership and budget. Each replay currently holds the complete design in memory. For T3, for example, send `policy={"sizing":true,"buffering":false}` and `budget={"repair_trials":1}`. Compare timing only at matched evaluation level, timing coverage and estimator. A frozen result includes the actual evaluated topology and masters.

`explorations` reports state, queue/start/runtime, evidence and closure history. Set `synth.place.explore.jobs`, `heavy_jobs`, `queue`, `open` and `timeout` to control concurrency, retained results and worker lifetime. T1–T3 accept an optional `budget.elapsed_s` to shorten a candidate’s deadline, including queue time. A positive `timeout.t1` through `timeout.t4`, `timeout.foreground` or `timeout.region` overrides the common deadline in seconds. The `memory_mb` and `disk_mb` limits cover each worker’s process tree and private directory. `threads` sets the CPU share reserved for each replay worker. Snapshot and foreground evaluators use one thread. The ledger reports sampled peak resident memory for the worker and the whole session, plus private disk usage. Set `synth.place.explore.session_memory_mb` to limit aggregate RSS across the retained main evaluator, controller, snapshot owner and all candidate descendants. Its default is half the detected host or cgroup RAM. Admission includes predicted worker growth. A worker that crosses the session limit reports `budget_exhausted`, while main stays available. RSS counts shared pages in each process, so the budget is conservative for copy-on-write snapshots. `history_mb` bounds detailed closed records. After a detailed record expires, the ledger retains its candidate ID, baseline, tier, status, measured metrics and cost through session shutdown. Workers share `lhd.jobs` with other work, including load/memory admission. A foreground experiment uses main's reserved capacity while main waits. Region extraction also consumes a share and creates a page with cut boundary pins. That page models the extracted design. Transfer desired edits to main with ordinary commands, then evaluate main. Discard closes the scratch process and removes its private files. Choose its ID, its unique label, or `explore_discard all=true` to close every currently open candidate.

The `set` field accepts string values for a new replay recipe, including `lhd.seed`, `synth.place.aspect`, `utilization`, `area`, `core`, `die`, `density`, `halo` and `iterations` under the same placement prefix. A variant records its actual replayed baseline. Compare it against an empty patch run with the same flags, gate, scope and estimator. **Export → Changed flags** and `export what=flags target=ID` return the target’s physical recipe overrides and changed mutation controls relative to the invocation. The ledger separates queue time, worker startup (including replay), and candidate run time. It records worker CPU seconds and peak RSS at exit. After the replay reaches its gate, `work` counts the candidate’s full and ranking timing requests and completed GP iterations. `replay_work` records the same counters for reconstruction. A hard stop retains the last observed counters, and a worker that exits before reporting them returns null. External routing has its own downstream receipt.

T1 and iteration-bounded T2 each issue one final timing request after the patch. A patch can include several released drags. For a named T2 stop or T3 repair, set `budget.timing_calls` to cap the combined full and ranking requests, including placement and repair requests. The result reports `budget_exhausted` when the next call would exceed that cap. The browser exposes this limit for repair and shows measured calls and iterations for each candidate.

Use `move` with an `orientation` field to flip an editable macro at an FP or macro gate. DEF orientations N, FN, FS and S follow the LEF symmetry of each master. A single orientation applies to all listed nodes, or an array supplies one per node. Include zero displacement for an orientation-only edit. `signature` describes each macro’s group, boundary side, side order, coarse position and orientation. `similarity` compares two signatures by macro name.

`maps` reports same-frame density, RUDY, RSMT and available URoute grids, plus peaks, over-capacity share and hotspot boxes. Open a worker to inspect individual layers. The Compare panel plots macro displacements and per-bin candidate-minus-reference utilization. `leverage` reports constrained endpoint counts, WNS and TNS by clock and path family, together with coverage of resolved failing paths. Channel crossings use straight segments between successive cell centers. For an unresolved path, its endpoint still contributes slack to the totals.

In the Compare panel, the timing table shows candidate minus reference WNS, TNS and failing-endpoint count for each capture clock and path family. Each group records a fingerprint of its constrained endpoint names. If the endpoint set changes, the panel gives both counts and the socket returns null for that group’s deltas. An expired detailed record retains total and per-family timing. An illegal intermediate placement carries its geometry warning even when its score is available.

A full timing result identifies the timer, setup corners, clock periods and SDC issues under `timing.endpoints`. Fmax uses the worst setup slack when every measured endpoint follows the period of one clock. Ranking, multiple clocks and unverified clock dependence leave Fmax unavailable. Compare checks clock assignments as well as endpoint names. The `geometry` field identifies the legality of each side, including the input placements submitted for routing. Each summary metric and score component reports its validity on the measured geometry. Comparison maps use the reference floorplan and the frame of each measurement.

The timing panel, candidate ledger and `evidence` response flag negative setup slack, negative hold slack or electrical violations as reasons to consider a separate repair trial. The advice uses only a full timing measurement of the current frame. Missing hold or electrical data supplies no reason; a ranking refresh or stale frame has no advice. The message reports whether implementation changes are frozen. Reading it does not consume an evaluation or enable any repair. Each reported reason is a measured violation, not a prediction that repair will improve routed timing.

## Replay exact coordinates

Choose **Export → Design metadata + exact macro coordinates** on the desired frame, or save the `records` field from `export what=macros format=metadata`. The downloaded file is the normal array of class-tagged LiveHD metadata. It preserves other design records and replaces this design's `livehd.placement.state` with the selected macro positions. Pass that array directly alongside the RTL to a clean run with the same physical recipe:

```sh
lhd synth chip.v design.json --top chip --workdir replay --set synth.place=true \
  --set synth.liberty=cells.lib --set synth.lef=tech.lef,cells.lef,macros.lef
```

A completed batch placement writes its macro poses to `synth/place/metadata.json`, even without input metadata. For socket clients, save the records themselves:

```sh
tools/lhd_gui.py --workdir W export what=macros format=metadata encoding=json_text |
  python3 -c 'import json,sys; print(json.load(sys.stdin)["records_json"])' > design.json
```

A state record fixes each named macro's lower-left position in micrometers and its DEF orientation. The native placer supports N, FN, FS and S when allowed by the master. Standard cells legalize around these macros. A changed master, missing instance, unrepresentable coordinate, conflicting fixed floorplan, or illegal placement fails replay. To explore another core size, keep the relationship hints and omit the design's exact state.

You can pass `placement.json tuning.json` alongside the RTL, or join their arrays first. Each pass reads its own class. Placement preserves the other passes' records in its output; conflicting records with the same class, design and ID are rejected. To join files using Python 3, preserving 64-bit integer fields:

```sh
python3 -c 'import json,sys; json.dump([r for p in sys.argv[1:] for r in json.load(open(p))], sys.stdout)' \
  placement.json tuning.json > design.json
```

## Save and transfer a relationship

The page can preserve an explicit align, distribute or matrix operation on selected macros as a soft relationship. For a sidecar, use the existing positional metadata input beside RTL. The following record selects four named banks in fixed row-major order starting at the lower y coordinate. Physical gaps stay in micrometers across core-size changes.

```json
[{
  "class": "livehd.placement.hints", "schema_version": 2,
  "design": "chip", "id": "banks",
  "templates": [{
    "module": "chip",
    "hints": [{
      "id": "bank_matrix", "kind": "matrix", "columns": 2,
      "members": [["ram0"], ["ram1"], ["ram2"], ["ram3"]],
      "gap_x_um": 5, "gap_y_um": 5
    }]
  }]
}]
```

`intent_preview` and `intent_apply` accept either the design bundle or an array containing just the placement hints. Supply the complete set of applicable hints you want to keep; an empty array clears them. Both commands preserve other classes and hints scoped to other designs. Preview resolves hierarchy and measures residuals on the selected frame. In the browser, dashed rectangles show target macro footprints and solid lines connect them to the current centers. Pin-facing hints mark the requested edge. Toggle **Show intent targets** to inspect the underlying placement. The overlay expires when the frame changes. The socket returns the same `targets_dbu`, command sequence and measured frame in `export what=intent`. Applying installs those hints at a gate. `export what=intent` returns the authored records alongside the binding and residual report. Save the authored records as the positional JSON input. Instance overrides address a hint by its ID and exact instance path. Inspect the report for ambiguous subjects, disappeared instances or infeasible relationships. The trace records execution-specific moves and revisions. Use the authored sidecar for transfer.

For a cluster, the residual is the farthest member center's distance from the group centroid. A cluster has no maximum radius. For an advisory coordinate seed, the residual is the current lower-left's distance from the seed; successful initialization does not hold the macro there. Both distances are in micrometers. A matrix that cannot fit the core with the requested gaps and halos reports that reason even when the individual members miss their targets by less than one pitch.

For a logical memory wrapper retained in the input hierarchy, a group reference such as `"storage"` selects its physical SRAM banks in the current module instance. A missing or ambiguous bank discards that instance’s relationship. With an already-flat netlist, supply exact physical instance names or declare a sidecar group. Each ordered matrix or distribution slot needs one physical instance.

The schema-2 `pin_facing` hint takes a macro `subject`, exact signal `pins` and a compass `side`. For example, `{"id":"addr_east","kind":"pin_facing","subject":["ram0"],"pins":["A0","A1"],"side":"east"}` prefers legal orientations bringing those pin centers nearer the east side of the body. The residual is the mean pin-center distance in micrometers. Explicit coordinate seeds with an orientation take precedence on the same macro.

## Validate downstream

```sh
tools/placereason.py "$data/bp_fe" --aspect 2:1 --workdir repros/bp_fe --run
tools/lhd_gui.py --workdir repros/bp_fe export what=macros format=placereasoner > repros/bp_fe/coords.json
tools/placereason_score.py "$data/bp_fe" --aspect 2:1 --coords repros/bp_fe/coords.json \
  --flow "$placereasoner_beta" --workdir repros/bp_fe/score --placement-seconds "$seconds"
```

The socket command `export what=macros format=def` returns a standalone DEF in the `def` field and its component section in `components`. It records every exported macro as FIXED with its external instance name, integer coordinates and DEF orientation.

The importer uses the supplied mapped DEF, rows, LEFs, Liberty and SDC. It holds the imported port locations used by the downstream scorer (`synth.place.preserve_imported_io=true`); unplaced ports still receive a location. By default it reads the benchmark clusters. To replay a saved design, pass the complete metadata array in their place:

```sh
tools/placereason.py "$data/bp_fe" --aspect 2:1 --metadata design.json --workdir repros/replay --batch --run
```

The importer passes this file straight to LiveHD. Its cluster hints, exact macro state and other passes’ records can travel together. Splitting or joining the same records preserves the placement replay identity. Simulation settings, observations and another design’s records remain in the exported bundle without invalidating a placement baseline. A change to applicable placement hints, macro state, synthesis tuning or row-access conventions invalidates the saved baseline. Drop `--batch` to open the replay in the GUI. The scorer applies each macro by exact name and orientation and runs the upstream OpenROAD detailed-placement, CTS and routing templates. That benchmark reads only the macro poses from a candidate: sizing or moving its standard cells affects the placement estimate, but those changes do not enter the benchmark netlist. Identical macro exports can share a downstream result when the input netlist, constraints, tool, templates and execution settings match. Its new output directory contains the coordinates, applied macro poses, input hashes, log, reports and benchmark row. Failed downstream runs retain their evidence. Report routed timing from the downstream row and placement-RC timing from the LiveHD report.

A T4 result requires the requested macro lower-left coordinates and orientations at OpenDB precision. The scorer rejects a coordinate outside the signed DEF database-unit range or between two grid units, using the same numerical tolerance as LiveHD’s saved-state reader. The adapter checks the full macro inventory immediately after applying the proposal, then checks that every macro remains fixed at that pose and calls OpenROAD `check_placement -verbose` after routing. A successful route with an illegal final placement fails validation. Caught intermediate detailed-placement failures are recorded separately: later passes may repair them. The GUI receipt names each check as `passed`, `failed`, or `not_run`. The GUI accepts a routed result only when the scorer and OpenROAD exit successfully and all three final checks explicitly pass. A contradictory receipt stays available at `/validation/receipt.json?x=ID` for inspection while the worker is open. The two pose tables and `placement_checks.json` are available under `/validation/score/` while the worker is open.

A completed route can have no constrained timing paths. After the final OpenROAD placement check, the scorer looks for a constrained setup path in that graph before reading WNS and TNS in nanoseconds. If timing is unavailable, the CSV fields stay empty, the ledger gives the reason, and the geometry and DRC results remain available. Inspect `/validation/score/setup_timing.json?x=ID` for the check. The benchmark uses global-route parasitic estimates in its final report; use extracted SPEF if you need wire RC from detailed routing.

Expand **Routed setup timing by clock and path family** on a completed job to see how many endpoints failed and where the TNS came from. Each capture clock has its own count; the total counts a physical endpoint once, using its worst path. The scorer checks that these paths reproduce OpenSTA’s final worst slack and TNS. The groups are in picoseconds; the benchmark row stays in nanoseconds. Download `/validation/score/setup_endpoints.tsv?x=ID` for individual paths (names are hex-encoded UTF-8).

**Compare** shows timing differences only for matching clock periods and endpoint identities, including each endpoint’s capture clocks. If an exception removes a path or a repair changes the endpoint set, the affected groups show their old and new counts; groups with unchanged coverage can still be compared. An older result without this evidence can still supply physical differences such as DRC and wirelength. The main frame and placement candidates have a separate **Placement setup timing by clock and path family** table.

The routed **View** shows final downstream setup timing and the quality score configured when the job was submitted. Its drawing shows the macro proposal sent to OpenROAD. A timing-only score uses the final benchmark row in picoseconds. Routed wirelength cannot stand in for placement HPWL, and a DRC count cannot stand in for a RUDY, RSMT or URoute map. A score requiring one of those missing measurements reads **unavailable**. Score differences require matching score definitions and coverage for their inputs.

To route directly from the GUI, prepare the session with `tools/placereason.py "$data/bp_fe" --aspect 2:1 --workdir W --flow "$placereasoner_beta" --openroad "$openroad" --run`. At a stable gate submit `explore tier=T4 'patch=[]'` or a list of macro moves. An optional nonnegative `placement_seconds` records the time spent producing imported coordinates. Otherwise the result includes elapsed session time through submission. The ledger separates this value from worker and queue wall time. While the result is open, fetch `/validation/receipt.json?x=ID`, `/validation/score/row.csv?x=ID`, `/validation/score/macro_poses.tsv?x=ID` or the named evidence files through the local socket. Downstream timing is in ns. The archived scene describes the submitted macro proposal and the TSV records OpenROAD’s actual applied coordinates.

Use `cmd 'command={"cmd":"continue"}'` to release a gate. At GP end, `run` requests additional iterations. `finish` lets the flow complete. The final flow QoR is in the synthesis result and the placement report in the workdir.
