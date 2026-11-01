# Shared semantic contract — provenance on master

The projection uses the *same* `DesignCert` / runtime / `interpretDesign` as
the other executable tracks, not a copy that has drifted.  A copied-and-modified
`interpretDesign` would make any equivalence result between the tracks
meaningless.

## Base, and what master carries

The FP branch pinned the shared files at the SEMANTIC BASE

    90583be1349acbd25f740b176a406aa8bbe1251c   (livehd-d2-ir-semantics: clock provenance, multi-clock Phase B)

which is present in this repository.  On master the shared files are master's
own; the projection consumes them unchanged.  Against the base:

    Translation/GraphRefine.lean      byte-identical
    Compiler/Runtime.lean             IMPORT-ONLY (identical without one added import; recorded hash)
    Compiler/DesignCertWF.lean        byte-identical
    SemanticPrimitives.lean           byte-identical

    Translation/LGraphModel.lean      MASTER-ADAPTED (operator boundary repairs)
    Translation/OpBridge.lean         MASTER-ADAPTED (bridges re-proved for them)
    Compiler/DesignCert.lean          MASTER-ADAPTED (DCERT3 memory fields; recorded hash)
    Compiler/DesignSemantics.lean     MASTER-ADAPTED (the power-on image; recorded hash)

### `Compiler/DesignCert.lean`

032690762 (D2 audit item C4) corrected the `ClockDesc` docstring: it claimed
ordinal 0 is the reference clock, "the net carrying the most state", which the
exporter does not provide (it numbers domains by sorting on a structural key)
and nothing in the semantics reads. That was a comment-only change.

