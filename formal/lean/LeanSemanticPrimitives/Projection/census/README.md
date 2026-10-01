# CORE-ET operator census — what the numbers are, and what they are not

Phase 3 of the Futamura plan is driven by a census of which LGraph operators the
CORE-ET certificates actually use.  The numbers were previously only in terminal
output; this directory makes them reproducible.

## Reproduce

    python3 formal/lean/LeanSemanticPrimitives/Projection/census/coreet_operator_census.py

Exit status is 0 when every operator in the census has an implementation.
Options: `--generated <dir>` and `--interpreter <HardwareInterpreter.lean>`.

## Inputs and filter

    generated dir  /mada/users/czeng14/projects/livehd-new/generated/core-et
                   one subdirectory per module, each holding
                   <mod>/lean/<mod>_Lgraph.lean
    population     <generated>/coreet_census.tsv, rows with verdict == READY
    operators      from the CERTIFICATE node table only -- the text of
                   `def <mod>_nodesTree : BT NodeCert := ...` up to the next
                   `def` -- matching `op := LGraphOp.<Op_X>`

The fast model in the same file is deliberately NOT scanned: it is not what
`interpretDesign` runs.

The IMPLEMENTED set is not hardcoded.  It is read back out of
`Projection/HardwareInterpreter.lean`, from the operator-code constants
`applyOp` dispatches on, so this script cannot silently drift from the
interpreter it is reporting on.

## Result, as of this commit

    READY modules      65
    distinct operators 17
    implemented        17 / 17, uncovered NONE
    STATIC REACHABILITY 65 / 65, unreachable NONE

## WHAT "65/65" DOES NOT MEAN

It is **static operator reachability**, not execution.

* It says: no READY module contains an LGraph operator that `I_hw` lacks.
* It does **not** say that any module was projected, simulated, or compared
  against the reference semantics.  Nothing in this directory runs a design.
* A module counted "reachable" may still fail to project or run for reasons
  this census cannot see -- fuel, memory operators, certificate size, or any
  guard `SupportedByProjection` will add in Phase 4.

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
