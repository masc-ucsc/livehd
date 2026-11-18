# Live floorplan GUI and agent socket

## Connect to the planning gate

Start the usual `lhd synth chunk init` recipe with `--gui`, or open an existing project with `lhd synth chunk update --workdir W --gui`. For socket-only use, add `--gui-no-open`. Once the gate is open, issue successive edits in that session. From the repository root, use the same workdir for the client.

```sh
gui() { python3 tools/lhd_gui.py --workdir W "$@"; }
gui wait state=gate phase=fp
gui export what=chunks
```

The client reads the Unix socket path from `W/synth/place/gui_agent.json`. The chunk listing gives physical IDs, boxes in DBU, estimated worker bytes, frame and intent revision. Read members, peers or pins for a selected ID. Each list supports `offset` and `limit`.

```sh
gui export what=chunk_members chunk="$id" offset=0 limit=32
gui export what=chunk_peers chunk="$id"
gui export what=chunk_pins chunk="$id"
```

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
