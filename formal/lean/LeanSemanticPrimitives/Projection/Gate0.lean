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
#guard mixProgram.funs.length == 52

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

/-! ## (b) The second projection

`mix(mix, interp)` -- specialize the specializer to the interpreter.  What comes
out should be a COMPILER: a program that takes a source program and emits the
residual program, without an interpreter's dispatch anywhere in it. -/

def A_M : AProgram :=
  match bta mixProgram mixInline [.stat, .dyn] 200 with
  | .ok a    => a
  | .error _ => ⟨[], 0⟩

-- BTA must find the interpreter's ANNOTATED program static and the static
-- arguments dynamic; everything below depends on the divisions it infers
#guard (bta mixProgram mixInline [.stat, .dyn] 200).toOption.isSome

/-- The division `mix`'s term walk gets.  This single row is what decides
whether the second projection does anything: `A` static, `reqs` dynamic, the
DIVISION static, `env` dynamic, the TERM static.  With the term dynamic --
which is what the first version of `MixProgram.lean` produced -- `mixTerm`'s
`switch` residualizes and `mix(mix, interp)` returns `mix`. -/
def mixTermIdx : Nat := (mixS.funs.findIdx? (fun f => f.name == "mixTerm")).getD 0

def mixTermDivision : Div × BT :=
  match A_M.funs[mixTermIdx]? with | some fd => (fd.params, fd.ret) | none => ([], .dyn)

#guard mixTermDivision == ([.stat, .dyn, .stat, .dyn, .stat], .dyn)

def compilerR : Except MixError Program := mixDriver 200000 500 A_M [encAProgram Demo.interpA2P]

#guard compilerR.toOption.isSome

def compilerP : Program := match compilerR with | .ok p => p | .error _ => ⟨[], 0⟩

-- one residual function per (division, subterm) reachable in the interpreter
#guard compilerP.funs.length == 93

/-- Run the compiler on the source program.  Its argument is the SOURCE, and
nothing else -- the interpreter is gone, baked in. -/
def compiled : EvalResult :=
  evalFuel 200000 compilerP [] (.call compilerP.entry [.lit (encVals [Demo.sample])])

def compiledProgram : Option Program :=
  match compiled with | .value v => decProgram v | _ => none

#guard compiledProgram.isSome

-- THE SECOND PROJECTION: what the derived compiler emits is exactly what the
-- specializer produces directly
#guard compiledProgram == some Demo.residual2P

-- and that program still computes what the interpreter computed
#guard evalFuel 200 (compiledProgram.getD ⟨[], 0⟩) [] (.call 0 [.lit Demo.sampleEnv])
         == .value (.int 56)

/-! ## Gate 3 -- the compiler must not still be an interpreter

The test that actually distinguishes a compiler from a specializer that did
nothing: `compilerProgram` must contain no residual dispatch on the annotated
interpreter's TERM TAGS.  If it still walks the interpreter's syntax at run
time, specialization achieved nothing even when its output happens to be right. -/

def caseTags : Term → List Nat
  | .lit _      => []
  | .var _      => []
  | .letIn a b  => caseTags a ++ caseTags b
  | .ite a b c  => caseTags a ++ caseTags b ++ caseTags c
  | .prim _ ts  => caseTagsL ts
  | .ctorT _ ts => caseTagsL ts
  | .caseT s as => caseTags s ++ (as.map Alt.tag) ++ caseTagsAlts as
  | .call _ ts  => caseTagsL ts
where
  caseTagsL : List Term → List Nat
    | []      => []
    | t :: ts => caseTags t ++ caseTagsL ts
  caseTagsAlts : List Alt → List Nat
    | []      => []
    | a :: as => caseTags a.body ++ caseTagsAlts as

def programCaseTags (P : Program) : List Nat :=
  (P.funs.map (fun fd => caseTags fd.body)).flatten

/-- Tags 30-42 are `ATerm`'s: dispatching on one means walking the annotated
interpreter's syntax. -/
def aTermTagCount (P : Program) : Nat :=
  ((programCaseTags P).filter (fun t => tagALit ≤ t && t ≤ tagAProgram)).length

-- the specializer dispatches on the interpreter's syntax; the compiler does not
#guard aTermTagCount mixProgram > 0
#guard aTermTagCount compilerP == 0

end Gate0
end Projection
