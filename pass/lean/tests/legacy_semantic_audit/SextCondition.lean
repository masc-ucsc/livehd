/-
One-bit Sext wrapper audit for the master adapter.
The expression is the emitted fast-model Sext operation. Positive amount and
result width preserve bit zero and nonzero for every one-bit source, including
both reset polarities. This is a condition-preservation theorem, not a claim
that every result bit is a copy of the source or that RTL-to-graph is verified.
Run from formal/lean with: lake env lean ../../pass/lean/tests/legacy_semantic_audit/SextCondition.lean
-/
import LeanSemanticPrimitives.Translation.OpBridge

namespace ResetWrapperAudit

def sextCondition (a : BitVec 1) (amount width : Nat) : BitVec width :=
  bv_sext (BitVec.ofNat amount a.toNat)

theorem sext_low_bit (a : BitVec 1) {amount width : Nat}
    (ha : 0 < amount) (hw : 0 < width) :
    (sextCondition a amount width).getLsbD 0 = a.getLsbD 0 := by
  change ((BitVec.ofNat amount a.toNat).signExtend width).getLsbD 0 = _
  rw [BitVec.getLsbD_signExtend]
  simp only [ha, hw, decide_true, Bool.true_and, ite_true]
  simp [ha]

theorem sext_zero_iff (a : BitVec 1) {amount width : Nat}
    (ha : 0 < amount) (hw : 0 < width) :
    sextCondition a amount width = 0#width ↔ a = 0#1 := by
  constructor
  · intro h
    apply BitVec.eq_of_getLsbD_eq
    intro i hi
    have : i = 0 := by omega
    subst i
    rw [← sext_low_bit a ha hw, h]
    simp
  · intro h
    subst a
    simp [sextCondition, bv_sext]

theorem sext_nonzero (a : BitVec 1) {amount width : Nat}
    (ha : 0 < amount) (hw : 0 < width) :
    bitvec_nonzero (sextCondition a amount width) = bitvec_nonzero a := by
  simp [bitvec_nonzero, sext_zero_iff a ha hw]

theorem sext_reset_asserted (a : BitVec 1) (activeLow : Bool) {amount width : Nat}
    (ha : 0 < amount) (hw : 0 < width) :
    (if activeLow then !bitvec_nonzero (sextCondition a amount width)
     else bitvec_nonzero (sextCondition a amount width)) =
    (if activeLow then !bitvec_nonzero a else bitvec_nonzero a) := by
  rw [sext_nonzero a ha hw]

-- Amount > 1 keeps the Boolean condition but does not replicate the bit.
example : sextCondition (1#1) 2 4 = 1#4 := by decide
example : sextCondition (1#1) 1 4 = 15#4 := by decide

#print axioms sext_low_bit
#print axioms sext_zero_iff
#print axioms sext_nonzero
#print axioms sext_reset_asserted
end ResetWrapperAudit
