/-
# `DirectVsCompiled` — the two implementations agree where both apply

Direction 2, phase 3's third theorem and phase 6's evaluation oracle.

This is the ONLY module in the Direction 2 chain that mentions the residual
compiler.  `DirectSemantics` must not, or the direct evaluator would just be a
second entry point to B1+B2; the agreement below is a CONSEQUENCE of the two
correctness theorems, not part of either definition.

It is deliberately a short composition:

    directStep D e i s = .ok r    gives  r = interpretDesign D e i s
    compilesOk D = true           gives  compileAndRun D e i s = interpretDesign D e i s

so `r = compileAndRun D e i s` follows with no node-by-node reasoning.  If this
file ever needed an induction, something upstream would be wrong.
-/
import LeanSemanticPrimitives.Compiler.CompileDesign
import LeanSemanticPrimitives.Compiler.DirectTrace

namespace Compiler
namespace Direct

--------------------------------------------------------------------------------
-- One step
--------------------------------------------------------------------------------

/-- **Cross-implementation agreement.**  On every certificate both paths accept,
the direct interpreter and the verified compiler's output are the same value —
at every edge vector, input and state. -/
theorem directStep_eq_compileAndRun (D : DesignCert) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) (r : RuntimeResult) (hd : directStep D e i s = .ok r)
    (hc : compilesOk D = true) :
    r = compileAndRun D e i s := by
  rw [compileAndRun_correct D hc e i s]
  exact directStep_correct D e i s r hd

--------------------------------------------------------------------------------
-- Traces
--------------------------------------------------------------------------------

/-- The compiled trace: iterate `compileAndRun`. -/
def compiledTrace (D : DesignCert) : RuntimeState → List Tick → TraceResult
  | s, []       => { steps := [], finalState := s }
  | s, tk :: ts =>
      let r := compileAndRun D tk.edges tk.input s
      let t := compiledTrace D r.nextState ts
      { steps := r :: t.steps, finalState := t.finalState }

theorem compiledTrace_eq_refTrace (D : DesignCert) (hc : compilesOk D = true) :
    ∀ (s : RuntimeState) (ts : List Tick), compiledTrace D s ts = refTrace D s ts := by
  intro s ts
  induction ts generalizing s with
  | nil => rfl
  | cons tk ts ih =>
      simp only [compiledTrace, refTrace, compileAndRun_correct D hc tk.edges tk.input s, ih]

/-- The trace-level corollary. -/
theorem runDirect_eq_compiledTrace (D : DesignCert) (s : RuntimeState) (ts : List Tick)
    (t : TraceResult) (hd : runDirect D s ts = .ok t) (hc : compilesOk D = true) :
    t = compiledTrace D s ts := by
  rw [compiledTrace_eq_refTrace D hc s ts]
  exact runDirect_correct D s ts t hd

--------------------------------------------------------------------------------
-- The executable oracle
--------------------------------------------------------------------------------

/-- The same agreement, against an ALREADY compiled residual program.  Stated
separately because the oracle below must not recompile the design every step:
`compileAndRun` calls `compileDesign` on each call, which is O(nodes) of pure
overhead when the certificate has not changed. -/
theorem directStep_eq_denoteResidual (D : DesignCert) (R : ResidualProgram)
    (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) (r : RuntimeResult)
    (hd : directStep D e i s = .ok r) (hc : compileDesign D = .ok R) :
    r = denoteResidual R e i s := by
  rw [compileDesign_correct D R hc e i s]
  exact directStep_correct D e i s r hd

/-- Compare the two implementations on the OBSERVABLE data: ordered outputs and
next flop state.  Memories are functions, so they are sampled at `memAddrs`
rather than compared with `DecidableEq`.

This is a differential TEST — an oracle for the sweep — not a proof.  The proof
is `directStep_eq_denoteResidual`; a passing differential run adds confidence
that the two ELABORATED artifacts are the ones the theorem is about. -/
def diffStep (D : DesignCert) (R : ResidualProgram) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) (memAddrs : List Int) : Option String :=
  match directStep D e i s with
  | .error err => some s!"direct refused: {err.render}"
  | .ok a =>
    let b := denoteResidual R e i s
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
    RuntimeState → List Tick → Nat → Option String
  | _, [],       _ => none
  | s, tk :: ts, t =>
    match diffStep D R tk.edges tk.input s memAddrs with
    | some m => some s!"cycle {t}: {m}"
    | none   => diffTrace D R memAddrs (directStepRaw D tk.edges tk.input s).nextState ts (t + 1)

end Direct
end Compiler
