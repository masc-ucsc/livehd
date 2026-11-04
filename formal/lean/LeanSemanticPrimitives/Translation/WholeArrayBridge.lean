/- This file is distributed under the BSD 3-Clause License. See LICENSE for details. -/
import LeanSemanticPrimitives.Translation.OpBridge
import LeanSemanticPrimitives.Translation.WholeArray

/- The legacy proof bridge for the whole-array memory operators (decision #2):
the fast model's `mem_load` / `mem_to_bus` (`WholeArray`) equal the
certificate's `cert_mem_load` / `cert_mem_to_bus` on encoded values, as
`mem_write_bridge` does for one write. A separate file, as MulBridge and
SextBridge are, so the pinned OpBridge.lean stays byte-identical; the emitter
imports it only into a design with a whole-array memory. -/
namespace OpBridge


/-- On the guarded range `BitVec.ofInt` keeps the value. -/
theorem toNat_ofInt_in {a : Nat} {x : Int} (h0 : 0 ≤ x) (h1 : x < 2 ^ a) :
    (BitVec.ofInt a x).toNat = x.toNat := by
  have h := ofNat_toNat_ofInt h0 h1
  conv_rhs => rw [← h]
  rfl

/-- `Op_MemLoad n`: one enable-gated whole-array load. `n ≤ 2^a` (a memory
never has more entries than addresses) keeps the load inside `memenc`'s guard;
the emitter discharges it by `decide`. -/
theorem mem_load_bridge {a d bw we : Nat} (m : BitVec a → BitVec d) (bus : BitVec bw)
    (en : BitVec we) (n : Nat) (hn : n ≤ 2 ^ a) :
    cert_mem_load d n (memenc m) (bvenc bus) (bvenc en)
      = memenc (if bitvec_nonzero en then mem_load m bus n else m) := by
  unfold cert_mem_load
  rw [bv_nonzero_bvenc]
  by_cases h : bitvec_nonzero en
  · simp only [h, ↓reduceIte]
    funext x
    rw [bv_uint_bvenc]
    unfold memenc mem_load
    by_cases hr : 0 ≤ x ∧ x < 2 ^ a
    · have hk := toNat_ofInt_in hr.1 hr.2
      by_cases hx : 0 ≤ x ∧ x < n
      · have hxn : x.toNat < n := by omega
        simp only [hx, hr, and_self, ↓reduceIte, hk, hxn]
        unfold bvenc
        apply mk_bv_eq_of_emod
        rw [BitVec.toNat_ofNat, Nat.shiftRight_eq_div_pow]
        simp only [Int.ofNat_eq_natCast]
        push_cast
        rw [Int.emod_emod_of_dvd _ (dvd_refl _)]
      · have hxn : ¬ x.toNat < n := by omega
        have hxl : ¬ x < n := by omega
        simp only [hr, hxl, hxn, and_self, and_false, ↓reduceIte, hk]
    · have hx : ¬ (0 ≤ x ∧ x < n) := by
        intro hc; apply hr; refine ⟨hc.1, ?_⟩
        have : (n : Int) ≤ 2 ^ a := by exact_mod_cast hn
        omega
      simp only [hx, hr, ↓reduceIte]
  · simp only [h, Bool.false_eq_true, ↓reduceIte]

/-- The certificate's packing of an encoded memory is the fast packing. -/
theorem mem_pack_memenc {a d : Nat} (e : Nat) (m : BitVec a → BitVec d) :
    ∀ k, cert_mem_pack e (memenc m) k = Int.ofNat (mem_pack e m k)
  | 0 => rfl
  | k + 1 => by
      simp only [cert_mem_pack, mem_pack, mem_pack_memenc e m k]
      by_cases hk : k < 2 ^ a
      · have hr : (0 : Int) ≤ (k : Int) ∧ (k : Int) < 2 ^ a := ⟨by omega, by exact_mod_cast hk⟩
        have hof : BitVec.ofInt a (k : Int) = BitVec.ofNat a k := by
          apply BitVec.eq_of_toNat_eq
          rw [toNat_ofInt_in hr.1 hr.2, BitVec.toNat_ofNat, Nat.mod_eq_of_lt hk]
          simp
        simp only [memenc, hr, and_self, ↓reduceIte, hof, bv_uint_bvenc, hk, Int.ofNat_eq_natCast]
        push_cast; ring
      · have hr : ¬ ((0 : Int) ≤ (k : Int) ∧ (k : Int) < 2 ^ a) := by
          intro hc; apply hk; exact_mod_cast hc.2
        simp only [memenc, hr, ↓reduceIte, hk, Int.ofNat_eq_natCast]
        simp [bv_uint, mk_bv]

/-- `Op_MemToBus n`: the whole-array read. -/
theorem mem_to_bus_bridge {a d : Nat} (m : BitVec a → BitVec d) (n w : Nat) :
    cert_mem_to_bus w n (memenc m) = bvenc (mem_to_bus m n w) := by
  unfold cert_mem_to_bus mem_to_bus bvenc
  rw [mem_pack_memenc]
  apply mk_bv_eq_of_emod
  rw [BitVec.toNat_ofNat]
  simp only [Int.ofNat_eq_natCast]
  push_cast
  rw [Int.emod_emod_of_dvd _ (dvd_refl _)]

end OpBridge
