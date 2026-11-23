# Live floorplan GUI and agent socket

## Connect to the planning gate

Start the usual `lhd synth chunk init` recipe with `--gui`, or open an existing project with `lhd synth chunk update --workdir W --gui`. For socket-only use, add `--gui-no-open`. Once the gate is open, issue successive edits in that session. From the repository root, use the same workdir for the client.

```sh
gui() { python3 tools/lhd_gui.py --workdir W "$@"; }
gui wait state=gate phase=fp
gui export what=chunks
```

For a live edit, use the workdir client directly. A click or Shift-click supplies the current members.
Use `gui connected kind=color limit=2 add_to_selection=true` to add the two most connected colors.
`gui split home=A new_id=B`, `gui transfer home=A to=B`, `gui fuse home=A other=B` and
`gui reshape home=B aspect=1.5` apply and wait for the planner. Add `apply=false` for a preview.
Finish the plan to switch the same browser tab to placement. Run `gui macro 'ids=["B"]'` for one home.

At the worker stage, `gui selection` returns clicked objects and contacts. `gui find home=B kind=macro match=csr`
lists named memories and their coordinates. `gui edit align=row side=right gap=3000` edits the selected
objects immediately. Add `fix=x|y|xy` for held axes. Use `gui undo` to restore the full previous main state.

```sh
gui trial iterations=100 metric=hpwl keep_if=checkpoint \
  'left={"align":"row","side":"left","gap":3000}' \
  'right={"align":"row","side":"right","gap":3000}'
```

The result includes completed iterations and deltas against the starting checkpoint and an equally advanced
control. Use `keep_if=control` for a comparison against that control. For manual inspection, omit
`keep_if`, then `gui view trial=ID branch=right`, `gui compare trial=ID wait=true` and
`gui keep trial=ID branch=right`. Use `metric=wns` for a packet with constrained nets at export.
Its estimate applies local wire-delay changes to STA seed slacks. Moves preserve the full legal handoff
when accepted, and a stale fork cannot replace an intervening main edit.

Use `gui overview` for the accepted, selected and best comparable assembled pin HPWL and estimated WNS. Run `gui next objective=hpwl|wns|density` to inspect each chunk's priority and reason. Supply `hypothesis="..."` with the trial. A density candidate such as `'sparse={"density":0.72}'` changes that worker's target. An IO candidate such as `'slide={"io":{"net":17,"xy":[1200,2400]}}'` advances every incident chunk and its matched control. Add `stage=final` to fork a finalized handoff. `gui history query=CSR`, `gui finding trial=ID branch=NAME text="..."` and `gui replay trial=ID branch=NAME` use the persistent experiment log.

