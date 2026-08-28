/-
  Gate 0 -- self-application, on a problem small enough to read.

  Two questions, in order:

  (a) Does `mixProgram` -- `mix` written in `L` -- compute what the Lean `mix`
      computes?  If not, nothing about the second projection can be trusted,
      and the bug is in a transcription rather than in the theory.

  (b) Does specializing `mixProgram` to the toy interpreter produce a COMPILER?
      That is the second projection, and it is the thing this branch exists to
      answer.

  Everything here is a `#guard`, so the build is the test.
-/

import LeanSemanticPrimitives.Projection.MixProgram
import LeanSemanticPrimitives.Projection.Demo
import LeanSemanticPrimitives.Projection.BTA

namespace Projection
namespace Gate0

open MixProg

/-! ## (a) `mixProgram` against the Lean specializer -/

#guard mixResolved.toOption.isSome
#guard mixProgram.funs.length == 39

/-- Run the object-level specializer on the toy interpreter and the sample
expression -- the same inputs `Demo.residual2` gave the Lean specializer. -/
def objOut : EvalResult :=
  evalFuel 100000 mixProgram []
    (.call mixProgram.entry [.lit (encAProgram Demo.interpA2P), .lit (encVals [Demo.sample])])

def objProgram : Option Program :=
  match objOut with
  | .value v => decProgram v
  | _        => none

-- it terminates and its output really is an encoded program
#guard objProgram.isSome

def objP : Program := objProgram.getD ⟨[], 0⟩

-- THE AGREEMENT: the object specializer and the Lean specializer produce the
-- same residual program, not merely programs that happen to agree on one input
#guard objP == Demo.residual2P

-- and it computes what the interpreter computed
#guard evalFuel 200 objP [] (.call 0 [.lit Demo.sampleEnv]) == .value (.int 56)

end Gate0
end Projection
