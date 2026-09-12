/-
  The common simulator contract.

  Milestone 0 of `SIMULATOR_PLAN.md`: one `DesignCert`, one runtime, one
  reference semantics, shared with every other executable track rather than
  re-invented here.  `Compiler.interpretDesign` is imported verbatim at a pinned
  revision -- see `SHARED_SEMANTICS.md`.  A copied-and-modified `interpretDesign`
  would turn the Milestone 5 equivalence theorem into a statement about two
  different semantics, which is worth nothing.

  This module deliberately contains NO Futamura-specific machinery.  It is the
  vocabulary every track states its result in, so that cross-track equivalence
  is transitivity rather than a fresh proof per pair.  Nothing here mentions
  `mix`, the object language, or the residual program; nothing here imports
  `compileDesign` either, which is what keeps the eventual comparison with the
  verified compiler non-circular.
-/

import LeanSemanticPrimitives.Compiler.DesignSemantics

namespace Projection
open Compiler

/-! ## One cycle -/

/-- One cycle of an executable implementation, which may fail.  The error type
is a parameter because the tracks fail differently -- a specializer reports
`MixError`, a checked loader reports something else -- and none of that should
leak into the contract. -/
abbrev Step (ε : Type) :=
  DesignCert → RuntimeInput → RuntimeState → Except ε RuntimeResult

/-- The single obligation every executable track owes.

Stated on success only, deliberately: an implementation is free to refuse a
design it does not support, and refusing is not incorrect.  What it may not do
is return a wrong answer. -/
def StepCorrect {ε : Type} (step : Step ε) (D : DesignCert) : Prop :=
  ∀ i s r, step D i s = .ok r → r = interpretDesign D i s

/-- Any two correct implementations agree wherever both succeed.

This is the whole point of routing every track through `interpretDesign`:
pairwise equivalence is a corollary, not a proof obligation per pair.  Adding an
n-th track costs one `StepCorrect` instance, not n-1 comparisons. -/
theorem step_agree {ε₁ ε₂ : Type} {s₁ : Step ε₁} {s₂ : Step ε₂} {D : DesignCert}
    (h₁ : StepCorrect s₁ D) (h₂ : StepCorrect s₂ D)
    {i : RuntimeInput} {st : RuntimeState} {r₁ r₂ : RuntimeResult}
    (e₁ : s₁ D i st = .ok r₁) (e₂ : s₂ D i st = .ok r₂) : r₁ = r₂ := by
  rw [h₁ i st r₁ e₁, h₂ i st r₂ e₂]

/-! ## Traces

A one-cycle theorem is not a simulator result.  What a user runs is a trace, so
the contract lifts to one: thread `nextState` through a list of inputs. -/

/-- The reference trace. -/
def refTrace (D : DesignCert) : RuntimeState → List RuntimeInput → List RuntimeResult
  | _, []      => []
  | s, i :: is =>
      let r := interpretDesign D i s
      r :: refTrace D r.nextState is

/-- An implementation's trace, stopping at the first cycle it refuses. -/
def stepTrace {ε : Type} (step : Step ε) (D : DesignCert) :
    RuntimeState → List RuntimeInput → Except ε (List RuntimeResult)
  | _, []      => .ok []
  | s, i :: is =>
      match step D i s with
      | .error e => .error e
      | .ok r =>
        match stepTrace step D r.nextState is with
        | .error e => .error e
        | .ok rest => .ok (r :: rest)

/-- One cycle correct implies every trace correct.  The induction is the only
content: each step's `nextState` is what the next step starts from, on both
sides, so a single divergence would have to appear at the first cycle. -/
theorem stepTrace_correct {ε : Type} {step : Step ε} {D : DesignCert}
    (h : StepCorrect step D) :
    ∀ (s : RuntimeState) (is : List RuntimeInput) (rs : List RuntimeResult),
      stepTrace step D s is = .ok rs → rs = refTrace D s is := by
  intro s is
  induction is generalizing s with
  | nil => intro rs hr; simp only [stepTrace] at hr; cases hr; rfl
  | cons i is ih =>
      intro rs hr
      simp only [stepTrace] at hr
      split at hr <;> try contradiction
      rename_i r hstep
      split at hr <;> try contradiction
      rename_i rest hrest
      cases hr
      have hr1 : r = interpretDesign D i s := h i s r hstep
      subst hr1
      simp only [refTrace]
      rw [ih _ _ hrest]

/-- …and therefore any two correct implementations produce the same trace. -/
theorem trace_agree {ε₁ ε₂ : Type} {s₁ : Step ε₁} {s₂ : Step ε₂} {D : DesignCert}
    (h₁ : StepCorrect s₁ D) (h₂ : StepCorrect s₂ D)
    {st : RuntimeState} {is : List RuntimeInput} {r₁ r₂ : List RuntimeResult}
    (e₁ : stepTrace s₁ D st is = .ok r₁) (e₂ : stepTrace s₂ D st is = .ok r₂) :
    r₁ = r₂ := by
  rw [stepTrace_correct h₁ st is r₁ e₁, stepTrace_correct h₂ st is r₂ e₂]

/-! ## Milestone 0 acceptance

A single `DesignCert`, input and state, passed unchanged to the reference
semantics in THIS branch.  The point is not the arithmetic -- it is that the
certificate type, the runtime types and `interpretDesign` are the shared ones,
so an adapter for any track can be handed exactly these values. -/

namespace Acceptance

/-- `y = x &&& 12`, on 4 bits.  Two sources (slots 0 and 1), one node (slot 2),
one output.  Constants are certificate SOURCES, never node ops. -/
def tinyD : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := #[{ op := .Op_And, width := 4, deps := #[0, 1] }]
  outputs  := #[{ slot := 2, width := 4 }]
  flops    := #[]
  memories := #[]

def tinyIn : RuntimeInput := #[mk_bv 4 5]
def tinySt : RuntimeState := { flops := #[], mems := #[] }

-- 0b0101 &&& 0b1100 = 0b0100
#guard (interpretDesign tinyD tinyIn tinySt).outputs == #[mk_bv 4 4]

-- and it is combinational: the next state is empty, so a trace is stable
#guard (refTrace tinyD tinySt [tinyIn, tinyIn]).length == 2

end Acceptance

end Projection