Use `gui place-design` to finish outstanding chunks locally, validate ownership and assemble their exact positions. The equivalent headless commands are `lhd synth chunk final --workdir W` and `lhd synth chunk assemble --workdir W`. Use `--workers 1` on `chunk init --gui` or `chunk gui` when only one placement worker fits. [Scoring and experiment semantics](../../../pass/place/README.md#live-search-on-isolated-workers)

The client reads the Unix socket path from `W/synth/place/gui_agent.json`. The chunk listing gives physical IDs, boxes in DBU, estimated worker bytes, frame and intent revision. Read members, peers or pins for a selected ID. Each list supports `offset` and `limit`.

```sh
gui export what=chunk_members chunk="$id" offset=0 limit=32
gui export what=chunk_peers chunk="$id"
gui export what=chunk_pins chunk="$id"
```

## Iterate from final placement back to the floorplan

On the local chunk socket, `place-design` runs macro planning for homes with two or more
macro members and then final placement. Use `move-io id=HOME net=N measured=final` to project
the measured pin centroid onto the boundary. The response identifies the homes to recompute.
Use `measured=macro` to take feedback from an earlier stage.

A macro or final `trial` candidate can supply `centroids`, an array of placement hint records.
Use the physical names returned by `scene`. An accepted candidate replaces that home's records.
Absolute positions use die coordinates in microns. Nested and relative references use ids within
the home. A matched control inherits the accepted records.

Run `pcrs clock_port=PORT` on the local socket to measure the assembled final placement with
LegRoute and write a clock checkpoint. The reply includes the revision, routing residual, clock
report and infeasible networks. The live search uses the placement engine’s per-net feedback budget (2,000 queue pops). Set
`legroute_pops=100000` for a deeper search. Trials at the same budget and revision reuse the result.
Use `replan guidance='{"geometry":[{"reshape":"0:0","aspect_ratio":1.25}]}'` to replace
floorplan editing intent. The native producer reuses mapping caches and the coordinator picks up
the resulting packet generation. An empty guidance object clears an unsuccessful floorplan edit.

## Edit the live assignment

In the browser, select a chunk under **Floorplan chunks**. **Physical homes** has member selection, transfer, split and fuse. **Move a physical chunk** has width/height and shape preview. A reshape moves a complete shared edge on the placement grid and adjusts the homes along it. The planner checks space, aspect locks and macro clearances. A chunk occupying the entire core needs a split before it has a shared edge to move.

The socket uses `chunk_edit`. These example operations use A and B as existing chunk IDs, C as a new ID, and ram0 as a member of A. Substitute IDs returned by the live gate. Copy the member's `macro` boolean along with its ID (`false` for a color).

| Edit | Field | Example operation |
|---|---|---|
| Move members into B | `topology` | `{"transfer":"A","to":"B","members":[{"id":"ram0","macro":true}]}` |
| Split members into C | `topology` | `{"split":"A","new_id":"C","members":[{"id":"ram0","macro":true}]}` |
| Join B into A, keeping A | `topology` | `{"fuse":["A","B"]}` |
| Change A's width / height | `geometry` | `{"reshape":"A","aspect_ratio":1.5}` |

Preview is the default. To try a reshape, substitute the actual ID in the operation. Set the shell variables `frame` and `revision` from the same listing's `frame` and `intent_revision`.

```sh
op='{"reshape":"A","aspect_ratio":1.5}'
expect=$(printf '{"frame":%s,"intent":%s}' "$frame" "$revision")
gui chunk_edit field=geometry "op=$op" "expect=$expect"
```

Use the returned `sequence` as `seq`. Poll `gui command_status sequence="$seq"` while the status is `queued`. For a preview, an `applied` receipt means the proposal is ready. Read `gui export what=chunks view=preview` to inspect the boxes, or query `chunk_members` in the preview to check a transfer. To accept, repeat `gui chunk_edit field=geometry "op=$op" "expect=$expect" apply=true` and poll its new sequence. A rejection gives the reason. If the expected revision has changed, read the live plan and preview against the new revision. After a successful apply, connected browsers and agents see the new scene at the same gate. Undo the last operation in the chosen field with `gui chunk_edit field=geometry op=undo apply=true`.

Finish the accepted plan with `gui cmd 'command={"cmd":"finish"}'`. This publishes the worker packets and opens the chunk placement controls. Continue with the [macro and final worker commands](lhd.md#pre-cts-chunk-placement).

## Other placement gates

For a whole-design `lhd synth --gui` session, use `hello`, `state` and `help` to discover phase and capabilities. `selection` reads the user's selection and `describe name=INSTANCE` inspects an object. `screenshot out=FILE.png` captures the displayed placement. Use `export what=floorplan encoding=json_text` at the chunk gate for the plan and its guidance, or `export what=macros` at a macro-pose frame. Preserve the original metadata when saving a replay.

Use the [macro-pose exploration workflow](SKILL.md#explore-macro-poses-in-whole-design-placement) for `macro_scan`, `whatif` and `evaluate`. The separate `macro_move` command can restart whole-design placement to impose a fixed pose. Chunk membership and shape edits use the in-session `chunk_edit` command.

---

# Whole-design placement socket reference

`lhd synth --gui` opens a live placement session. Besides the browser page, it
serves a **local Unix-domain socket (mode 0600)** that an agent or script can
use to inspect, try and apply placement and floorplan edits. The edits it
saves become collaterals: macro poses, placement hints and flags. You do not
need the browser or the token. Only the user who runs `lhd` can open the
socket.

## Start a session

```sh
lhd synth <design files> --top T --workdir W \
  --set synth.place=true --gui --set synth.place.gui.pause=true
```

stderr prints the socket path:

```
lhd synth --gui agent socket (local, mode 0600): /…/lhd-gui-<pid>.sock   (tools/lhd_gui.py --workdir W help)
```

The path and pid are also written to `W/synth/place/gui_agent.json`.
`synth.place.gui.pause=true` holds the flow at the first gate (before global
placement) so you can work on a stable frame. The process stays up after the
flow ends, until it receives `cmd command={"cmd":"finish"}`.

## Talk to it

`tools/lhd_gui.py` is the client (stdlib only). It finds the socket with
`--workdir W`, `--socket PATH` (the stderr line can be pasted as is) or
`LHD_GUI_SOCKET`:

```sh
tools/lhd_gui.py --workdir W help        # overview + full command reference
tools/lhd_gui.py --workdir W hello       # invocation, files, capabilities, options, state
tools/lhd_gui.py --workdir W state       # phase, gate, iteration, WNS/TNS, checkpoints
tools/lhd_gui.py --workdir W critical n=20
tools/lhd_gui.py --workdir W '{"cmd":"whatif","candidates":[{"cmd":"move","nodes":["ram0"],"dx":[2000],"dy":[0]}]}'
```

Write arguments as `key=value`; a value that parses as JSON is sent as JSON.
To send nested payloads verbatim, give the whole request as a JSON object. The
reply is JSON, and the client exits with status 1 when the reply has
`"ok": false`. Underneath, each call is `POST /agent` with a body
`{"cmd": ..., ...}`.

The client adds a few helpers of its own:
- `wait state=gate|phase=..|gate=.. timeout=S` polls `state`.
- `macros` prints every macro's pose, size and orientation, plus the core outline (`export what=macros` underneath).
- `screenshot out=FILE` saves a PNG.
- `--retry S` rides out a restart.

Always read `help` first. Its `overview` groups the commands and gives the
usual loop. Its `commands` entry documents every argument and is the
authoritative reference for the running build.

## Command groups (from `help.overview`)

| Group | Commands | Use |
|---|---|---|
| start | `hello`, `state`, `summary` | What runs, what it can do, where the flow is |
| inspect | `find`, `describe`, `selection`, `critical`, `geometry`, `maps`, `clusters`, `signature`, `leverage`, `evidence` | Cells, macros, critical paths, congestion maps, channels and dead space |
| paint | `highlight`, `highlight_names`, `select`, `clear_highlight`, `highlights` | Show findings in the page; the user's clicks come back through `selection` |
| edit | `move`, `batch`, `macro_move`, `macro_scan`, `pin`, `unpin`, `relax`, `legalize`, `evaluate`, `command_status`, `cmd` | Change the main placement (DBU); `macro_move` places an already-placed macro (restart); `cmd` reaches any page command (run, pause, run_until, stage, area, restart, fork, screenshot, finish, ...) |
| explore | `whatif`, `explore`, `explore_begin`, `explore_wait`, `explorations`, `compare`, `similarity`, `explore_discard`, `score_config` | Test candidates without touching main: tiers T0 (geometry) to T4 (routed); compare at a matched baseline |
| clock | `clock`, `clock_edit` | Inspect the native clock tree and stage decisions for its rebuild |
| centroids | `centroids`, `centroid_edit` | Group cells, flops, memories and macros around a point; edit the records the placement reads |
| collateral | `export`, `intent_preview`, `intent_apply` | Save the result |

## Loop

1. `hello`, then `state` until the flow sits at a gate (`fp`, `macro`,
   `macro_legalize`, `final`, `legalize`, `edit`). To advance it, use
   `cmd command={"cmd":"run_until","gate":"..."}`.
2. Inspect with `critical`, `geometry`, `maps` and `describe`. Paint what
   you find so the user can see it.
3. Screen candidates cheaply with `whatif` (a batch of moves at one gate,
   rolled back) or `explore tier=T0|T1`. Spend T2–T4 only on the survivors,
   and `compare` each one against the reference. Pass `expect={"frame":N}`
   so that an edit made on a stale frame is rejected rather than applied.
4. Apply the winner with `move` or `batch` (or `macro_move` for placed macros), then `evaluate`. Use
   `command_status` to check that the move was applied and to read its
   resolved poses.
5. Export the collaterals:
   - `export what=macros format=metadata|dbu|def|placereasoner`: macro poses.
     `metadata` produces `livehd.placement.state` records for the design
     bundle beside the RTL.
   - `intent_preview` / `intent_apply` with records, then `export
     what=intent`: `livehd.placement.hints` relationships (alignment, gap,
     pin facing) that can adapt to a different core.
   - `export what=relationships`: the moves you made, written as
     relationships.
   - `export what=flags`: the `--set synth.place.policy.*` values you
     changed, for the clean rerun.

A clean `lhd synth` run, without `--gui`, reads the saved metadata, which
makes the agent's floorplan reproducible. [SKILL.md](SKILL.md) covers how to
compare candidates fairly (same gate, seed, scope and estimator; fidelity
labels).

## Moving macros: what each command can do

| Situation | Command | Cost |
|---|---|---|
| Macro still movable at this gate (`describe` shows `movable: true`, usually at `fp`/`macro`) | `move`, `batch`, `pin` | Applied in place; the solver keeps going |
| Macro already placed (`kind: fixed`, e.g. at `final`/`legalize`/`edit`) | `macro_move` | Restarts the flow and replays it to the latest gate with the macro at that pose |
| Test a change without touching main | `explore` / `whatif` | T2 runs `budget.iterations` of bounded relaxation on a private copy |

- `move` refuses a placed macro and tells you to use `macro_move`.
- `macro_move` takes `nodes`, `x`, `y` (DBU, lower-left), one value or one per node.
- It also takes an optional `orientation` (`N`, `S`, `FN`, `FS`, one or one per node). That works for synthetic memory proxies only. Real LEF macros still need the matching `SYMMETRY`.
- `describe` → `allowed_orientations` lists what the solver itself may choose. A fixed node reports only `N` even when `macro_move` can flip it.
- Exploration patches (`move`/`pin`/`unpin`) only accept nodes that are movable at the chosen gate. A placed macro cannot be moved inside a T2 fork today, so use `macro_move` plus a restart for those. Plan geometry experiments at a gate where the macros you want to move are still movable when you can.
- Restarts keep every `macro_move` pose because the poses are replayed from the action trace. The exception is `cmd command={"cmd":"restart","checkpoint":-1}` (from the start), which **drops all edits**. To try a flag on the current layout, restart from a recorded checkpoint index (`state.cps`) with `"set":{...}`. Afterwards check that the poses survived, by comparing `macros` with what you sent.

## The clock tree

With `--set synth.place.pcrs.clock=true` the flow builds a clock tree after placement optimization. Every scene published after that carries it; `state.clock.nets` is zero until then. A session that ran to `state=finished` still answers.

```sh
gui clock                                         # per clock root: endpoints, buffers by master, depth range, wire, insertion, skew, worst stages
gui clock what=nets sort=slew limit=20            # one row per clock net; sort by slew, radius, skew, load, fanout, wire or delay
gui clock what=sinks order=late limit=20          # endpoints by estimated arrival (or order=early)
gui clock what=pair a=launch_reg b=capture_reg    # shared stage, private stages and skew of two endpoints
gui clock what=map metric=skew                    # die grid of local skew (also arrival, sinks, slew); peak_um is the worst bin
gui screenshot out=clk.png layers=comb,regs,macros,clock
```

How to read it:

- **Wire lengths are measured at the frame you are looking at.** Delays, slews and arrivals are the estimates in `pcrs_clock.json`, made at the positions the tree was planned at. `handoff` names that file; with no handoff the reply says "topology only" and those fields are null.
- `depth.min` below `depth.max` means endpoints sit behind different numbers of stages: skew by construction. `private_stages` in `pair` says where two endpoints part.
- A stage whose `far_slew_ps` is close to the library limit is slow and sensitive. Check `masters`: a tree of only the smallest buffer is usually under-driven. `synth.place.pcrs.max_slew_ps` sets a transition target for the stages the planner inserts.
- `skew_ps` in `pair` is b minus a. For a path from a to b, positive helps setup and hurts hold.
- The global skew compares flops that may never exchange data. `map metric=skew` shows the spread among neighbours, which is what short paths see. Take a failing path from `critical`, then ask `pair` about its two flops.

Decisions are staged, then applied by a restart. Nothing patches a built tree: the flow rebuilds it with its own electrical and timing checks.

```sh
gui clock_edit op=drive leaf=__pcrs_clk_1cd2... step=2      # absolute drive step of one leaf (leaf_schedule)
gui clock_edit op=arrival 'pins=["capture_reg:CLK"]' wanted_ps=-10   # arrival_targets; needs balance
gui clock_edit op=option key=balance value=true             # also leaf_fanout, leaf_radius_um, max_slew_ps, source_fork, skew_rounds, ...
gui clock_edit op=show                                      # what is staged (state.clock.staged counts it)
gui clock_edit op=apply                                     # writes the decision files and restarts with --set
```

`apply` restarts from the start unless you pass `checkpoint` (an index of `state.cps`). The restart stops at the same gates as the first run, so continue through them, wait for the tree, and read `clock` again. A staged `drive` on a leaf replaces the entry the tree was built with for that leaf; other leaves keep theirs. Loads the handoff does not name (retained on the source, driven by a gate) cannot be scheduled, and the reply says so. Compare two trees only inside one session and build, and judge a decision by setup and hold over the whole design, not by the clock numbers alone.

## Centroids

A centroid is a point that pulls its members during placement (kind `centroid` of `livehd.placement.hints`; the page draws it as a circle with a cross, layer `centroids`). Create one from instance names or from the page selection, give it a position (`free`, coordinates, or relative to another centroid), a layout for its members (`floating`, `row`, `column`, `array`: slots around the point before final legalization) and a strength. A centroid can hold other centroids. The records are collateral: the same file a batch run reads, written under the hints record `gui-centroids`, and a clean `lhd synth` reads them back.

```
gui centroids                                                # authored records, and the groups the flow compiled (PCRS leaf groups are pcrs:N)
gui centroid_edit op=create id=regs 'members=["u/f0","u/f1","u/f2"]' layout=row strength=0.2
gui centroid_edit op=create members=selected position=[120,80]    # from the page selection
gui centroid_edit op=set id=regs 'layout={"kind":"array","columns":2,"gap_x_um":0.5}' strength_scale=2
gui centroid_edit op=move to=regs 'members=["u/f9"]'         # a member lives in one centroid
gui centroid_edit op=adopt id=pcrs:1234                      # edit a leaf group PCRS formed: it becomes authored, with its members
gui centroid_edit op=set id=regs apply=false ...             # preview without applying
```

An edit is applied through `intent_apply` at a placement gate (so `expect` and the intent revision protect it), and the hint report lists, per centroid, the members bound, those that did not resolve, the strength, and the largest distance of a member from its layout slot. Dragging a centroid in the page writes its `at` position. The pull is weak by default; raise `strength` (or scale it) on the centroids that matter and judge the result with `evaluate`. Records apply to plain placement; in the macro stages they are not read. A `clock` field is read by PCRS: `arrival_ps` (wanted arrival of the members' clock pins, with `synth.place.pcrs.balance=true`) and `drive_steps` (absolute drive step of their leaf); `drive` (a master) is stored only. They are the decisions of `arrival_targets` and `leaf_schedule`, per centroid.

## Reading results after a restart

- A submit returns `queued` at once, and `state` may still show the previous gate for a moment. Record `state.epoch` before submitting, then poll until the epoch changes **and** `state=gate` again. A plain `wait state=gate` right after a submit can return the stale gate.
- After every `macro_move`, read the client `macros` listing and diff it against the request, including orientation. The packer moves each macro to the nearest legal spot: halo against cells, `max(channel, 2*halo)` against other macros, routing tracks, rows. A drop that is too tight silently becomes a different layout. Large shifts mean the experiment did not test what you intended.
- `geometry` → `macros[].pin_access_proxy` gives, per side, `pins` and `connected_pins`. Unconnected pins cost nothing, so decide by connected pins.

## Efficient floorplan search

1. **Measure noise first.** Run the unchanged layout twice, and once more at the end of each batch. Treat deltas inside that spread as ties. Placement-RC timing on a design far from closure moves little; HPWL is often the more sensitive signal.
2. **Never compare across sessions or builds.** The core outline, the baseline and the defaults can change between a relaunch and a rebuild. Re-measure the reference in the session you are testing.
3. **Coarse before fine.** Settle the topology first, with large moves that change one factor each:
   - which edge or corner each group sits on, versus the centre;
   - grouping and array shape (e.g. 2×8 vs 4×4 vs 8×2);
   - orientation, including mirroring the whole layout;
   - the order of blocks along an edge;
   - whether a group is placed at all or left to the placer.

   Fine nudges of 10–20 µm come last; near a good layout they are usually noise.
4. **Use pin sides.** Group same-master macros into regular arrays and put the side with no connected pins against the core edge. Flip a macro if its pin-free side points inward. Keep pinned faces away from the edge and away from a neighbour's pinned face, or leave a channel between them sized for the connected pins.
5. **Keep the standard-cell region contiguous.** Macro groups in the core interior cut it up and usually cost wirelength and slack.
6. **Generate layouts from the live core.** Read the core outline (client `macros` → `core`) before each submit. Place blocks relative to edges and to each other, with a small margin, snapping to the site width and row height. Hard-coded coordinates break when the core size changes.
7. **Prefer forks to restarts for small changes.** For an edit the solver can apply at the current gate, run a T2 exploration with 100–200 iterations from the current frame and `compare` it with the reference. Restart from scratch only for large topology changes, or for moves the fork cannot express (placed macros, flags).
8. **Script batches.** Script each experiment as submit, then poll the epoch, then read `state`, `macros` and a `screenshot`. Log one JSON row per experiment, and look at screenshots of outliers before trusting their numbers.

## Notes

- Highlights and the selection are session state keyed by node name. They
  survive phase changes, and the page and the agent see the same thing.
- `critical` and `evaluate` are answered by the flow thread at its next poll
  or gate. While GP runs, a call can take up to one poll
  (`synth.place.gui.poll` iterations).
- Code: `pass/place/gui/gui_agent.cpp` (commands and the `help` text),
  `gui_server.cpp` (socket), `gui_session.cpp` (session state). Tests: `gui_session_test`,
  `lhd/tests/lhd_gui_register_smoke.py`, `lhd_gui_explore_smoke.py`.
