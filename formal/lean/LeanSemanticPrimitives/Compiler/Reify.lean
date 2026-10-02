/-
# `Reify` — composition lemmas for translation validation (Direction 3)

The reifier emits a straight-line `let`-chain `Foo_step` and a proof that it
equals `denoteResidual R` for an explicit literal `R`.  The naive proof — one
`simp` blast over `runBindings` — is quadratic (measured 4.35x doubling ratio),
because it names every intermediate environment syntactically and each such
term has size O(k).

The escape is to never let an intermediate environment appear as a term.  Each
lemma below advances the walk by exactly one binding while the environment
stays a single free variable, so a generated proof can `generalize` after every
step and keep each step O(1).

## The invariant carried across the walk

Two facts are needed at step `k` and both must be O(1) to maintain:

  * `size`  — where the next binding lands;
  * `agree` — every SOURCE slot still reads as it did in the initial
    environment.  Without this, reading a source operand from step `k` costs
    `k` rewrites through the push chain, which is what makes a benchmark whose
    bindings all reference one source quadratic *by construction* rather than
    because the proof is bad.
-/
import LeanSemanticPrimitives.Compiler.CompileGraphDefs

namespace Compiler

/-- Run a binding list in two pieces.

`runBindings` is a fold that threads the environment, so splitting the list
splits the fold: the left piece produces an environment and the right piece
continues from it.  This is what lets a walk be proved in SEGMENTS, each stating
only its own bindings, instead of one proof whose goal carries the whole
remaining list at every step.

Proved by induction on the LEFT list, generalizing the environment -- the
environment differs at each step, so it cannot be fixed before the induction. -/
theorem runBindings_append (bs cs : List ResidualBinding) (env : SlotEnv) :
    runBindings (bs ++ cs) env = runBindings cs (runBindings bs env) := by
  induction bs generalizing env with
  | nil          => rfl
  | cons b bs ih => simp only [List.cons_append, runBindings, ih]

/-- Advance the fold by one binding.  The hypothesis is discharged against the
CURRENT environment, so the caller may keep that environment opaque. -/
theorem runBindings_step (b : ResidualBinding) (bs : List ResidualBinding)
    (env : SlotEnv) (v : CertVal) (hv : denoteExpr env b.rhs = v) :
    runBindings (b :: bs) env = runBindings bs (env.push v) := by
  simp only [runBindings, hv]

/-- Read an earlier slot through one push. -/
theorem refBV_push_lt (env : SlotEnv) (v : CertVal) (j : Nat) (h : j < env.size) :
    refBV (env.push v) j = refBV env j := by
  simp only [refBV, denoteRef, Array.getElem?_push_lt h, Array.getElem?_eq_getElem h,
    Option.getD_some]

/-- Read the slot just pushed. -/
theorem refBV_push_self (env : SlotEnv) (v : CertVal) :
    refBV (env.push v) env.size = v.asBV := by
  simp only [refBV, denoteRef, Array.getElem?_push_size, Option.getD_some]

theorem refMem_push_lt (env : SlotEnv) (v : CertVal) (j : Nat) (h : j < env.size) :
    refMem (env.push v) j = refMem env j := by
  simp only [refMem, denoteRef, Array.getElem?_push_lt h, Array.getElem?_eq_getElem h,
    Option.getD_some]

theorem refMem_push_self (env : SlotEnv) (v : CertVal) :
    refMem (env.push v) env.size = v.asMem := by
  simp only [refMem, denoteRef, Array.getElem?_push_size, Option.getD_some]

/-- Source agreement, propagated across one push in O(1).

This is the lemma that decouples proof cost from dependency DISTANCE.  Without
it a binding that reads a source `k` levels down costs `k` rewrites; with it
the read costs one, whatever `k` is. -/
theorem srcAgree_push {env base : SlotEnv} (v : CertVal)
    (hsz : base.size ≤ env.size)
    (h : ∀ j, j < base.size → refBV env j = refBV base j) :
    ∀ j, j < base.size → refBV (env.push v) j = refBV base j := by
  intro j hj
  rw [refBV_push_lt env v j (Nat.lt_of_lt_of_le hj hsz)]
  exact h j hj

