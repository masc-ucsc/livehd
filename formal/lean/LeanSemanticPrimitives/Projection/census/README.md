# CORE-ET operator census — what the numbers are, and what they are not

Phase 3 of the Futamura plan is driven by a census of which LGraph operators the
CORE-ET certificates actually use.  The numbers were previously only in terminal
output; this directory makes them reproducible.

## Reproduce

    python3 formal/lean/LeanSemanticPrimitives/Projection/census/coreet_operator_census.py

Exit status is 0 when every operator in the census has an implementation AND
no fixed-arity operator is given the wrong operand count.
Options: `--generated <dir>` and `--interpreter <HardwareInterpreter.lean>`.

## Inputs and filter

    generated dir  /mada/users/czeng14/projects/livehd-new/generated/core-et
                   one subdirectory per module, each holding
                   <mod>/lean/<mod>_Lgraph.lean
    population     <generated>/coreet_census.tsv, rows with verdict == READY
    arity map      parsed from `RequiredArity` in HardwareInterpreter.lean,
                   NOT a second table here; the script exits if it is gone
    operators      from the CERTIFICATE node table only -- the text of
                   `def <mod>_nodesTree : BT NodeCert := ...` up to the next
                   `def` -- matching `op := LGraphOp.<Op_X>`

The fast model in the same file is deliberately NOT scanned: it is not what
`interpretDesign` runs.

The IMPLEMENTED set is not hardcoded.  It is read back out of
`Projection/HardwareInterpreter.lean`, from the operator-code constants
`applyOp` dispatches on, so this script cannot silently drift from the
interpreter it is reporting on.

## The arity half

Nine of the seventeen supported operators are **fixed-arity**: the pinned
`eval_op` matches on an exact operand shape and answers `mk_bv w 0` at any
other length, while `applyOp` reads positionally — it errors below the arity
and ignores surplus operands above it.  `SupportedByProjection.arities` is the
hypothesis that rules the mismatch out.

The mapping is **not** duplicated here.  It is parsed out of `RequiredArity` in
`Projection/HardwareInterpreter.lean`, its single source of truth.  If that
definition is renamed, moved, or loses its `some k` cases, the script **exits**
rather than silently reporting zero violations out of zero constraints.

## Results, as of this commit

Three results, reported independently because they discharge different fields.

    READY modules        65
    certificate nodes    131,291
    distinct operators   17

    RESULT 1  operator coverage  (SupportedByProjection.ops)
      implemented            17 / 17, uncovered NONE
      operator reachability  65 / 65, unreachable NONE

    RESULT 2  arity coverage     (SupportedByProjection.arities)
      fixed-arity operators  9 (from RequiredArity)
      Op_GetMask  63949  requires 2  observed [2]  violations 0
      Op_SRA      12378  requires 2  observed [2]  violations 0
      Op_MuxBool   2375  requires 3  observed [3]  violations 0
      Op_Sext      2043  requires 2  observed [2]  violations 0
      Op_Not       1848  requires 1  observed [1]  violations 0
      Op_SLT        300  requires 2  observed [2]  violations 0
      Op_ULT         86  requires 2  observed [2]  violations 0
      Op_UGT         18  requires 2  observed [2]  violations 0
      Op_SGT         10  requires 2  observed [2]  violations 0
      arity violations       0
      arity-clean modules    65 / 65

    RESULT 3  combined operator-shape reachability
      every node has an implemented operator AND the operand count that
      operator requires                                65 / 65

## WHAT "65/65" DOES NOT MEAN

It is **static operator-shape reachability**, not execution, and not
`SupportedByProjection`.

* It measures **two of five** fields — `ops` and `arities`.  Nothing here
  measures `wf` (dependency ordering and slot ranges), `memFree`, `sources`
  (the source forms), or `flopClocks`.
* It says: no READY module contains an LGraph operator that `I_hw` lacks, and
  none gives a fixed-arity operator the wrong operand count.
* It does **not** say that any module was projected, simulated, or compared
  against the reference semantics.  Nothing in this directory runs a design.
* A module counted "reachable" may still fail to project or run for reasons
  this census cannot see — fuel, memory operators, certificate size, or the
  three support fields it does not measure.
* It is evidence about **the exporter**, not a theorem.  It says no emitted
  certificate violates the condition today, not that none can.

Actually running designs is Phase 6, and its first external milestone is 30
CVA6 blocks through the projected simulator — to be claimed only from recorded
execution and comparison against the reference semantics, never from
certificate emission, projection, or a census like this one.

## Caveats visible in the data

* Two generated modules (`minion_dcache_texsend`, `null_vpu`) have an EMPTY
  certificate node table (`BT.lf`).  They are not READY, so the filter already
  excludes them; they would contribute no operators in any case.
* Five further modules carry a non-empty certificate without being READY.  The
  filter excludes them.  Including them would add `Op_Mult` (4 nodes, 1 module)
  and make the totals 70 modules / 18 operators.
* The plan text once said "20 distinct operators over 71 proven certificates".
  That matches neither reading of the data on disk and was corrected when the
  census was first re-run.
