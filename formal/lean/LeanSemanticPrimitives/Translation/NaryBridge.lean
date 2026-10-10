/- This file is distributed under the BSD 3-Clause License. See LICENSE for details. -/
import LeanSemanticPrimitives.Translation.OpBridge

/- General mixed-width folds for upstream graphs with coalesced operands.
   These theorems extend legacy bridge coverage; eval_op, the certificate
   vocabulary and the emitted fast-model expressions are unchanged. -/
namespace OpBridge

theorem bv_bitwise_and_step {w : Nat} (A : BitVec w) (B : BV) :
    bv_bitwise w (fun x y => x && y) (bvenc A) B = bvenc (A &&& bv_to_bitvec w B) := by
  apply bv_bitwise_eq
  intro i hi
  rw [bv_bit_bvenc, BitVec.getLsbD_and, bv_to_bitvec_getLsbD B i hi]

theorem andn_bv_foldl {w : Nat} : ∀ (bvs : List BV) (acc : BitVec w),
    bvs.foldl (fun a b => bv_bitwise w (fun x y => x && y) a b) (bvenc acc)
      = bvenc (bvs.foldl (fun a b => a &&& bv_to_bitvec w b) acc) := by
  intro bvs
  induction bvs with
  | nil => intro acc; rfl
  | cons b bs ih =>
    intro acc
    simp only [List.foldl_cons]
    rw [bv_bitwise_and_step]
    exact ih (acc &&& bv_to_bitvec w b)

theorem andn_bv_bridge {w : Nat} (a : BV) (rest : List BV) :
    eval_op LGraphOp.Op_And w (a :: rest)
      = bvenc (rest.foldl (fun acc b => acc &&& bv_to_bitvec w b) (bv_to_bitvec w a)) := by
  change rest.foldl _ (mk_bv w (bv_uint a)) = _
  rw [mk_bv_ofInt]
  exact andn_bv_foldl rest (bv_to_bitvec w a)

theorem bv_bitwise_xor_step {w : Nat} (A : BitVec w) (B : BV) :
    bv_bitwise w (fun x y => xor x y) (bvenc A) B = bvenc (A ^^^ bv_to_bitvec w B) := by
  apply bv_bitwise_eq
  intro i hi
  rw [bv_bit_bvenc, BitVec.getLsbD_xor, bv_to_bitvec_getLsbD B i hi]

theorem xorn_bv_foldl {w : Nat} : ∀ (bvs : List BV) (acc : BitVec w),
    bvs.foldl (fun a b => bv_bitwise w (fun x y => xor x y) a b) (bvenc acc)
      = bvenc (bvs.foldl (fun a b => a ^^^ bv_to_bitvec w b) acc) := by
  intro bvs
  induction bvs with
  | nil => intro acc; rfl
  | cons b bs ih =>
    intro acc
    simp only [List.foldl_cons]
    rw [bv_bitwise_xor_step]
    exact ih (acc ^^^ bv_to_bitvec w b)

theorem xorn_bv_bridge {w : Nat} (bvs : List BV) :
    eval_op LGraphOp.Op_Xor w bvs
      = bvenc (bvs.foldl (fun a b => a ^^^ bv_to_bitvec w b) 0#w) := by
  change bvs.foldl _ (mk_bv w 0) = _
  rw [show (mk_bv w 0) = bvenc (0#w) from by simp [bvenc]]
  exact xorn_bv_foldl bvs 0#w

theorem isum_bv_to_bitvec_emod {w : Nat} (args : List BV) :
    isum (args.map (bv_to_bitvec w)) % (2 : Int)^w = (args.map bv_uint).sum % (2 : Int)^w := by
  induction args with
  | nil => rfl
  | cons a rest ih =>
    change (Int.ofNat (bv_to_bitvec w a).toNat + isum (rest.map (bv_to_bitvec w))) % (2 : Int)^w =
      (bv_uint a + (rest.map bv_uint).sum) % (2 : Int)^w
    have h : Int.ofNat (bv_to_bitvec w a).toNat % (2 : Int)^w = bv_uint a % (2 : Int)^w :=
      (mk_bv_emod_eq (mk_bv_ofInt (w := w) (bv_uint a))).symm
    calc
      _ = (Int.ofNat (bv_to_bitvec w a).toNat % (2 : Int)^w +
          isum (rest.map (bv_to_bitvec w)) % (2 : Int)^w) % (2 : Int)^w := Int.add_emod _ _ _
      _ = (bv_uint a % (2 : Int)^w + (rest.map bv_uint).sum % (2 : Int)^w) % (2 : Int)^w := by rw [h, ih]
      _ = _ := (Int.add_emod _ _ _).symm

theorem sumn_bv_bridge {w : Nat} (n : Nat) (args : List BV) :
    eval_op (LGraphOp.Op_Sum n) w args =
      bvenc (sumn_fast ((args.take n).map (bv_to_bitvec w)) ((args.drop n).map (bv_to_bitvec w))) := by
  change mk_bv w (((args.map bv_uint).take n).sum - ((args.map bv_uint).drop n).sum) = _
  rw [← List.map_take, ← List.map_drop]
  unfold sumn_fast
  apply mk_bv_eq_of_emod
  rw [mk_bv_sub_emod]
  have ha := (mk_bv_emod_eq (bvenc_bvSum ((args.take n).map (bv_to_bitvec w)))).trans
    (isum_bv_to_bitvec_emod (w := w) (args.take n))
  have hs := (mk_bv_emod_eq (bvenc_bvSum ((args.drop n).map (bv_to_bitvec w)))).trans
    (isum_bv_to_bitvec_emod (w := w) (args.drop n))
  exact Int.ModEq.sub ha.symm hs.symm

end OpBridge
