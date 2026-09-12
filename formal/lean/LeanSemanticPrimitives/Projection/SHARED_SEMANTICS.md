# Shared semantic contract — provenance and pin

Milestone 0 of `SIMULATOR_PLAN.md` requires this branch to use the *same*
`DesignCert` / runtime / `interpretDesign` as the other executable tracks, not
a copy that has drifted.  A copied-and-modified `interpretDesign` would make the
Milestone 5 equivalence theorem meaningless.

## Pin

Imported verbatim from `livehd-new` at

    f82056dbb84174bb4c4fd9fd3c6099efd58e8a23   (2026-09-11)

**These files are not modified in this branch.**  That is the whole point of the
pin, and it is checkable:

    NEW=<livehd-new>/formal/lean/LeanSemanticPrimitives
    OLD=<this clone>/formal/lean/LeanSemanticPrimitives
    for f in Translation/LGraphModel.lean Translation/GraphRefine.lean \
             Translation/OpBridge.lean Compiler/DesignCert.lean \
             Compiler/Runtime.lean Compiler/DesignCertWF.lean \
             Compiler/DesignSemantics.lean SemanticPrimitives.lean; do
      cmp -s "$NEW/$f" "$OLD/$f" && echo "ok   $f" || echo "DRIFT $f"
    done

If a file must change, change it in `livehd-new` and re-pin here; do not edit
the copy.

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
