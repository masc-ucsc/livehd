# Shared semantic contract — provenance and pin

Milestone 0 of `SIMULATOR_PLAN.md` requires this branch to use the *same*
`DesignCert` / runtime / `interpretDesign` as the other executable tracks, not
a copy that has drifted.  A copied-and-modified `interpretDesign` would make the
Milestone 5 equivalence theorem meaningless.

## Pin

Imported verbatim from `livehd-d2-ir-semantics` at

    90583be1349acbd25f740b176a406aa8bbe1251c   (clock provenance, multi-clock Phase B)

RE-PINNED from `livehd-new` `f82056dbb84174bb4c4fd9fd3c6099efd58e8a23`
(2026-09-11).  The multi-clock certificate semantics — `ClockDesc`, a clock
ordinal on every state element, `ClockEdges` and an edge-aware `interpretDesign`
— was made in d2's worktree and is the user's to port; `DIRECTION2_RESULTS.md`
§10 "Porting to the other branches" is the authoritative list.  Three of the
eight pinned files changed; the other five are byte-identical across both
revisions, so the pin moved rather than split.

**These files are still not modified in this branch.**  That is the whole point
of the pin, and it is checkable:

    D2=/mada/users/czeng14/projects/livehd-d2-ir-semantics
    OLD=<this clone>/formal/lean/LeanSemanticPrimitives
    for f in Translation/LGraphModel.lean Translation/GraphRefine.lean \
             Translation/OpBridge.lean Compiler/DesignCert.lean \
             Compiler/Runtime.lean Compiler/DesignCertWF.lean \
             Compiler/DesignSemantics.lean SemanticPrimitives.lean; do
      (cd "$D2" && git show 90583be13:formal/lean/LeanSemanticPrimitives/$f) \
        | cmp -s - "$OLD/$f" && echo "ok   $f" || echo "DRIFT $f"
    done

If a file must change, change it upstream and re-pin here; do not edit the copy.

## What the port did NOT take

d2's §10 list also covers its C++ exporter, its B1+B2 verified-compiler copies,
its direct simulator (`directStep`, `Tick`, `runDirect`) and the checker
additions (`noClocks`, `flopClockOutOfRange`, `asyncFlagMismatch`,
`edgesMismatch`, and the `DesignSemWF`/`RuntimeSemWF` fields behind them).
None of those is imported: this branch consumes the shared semantic definitions
only, and `Compiler/DesignCertWF.lean` is byte-identical at both revisions.

Because the checker is not here, the range facts it would discharge are carried
as hypotheses instead — `interpretDesign_allEdges` takes them explicitly, and
`Hw.RuntimeSized` is where `I_hw`'s own unchecked reads record them.

## What was imported

| file | role |
| --- | --- |
| `SemanticPrimitives.lean` | already identical before the pin |
| `Translation/LGraphModel.lean` | `LGraphOp`, `BV`, `eval_op`, `CertVal`, `GraphCert`, `NodeSemantics` |
| `Translation/GraphRefine.lean` | `evalGraphG` and its refinement lemmas |
| `Translation/OpBridge.lean` | `bvenc` and the per-operator bridges (needs Mathlib; not in the fast build) |
| `Compiler/DesignCert.lean` | the design certificate |
| `Compiler/Runtime.lean` | `RuntimeInput` / `RuntimeState` / `RuntimeResult` |
| `Compiler/DesignCertWF.lean` | well-formedness |
| `Compiler/DesignSemantics.lean` | **`interpretDesign`** — the reference one-cycle semantics |

## What was deliberately NOT imported

`CompileDesign`, `CompileGraph`, `CompileOp`, `ResidualIR`, `ResidualSemantics`.

The plan is explicit: this track must stay independent of the verified compiler
it will later be compared against.  Importing `compileDesign` would make the
Milestone 5 comparison circular.

## Note on an earlier decision

This supersedes the earlier choice to rebuild the design semantics from scratch
in this clone.  That choice optimised for branch independence; it would have
produced a second, drifting `interpretDesign` and made cross-track equivalence a
statement about two different semantics rather than one.