DCERT3 (decisions #5, #9 and #2, one revision) is a CODE change: `MemoryDesc`
gains `init` (a memory's power-on words, entry 0 first, 0 past the end), and two
RESERVED fields, `commits` (a commit table for a memory on more than one clock)
and `asyncImg` (an async-reset image for a whole-array cell), with the
`MemCommit` record and the helpers `reservedFree` and `initWord`.  Every field
defaults to today's meaning, and the step semantics reads none of them:
`interpretDesign` is unchanged.  Admission (`certWfError`) refuses a
non-default reserved field until a route models it.  The check pins the bytes
with a recorded hash.

### `Compiler/DesignSemantics.lean`

DCERT3 adds one definition, `MemoryDesc.powerOnImg` (word `a` is `init[a]`, 0
past its end): the start image every runner takes a memory from
(`DirectTrace.resetState`, cert_trace's `resetState`).  `interpretDesign`, the
step every theorem is about, is byte-for-byte what it was.  FP's start state
builds the same table as `MemImg.ofInit` (`initMemsOf`).  The check pins the
bytes with a recorded hash.

### `Compiler/Runtime.lean`

95c80e506 added one line, `import LeanSemanticPrimitives.Translation.FastBV`.
`FastBV` proves a `@[csimp]` theorem `f = fFast` for each hot `BV` primitive,
and the compiler uses `fFast` in code compiled after the theorem is in scope.
Importing it here puts it in scope for `Runtime` and for every module that
imports `Runtime`, among them the byte-identical `DesignSemantics`.  No
definition changes, and the substitution is a checked theorem, so every value
FP's theorems talk about is the same.  The check compares the file with the
base after removing exactly that one line, and pins the current bytes with a
recorded hash.

### `Translation/LGraphModel.lean`

Master's `eval_op` / `denote_op` carry three boundary repairs the base did not
have: `Op_SHL` saturates at the RESULT width (`k ≥ w` gives 0), `bv_sra`
saturates at the SOURCE width, and `Op_Sext` takes a strict `n > a.width`
branch.  They exist so the terms EVALUATE at operand amounts that are values
rather than widths (33-bit shift amounts in three CORE-ET designs).  FP inherits
them directly: `ObjectLanguageSemantics` evaluates `.bvSra` / `.bvShl` with
`bv_sra` / `bv_shl_step`.

`bv_shl_step` -- the name the FP branch introduced by its overlay
(`pass/lean/specialization/evidence/bv_shl_step.patch`) so `evalPrim`, `OperatorBridge` and
`HardwareAdequacy` could name the `Op_SHL` fold term -- is defined on master
with MASTER's guarded body.  The patch was not applied: its body is the
unguarded `2 ^ k`, and taking it would delete a proved repair (regression R46).
The patch file is kept as a historical record only.

Commits: 0143ea8cc, f3678b423, 9ca8cf8ed (the repairs), 39f118b05
(`bv_shl_step` with master's body), and the docstring correction that records
the bridge results below.

### `Translation/OpBridge.lean`

The per-operator BitVec bridges, re-proved against the repairs:
`bv_sra_guard_eq` shows the guarded `bv_sra` equals the unguarded formula on
canonical (`bvenc`) operands, the form both bridges apply it to.
`Translation/SraGuard.lean` closes the general case: `bv_sra_guard_eq_any`
proves the same equality on EVERY `BV`, normalised or not, with no hypothesis
(`bv_sint` reads its argument only through `bv_uint`, which already reduces
modulo `2 ^ width`), and `bv_sra_guard_eq_of_any` re-derives the canonical
case from it.  It is a separate file so the pinned `OpBridge.lean` and
`LGraphModel.lean` stay byte-identical; `LGraphModel.lean`'s docstring still
says "still open" for that reason.

## The repairs change no value FP's theorems depend on

`Projection/OperatorBridge.lean` states, over `Op_SHL`'s body AS IT READ
BEFORE the repairs (the inline unguarded expression):

    bv_shl_step_unfolds
    evalOp_SHL_unchanged_by_overlay
    denoteOp_SHL_unchanged_by_overlay
    evalOp_Sext

On the FP branch the first three were `rfl`.  On master they are PROVED, not
`rfl` (906befae5): `shl_step_eq` -- `2^w` divides `u * 2^k` whenever `k ≥ w`,
so the guard changes no value -- and `evalOp_Sext_shape` for the strict `Sext`
branch.  The statements are the pinned ones, unchanged.

## Checking it

    bash pass/lean/specialization/tests/check_overlay.sh [--semantic]

Runnable from any directory.  It checks the three byte-identical files against
the base (taken from THIS repository by default; `BASE_REPO` overrides),
`Runtime.lean` against the base without its one added import, and the four
adapted files and `Runtime.lean` against recorded sha256 values.  `--semantic` also rebuilds
`OperatorBridge` (no Mathlib) and, under lake, `Translation/OpBridge`, so the
bridge theorems above are re-checked rather than trusted.

If one of the three must change, change it deliberately and update this document.
If an adapted file changes, update its recorded hash in the check script in
the same commit, and say why.

## What the projection imports from the rest of the tree

| file | role |
| --- | --- |
| `SemanticPrimitives.lean` | `sem_*` BitVec primitives |
| `Translation/LGraphModel.lean` | `LGraphOp`, `BV`, `eval_op`, `CertVal`, `GraphCert`, `NodeSemantics` |
| `Translation/GraphRefine.lean` | `evalGraphG` and its refinement lemmas |
| `Compiler/DesignCert.lean` | the design certificate |
| `Compiler/Runtime.lean` | `RuntimeInput` / `RuntimeState` / `RuntimeResult`, `ClockEdges` |
| `Compiler/DesignCertWF.lean` | well-formedness |
| `Compiler/DesignSemantics.lean` | **`interpretDesign`** — the reference one-cycle semantics |
| `Compiler/DirectCheck.lean` | the checker's slot kinds (memory route only) |
| `Compiler/CertIOV1` / `CertIOCodec` / `CertIORoute` | the DCERT1 and DCERT3 codecs and the router |
| `Compiler/ResetPolarity` / `RunAdmission` | the drivers' run-admission checks |

`Translation/OpBridge.lean` needs Mathlib and is not imported by any Projection
module; it is pinned for the bridge results above.

## What is deliberately NOT imported

`CompileDesign`, `CompileGraph`, `CompileOp`, `ResidualIR`, `ResidualSemantics`.

This track stays independent of the verified compiler, so that comparing the
two is not circular; importing `compileDesign` would make it so.
(`Compiler/CertIO.lean` imports it for
`runChecked_correct`, which is why the codec was split into `CertIOCodec`.)
