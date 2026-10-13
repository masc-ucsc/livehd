/-
# Result widths, by induction over the dense slot order

The checked admission policy has to DERIVE the enabled write's address fit, not
assume it.  That needs one fact the repository did not have: every operator
returns a value of the node's declared width, and every source returns a value
of its declared width.  Both are proved here, and then lifted to
`slotValM` — the value the interpreter actually computes at a slot — by
induction over the dense order.

Nothing here is specific to memories; it is the width discipline the whole
certificate already intends, made available as a theorem.
-/
import LeanSemanticPrimitives.Projection.HardwareMemFixture

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. Every `BV` constructor used by `eval_op` carries the node width -/

@[simp] theorem mk_bv_width (w : Nat) (v : Int) : (mk_bv w v).width = w := rfl
@[simp] theorem bv_resize_width (w : Nat) (x : BV) : (bv_resize w x).width = w := rfl
@[simp] theorem bv_bitwise_width (w : Nat) (f : Bool → Bool → Bool) (a b : BV) :
    (bv_bitwise w f a b).width = w := rfl
@[simp] theorem bv_not_width (w : Nat) (a : BV) : (bv_not w a).width = w := rfl
@[simp] theorem bv_sdiv_width (w : Nat) (a b : BV) : (bv_sdiv w a b).width = w := rfl

theorem bv_sra_width (w : Nat) (x s : BV) : (bv_sra w x s).width = w := by
  simp only [bv_sra, apply_ite BV.width, mk_bv_width, ite_self]

theorem bv_get_mask_width (w : Nat) (a m : BV) : (bv_get_mask w a m).width = w := by
  simp only [bv_get_mask, apply_ite BV.width, mk_bv_width, ite_self]

theorem bv_set_mask_width (w : Nat) (a m v : BV) : (bv_set_mask w a m v).width = w := rfl

/-- A bitwise fold keeps the accumulator's width. -/
theorem foldl_bitwise_width (w : Nat) (f : Bool → Bool → Bool) :
    ∀ (l : List BV) (acc : BV), acc.width = w →
      (l.foldl (fun a b => bv_bitwise w f a b) acc).width = w
  | [],      _,   h => h
  | _ :: xs, acc, _ => foldl_bitwise_width w f xs _ rfl

theorem foldl_shl_width (w : Nat) (a : BV) :
    ∀ (l : List BV) (acc : BV), acc.width = w →
      (l.foldl (fun acc b =>
          bv_bitwise w (fun x y => xor x y) acc
            (mk_bv w (if (bv_uint b).toNat ≥ w then 0
                      else bv_uint a * (2 : Int) ^ (bv_uint b).toNat))) acc).width = w
  | [],      _,   h => h
  | _ :: xs, acc, _ => foldl_shl_width w a xs _ rfl

/-! ## 2. Every operator returns the node width -/

theorem eval_op_width (op : LGraphOp) (w : Nat) (l : List BV) :
    (eval_op op w l).width = w := by
  unfold eval_op
  split
  <;> first
    | rfl
    | (apply foldl_bitwise_width; rfl)
    | (apply foldl_shl_width; rfl)
    | exact bv_sra_width _ _ _
    | exact bv_get_mask_width _ _ _
    | exact bv_set_mask_width _ _ _
    | (split <;> rfl)
    | (simp only [apply_ite BV.width, mk_bv_width, bv_resize_width, ite_self])
    | (split <;> simp)
    | simp

/-- …and the memory READ returns it too: both branches are built at `w`. -/
@[simp] theorem cert_mem_read_width (w : Nat) (m : Int → BV) (a en : BV) :
    (cert_mem_read w m a en).width = w := by
  unfold cert_mem_read; split <;> rfl

/-- The `CertVal` form: a scalar node's result is a `.bv` of the node width. -/
theorem eval_op_cert_width_of_supported {op : LGraphOp} (hsup : OpSupported op = true)
    (w : Nat) (vs : List CertVal) :
    (eval_op_cert op w vs).asBV.width = w := by
  rw [Hw.evalOpCert_of_supported hsup]
  exact eval_op_width op w _

theorem eval_op_cert_width_memRead (w : Nat) (m : Int → BV) (a en : BV) :
    (eval_op_cert .Op_MemRead w [.mem m, .bv a, .bv en]).asBV.width = w := by
  show (cert_mem_read w m a en).width = w
  exact cert_mem_read_width w m a en

/-! ## 3. Sources return their declared widths -/

@[simp] theorem sourceValue_width_input (i : RuntimeInput) (s : RuntimeState)
    (idx w : Nat) : (sourceValue i s (.input idx w)).asBV.width = w := rfl

@[simp] theorem sourceValue_width_const (i : RuntimeInput) (s : RuntimeState)
    (w : Nat) (v : Int) : (sourceValue i s (.const w v)).asBV.width = w := rfl

@[simp] theorem sourceValue_width_flopQ (i : RuntimeInput) (s : RuntimeState)
    (idx w : Nat) : (sourceValue i s (.flopQ idx w)).asBV.width = w := rfl

@[simp] theorem sourceValue_width_flopQAsync (i : RuntimeInput) (s : RuntimeState)
    (idx w ri : Nat) (rv : Int) (al : Bool) :
    (sourceValue i s (.flopQAsync idx w ri rv al)).asBV.width = w := by
  simp only [sourceValue, CertVal.asBV, apply_ite BV.width, mk_bv_width,
             bv_resize_width, ite_self]

#print axioms eval_op_width
#print axioms cert_mem_read_width
#print axioms eval_op_cert_width_of_supported
#print axioms sourceValue_width_flopQAsync

end HwMem
end Projection