theorem srcAgreeMem_push {env base : SlotEnv} (v : CertVal)
    (hsz : base.size ≤ env.size)
    (h : ∀ j, j < base.size → refMem env j = refMem base j) :
    ∀ j, j < base.size → refMem (env.push v) j = refMem base j := by
  intro j hj
  rw [refMem_push_lt env v j (Nat.lt_of_lt_of_le hj hsz)]
  exact h j hj

/-- How a SOURCE slot reads out of the INITIAL environment.

The base case of the walk, and the one lemma this file was missing.  Everything
else here moves a fact ACROSS a push; nothing discharged the read at the bottom
of the chain, and that is exactly where `sourceEnvArr`'s `Array.map` blocks the
kernel -- `Array.map` is well-founded recursion, so `refBV (sourceEnvArr ...) j`
will not reduce by `rfl` however small the design.

Without this, a generated walk can carry source agreement all the way down in
O(1) per step and then fail to cash it in. -/
theorem refBV_sourceEnv (srcs : Array SourceDesc) (i : RuntimeInput) (s : RuntimeState)
    (j : Nat) (h : j < srcs.size) :
    refBV (sourceEnvArr srcs i s) j = (sourceValue i s srcs[j]).asBV := by
  simp [refBV, denoteRef, sourceEnvArr, h]

/-- The memory-image counterpart of `refBV_sourceEnv`. -/
theorem refMem_sourceEnv (srcs : Array SourceDesc) (i : RuntimeInput) (s : RuntimeState)
    (j : Nat) (h : j < srcs.size) :
    refMem (sourceEnvArr srcs i s) j = (sourceValue i s srcs[j]).asMem := by
  simp [refMem, denoteRef, sourceEnvArr, h]

/-- Slots already written stay written, also in O(1).  Together with
`srcAgree_push` this covers every read a binding can make: an operand is either
a source (handled above) or an earlier binding (handled here). -/
theorem bindAgree_push {env : SlotEnv} (v : CertVal) (j : Nat) (val : BV)
    (hj : j < env.size) (h : refBV env j = val) :
    refBV (env.push v) j = val := by
  rw [refBV_push_lt env v j hj]; exact h

/-- The memory-image counterpart of `bindAgree_push`.

`srcAgreeMem_push` transports the UNIVERSAL memory agreement over the source
block; this transports ONE already-written memory-valued slot.  A binding whose
`ty` is `.mem` produces an `Int → BV`, and carrying it across a push with
`bindAgree_push` would be a type error -- so a walk over any design with a
memory-valued intermediate needs this and nothing else will do. -/
theorem bindAgreeMem_push {env : SlotEnv} (v : CertVal) (j : Nat) (val : Int → BV)
    (hj : j < env.size) (h : refMem env j = val) :
    refMem (env.push v) j = val := by
  rw [refMem_push_lt env v j hj]; exact h

/-- `flopNext` IS `flopNextV` applied to the operands read out of the
environment.

The bridge the generated walk needs: it lets a flop's next value be stated in
terms of named per-binding values, exactly as a combinational output is, instead
of re-deriving reset priority and polarity on the fast side where they could
disagree. -/
theorem flopNext_eq (env : SlotEnv) (s : RuntimeState) (idx : Nat)
    (f : ResidualFlopUpdate) :
    flopNext env s idx f
      = flopNextV f.width (refBV env f.din) (f.enable.map (refBV env))
          (f.resetPin.map (refBV env)) f.resetValue f.resetActiveLow (s.flops[idx]?) := by
  -- The two bodies are the SAME expression once the options are resolved, so
  -- each case closes by `rfl`.  `simp` normalises the two `if`s differently and
  -- leaves a goal that looks like real content but is not.
  obtain ⟨w, din, en, rst, rv, ral⟩ := f
  cases rst <;> cases en <;> rfl

end Compiler
