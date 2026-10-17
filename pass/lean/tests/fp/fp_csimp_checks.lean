/-
# The five active `@[csimp]` registrations, audited by exact name

Count the declaration-attached ones, not `grep '@[csimp]'` hits: the attribute
is also written inside doc comments and `--` notes, which are prose.  The
active registrations are exactly

  Projection.PVal.shift_eq_shiftFast          (PartialEvaluator.lean)
  Projection.PEnv.shiftBy_eq_shiftByFast      (PartialEvaluator.lean)
  Projection.ProtoSum.PVal.shift_eq_shiftFast (Proto/PartialEvaluatorSummary.lean)
  Projection.ProtoSum.PEnv.shiftBy_eq_shiftByFast (same)
  Projection.S3Correct.mixDriver_eq_S3        (S3Correct.lean; the compiled
                                               mixDriver is S3, deferred shifting)

Two obligations, kept apart:

  * the KERNEL one -- each equality is a theorem, and its axiom report is the
    evidence that it is proved rather than assumed. That is what this file
    checks.
  * the COMPILED one -- `@[csimp]` is what makes the compiler emit the fast
    body in place of the reference one, so the equality is also load-bearing at
    runtime. That is NOT checked here; it is checked by running the original
    runners, which exercise the emitted code. A passing axiom report says
    nothing about which body the compiler emitted, and a passing runner says
    nothing about whether the equality is proved.
-/

import LeanSemanticPrimitives.Projection.PartialEvaluator
import LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorSummary
import LeanSemanticPrimitives.Projection.S3Correct

#print axioms Projection.PVal.shift_eq_shiftFast
#print axioms Projection.PEnv.shiftBy_eq_shiftByFast
#print axioms Projection.ProtoSum.PVal.shift_eq_shiftFast
#print axioms Projection.ProtoSum.PEnv.shiftBy_eq_shiftByFast
#print axioms Projection.S3Correct.mixDriver_eq_S3

-- The reference and fast bodies agree as FUNCTIONS, which is the statement the
-- attribute rests on; these re-check it at the exact names.
example : @Projection.PVal.shift = @Projection.PVal.shiftFast :=
  Projection.PVal.shift_eq_shiftFast
example : @Projection.PEnv.shiftBy = @Projection.PEnv.shiftByFast :=
  Projection.PEnv.shiftBy_eq_shiftByFast
example : @Projection.ProtoSum.PVal.shift = @Projection.ProtoSum.PVal.shiftFast :=
  Projection.ProtoSum.PVal.shift_eq_shiftFast
example : @Projection.ProtoSum.PEnv.shiftBy = @Projection.ProtoSum.PEnv.shiftByFast :=
  Projection.ProtoSum.PEnv.shiftBy_eq_shiftByFast
example : @Projection.mixDriver = @Projection.S3.mixDriver :=
  Projection.S3Correct.mixDriver_eq_S3
