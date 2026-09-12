/-
# `DirectVsCompiled` — the two implementations agree where both apply

Direction 2, phase 3's third theorem and phase 6's evaluation oracle.

This is the ONLY module in the Direction 2 chain that mentions the residual
compiler.  `DirectSemantics` must not, or the direct evaluator would just be a
second entry point to B1+B2; the agreement below is a CONSEQUENCE of the two
correctness theorems, not part of either definition.

It is deliberately a short composition:

    directStep D i s = .ok r      gives  r = interpretDesign D i s
    compilesOk D = true           gives  compileAndRun D i s = interpretDesign D i s

so `r = compileAndRun D i s` follows with no node-by-node reasoning.  If this
file ever needed an induction, something upstream would be wrong.
-/
import LeanSemanticPrimitives.Compiler.CompileDesign
import LeanSemanticPrimitives.Compiler.DirectTrace

namespace Compiler
namespace Direct

--------------------------------------------------------------------------------
-- One cycle
--------------------------------------------------------------------------------

/-- **Cross-implementation agreement.**  On every certificate both paths accept,
the direct interpreter and the verified compiler's output are the same value. -/
theorem directStep_eq_compileAndRun (D : DesignCert) (i : RuntimeInput) (s : RuntimeState)
    (r : RuntimeResult) (hd : directStep D i s = .ok r) (hc : compilesOk D = true) :
    r = compileAndRun D i s := by
  rw [compileAndRun_correct D hc i s]
  exact directStep_correct D i s r hd

--------------------------------------------------------------------------------
-- Traces
--------------------------------------------------------------------------------

/-- The compiled trace: iterate `compileAndRun`. -/
def compiledTrace (D : DesignCert) : RuntimeState → List RuntimeInput → TraceResult
  | s, []      => { steps := [], finalState := s }
  | s, i :: is =>
      let r := compileAndRun D i s
      let t := compiledTrace D r.nextState is
      { steps := r :: t.steps, finalState := t.finalState }

theorem compiledTrace_eq_refTrace (D : DesignCert) (hc : compilesOk D = true) :
    ∀ (s : RuntimeState) (is : List RuntimeInput), compiledTrace D s is = refTrace D s is := by
  intro s is
  induction is generalizing s with
  | nil => rfl
  | cons i is ih => simp only [compiledTrace, refTrace, compileAndRun_correct D hc i s, ih]

/-- The trace-level corollary. -/
theorem runDirect_eq_compiledTrace (D : DesignCert) (s : RuntimeState) (is : List RuntimeInput)
    (t : TraceResult) (hd : runDirect D s is = .ok t) (hc : compilesOk D = true) :
    t = compiledTrace D s is := by
  rw [compiledTrace_eq_refTrace D hc s is]
  exact runDirect_correct D s is t hd

--------------------------------------------------------------------------------
-- The executable oracle
--------------------------------------------------------------------------------

/-- The same agreement, against an ALREADY compiled residual program.  Stated
separately because the oracle below must not recompile the design every cycle:
`compileAndRun` calls `compileDesign` on each call, which is O(nodes) of pure
overhead when the certificate has not changed. -/
theorem directStep_eq_denoteResidual (D : DesignCert) (R : ResidualProgram)
    (i : RuntimeInput) (s : RuntimeState) (r : RuntimeResult)
    (hd : directStep D i s = .ok r) (hc : compileDesign D = .ok R) :
    r = denoteResidual R i s := by
  rw [compileDesign_correct D R hc i s]
  exact directStep_correct D i s r hd

/-- Compare the two implementations on the OBSERVABLE data: ordered outputs and
next flop state.  Memories are functions, so they are sampled at `memAddrs`
rather than compared with `DecidableEq`.

This is a differential TEST — an oracle for the sweep — not a proof.  The proof
is `directStep_eq_denoteResidual`; a passing differential run adds confidence
that the two ELABORATED artifacts are the ones the theorem is about. -/
def diffStep (D : DesignCert) (R : ResidualProgram) (i : RuntimeInput) (s : RuntimeState)
    (memAddrs : List Int) : Option String :=
  match directStep D i s with
  | .error e => some s!"direct refused: {e.render}"
  | .ok a =>
    let b := denoteResidual R i s
    if a.outputs ≠ b.outputs then some "outputs differ"
    else if a.nextState.flops ≠ b.nextState.flops then some "flop state differs"
    else
      let bad := (List.range D.memories.size).filter fun k =>
        memAddrs.any fun x =>
          (a.nextState.mems[k]?.elim (fun _ => mk_bv 0 0) id) x ≠
          (b.nextState.mems[k]?.elim (fun _ => mk_bv 0 0) id) x
      match bad with
      | []     => none
      | k :: _ => some s!"memory {k} differs"

/-- Run both implementations from `s`, stopping at the first disagreement. -/
def diffTrace (D : DesignCert) (R : ResidualProgram) (memAddrs : List Int) :
    RuntimeState → List RuntimeInput → Nat → Option String
  | _, [],      _ => none
  | s, i :: is, t =>
    match diffStep D R i s memAddrs with
    | some m => some s!"cycle {t}: {m}"
    | none   => diffTrace D R memAddrs (directStepRaw D i s).nextState is (t + 1)

end Direct
end Compiler
