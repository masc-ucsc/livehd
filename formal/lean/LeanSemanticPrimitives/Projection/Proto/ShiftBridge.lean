/-
# The first bridge lemma for deferred binder shifting

WHY THIS FILE EXISTS.  Sampling the native specialization of `alu` and
`csr_regfile` on both the FP pin and master puts `Projection.PVal.shiftGo` at
the top of 44-48% of worker samples and somewhere on 68-78% of them, with
allocator traffic accounting for most of the rest.  Eager binder shifting is
the dominant generation cost on the covered backend.

`Proto/PartialEvaluatorShift.lean` already prototypes the fix -- a `shifted k v`
constructor whose offset is COMPOSED rather than applied -- and measures 3x to
8x on the s3 grid.  It carries ZERO theorems, so it cannot enter the covered
simulator path: `specializeDesign_varT_correct` and `simSound_varT` take
`hproj : mixDriver … = .ok R` about the PROVED driver, and a run of an unproved
fork does not produce that premise.

WHAT THIS FILE DOES, AND WHAT IT DOES NOT.  It proves the arithmetic content
that makes offset COMPOSITION sound, against the PROVED `PVal.shift` and
`PEnv.shiftBy` -- nothing else.  It does not define a deferred representation,
does not relate `ProtoShift` to `PartialEvaluator`, and claims NOTHING about
the specializer being correct.  It is one step of the bridge listed in the
ledger, kernel-checked, with no `sorry`, no new axiom and no `native_decide`.

The pinned files are untouched: this is a new module, so the five specializer
files stay byte-identical to the FP pin.
-/
import LeanSemanticPrimitives.Projection.PartialEvaluator

namespace Projection
namespace ShiftBridge

/-- Composing two binder shifts is one shift by the sum.

This is the step a deferred representation needs: `shifted k₁ (shifted k₂ v)`
may be collapsed to `shifted (k₁ + k₂) v` only because the eager shifts it
stands for compose the same way. -/
theorem PVal_shift_add (k₁ k₂ : Nat) (v : PVal) :
    PVal.shift k₁ (PVal.shift k₂ v) = PVal.shift (k₁ + k₂) v := by
  induction v with
  | stat _ => rfl
  | dyn i =>
      show PVal.dyn (i + k₂ + k₁) = PVal.dyn (i + (k₁ + k₂))
      rw [Nat.add_assoc, Nat.add_comm k₂ k₁]
  | cons a b iha ihb =>
      show PVal.cons _ _ = PVal.cons _ _
      rw [iha, ihb]

/-- The environment-level form, needed because `mix` shifts whole environments
when it enters a binder. -/
theorem PEnv_shiftBy_add (k₁ k₂ : Nat) (e : PEnv) :
    PEnv.shiftBy k₁ (PEnv.shiftBy k₂ e) = PEnv.shiftBy (k₁ + k₂) e := by
  induction e with
  | nil => rfl
  | cons v rest ih =>
      show PVal.shift k₁ (PVal.shift k₂ v) :: _ = PVal.shift (k₁ + k₂) v :: _
      rw [PVal_shift_add, ih]

/-- A deferred offset of zero is the identity, so a representation may drop it.
Stated here for completeness of the composition laws; `PVal.shift_zero` is the
proved ingredient. -/
theorem PVal_shift_add_zero (k : Nat) (v : PVal) :
    PVal.shift k (PVal.shift 0 v) = PVal.shift k v := by
  rw [PVal_shift_add, Nat.add_zero]

end ShiftBridge
end Projection
