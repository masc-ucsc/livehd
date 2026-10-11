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

`90583be13` is the SEMANTIC BASE, not a tracking target.  This branch is
developed independently of d2: it does not wait for, merge, or re-pin to any
later d2 commit, and it does not modify d2.  The base is named so that what this
branch's semantics IS can be stated exactly.

## Seven files byte-identical, one overlaid

**SEVEN of the eight files are byte-identical to the base.  The eighth is not,
and saying otherwise would be false.**

    Translation/GraphRefine.lean      byte-identical
    Translation/OpBridge.lean         byte-identical
    Compiler/DesignCert.lean          byte-identical
    Compiler/Runtime.lean             byte-identical
    Compiler/DesignCertWF.lean        byte-identical
    Compiler/DesignSemantics.lean     byte-identical
    SemanticPrimitives.lean           byte-identical

    Translation/LGraphModel.lean      base + ONE definitional overlay

The overlay is `Projection/overlays/bv_shl_step.patch`: one new definition,

    def bv_shl_step (w : Nat) (a b : BV) : BV :=
      mk_bv w (bv_uint a * (2 : Int) ^ (bv_uint b).toNat)

and exactly two call-site replacements, in `denote_op`'s and `eval_op`'s
`Op_SHL` cases, where that expression was written inline.  Nothing else in the
file differs.

WHY.  `Op_SHL` was the only operator in the CORE-ET census whose per-step term
had no named helper, so an `evalPrim` case could only re-write the expression --
two copies that a later change to `Op_SHL` would silently desynchronise.  Naming
it gives one source of truth.

IT CHANGES NO MEANING, and that is a theorem rather than a claim.
`Projection/OperatorBridge.lean` proves

    bv_shl_step_unfolds
    evalOp_SHL_unchanged_by_overlay
    denoteOp_SHL_unchanged_by_overlay

each by `rfl`, and each stated over `Op_SHL`'s body AS IT READ BEFORE the
overlay -- the inline expression, with no mention of the new name.  A semantic
change could not satisfy them.  Both functions the overlay touched are covered.

## Checking it

    bash formal/lean/LeanSemanticPrimitives/Projection/overlays/check_overlay.sh

Run from the repo root.  It verifies the seven files against the base, and
verifies the eighth TWO independent ways: against a recorded sha256, and by
regenerating it from the base plus the stored patch.  The hash arm still means
something when the base repository is unavailable, so the record does not depend
on anything outside this repository.

`overlays/bv_shl_step.patch` in THIS repository is the authoritative copy.  An
earlier copy was staged under `automate_workspace/d4-futamura-projection/` while
the factoring was still a proposal; it has context lines where this one is
zero-context, and it is now only a historical artifact.  Nothing in this
repository reads it.

If one of the seven must change, change it at the base and update this document.
If the overlay must grow, it needs a new theorem of the same shape as the three
above, and the check script's recorded hash updated in the same commit.

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

The `bv_shl_step` overlay is the ONLY local change to any of the eight files.
It is a factoring, not a semantics: see "Seven files byte-identical, one
overlaid" above.

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
