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
import LeanSemanticPrimitives.Compiler.CompileGraph

namespace Compiler

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

/-- Slots already written stay written, also in O(1).  Together with
`srcAgree_push` this covers every read a binding can make: an operand is either
a source (handled above) or an earlier binding (handled here). -/
theorem bindAgree_push {env : SlotEnv} (v : CertVal) (j : Nat) (val : BV)
    (hj : j < env.size) (h : refBV env j = val) :
    refBV (env.push v) j = val := by
  rw [refBV_push_lt env v j hj]; exact h

end Compiler
