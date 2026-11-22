/-
# The staged step is the direct step

`stagedStep_eq`: for a certificate whose nodes read only earlier slots (`DepsBounded`, which
`checkDesign` establishes), `Staged.step (Staged.prepare D)` returns exactly what
`directStepRaw D` returns: the same outputs, flops and memory images.

* Each specialized operator's value is `eval_op`'s, on reduced values (`and_val` ...
  `shl_val`), from the `@[csimp]` equalities of `Translation/FastBV`.
* A slot invariant (`SlotOK`: the value array holds the slot's `uintNat`, and `exact` gives
  the slot itself) holds after the sources (`srcLoop_ok`) and is kept by every node
  (`nodeLoop_ok`).
* Then every slot the step reads is the direct step's, so the outputs, flops and memories are.

Mathlib-free.
-/
import LeanSemanticPrimitives.Compiler.DirectStaged
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Compiler
namespace Staged

open FastBV Direct

/-! ## Reduced values -/

theorem uintNat_ofNatLt {w n : Nat} (h : n < 2 ^ w) : uintNat (ofNatLt w n) = n := by
  show modNat w (Int.ofNat n) = n
  simp only [modNat, modPow2_eq]
  exact Nat.mod_eq_of_lt h

theorem modPow2_lt (w x : Nat) : modPow2 w x < 2 ^ w := by
  rw [modPow2_eq]; exact Nat.mod_lt _ (Nat.two_pow_pos w)

theorem truncNat_lt (w x : Nat) : truncNat w x < 2 ^ w := modPow2_lt w x

theorem mk_bv_eq_ofNatLt (w : Nat) (v : Int) : mk_bv w v = ofNatLt w (modNat w v) := by
  rw [mk_bv_eq_fast]; rfl

theorem bv_resize_eq_ofNatLt (w : Nat) (x : BV) : bv_resize w x = ofNatLt w (truncNat w (uintNat x)) := by
  rw [bv_resize_eq_fast]; rfl

theorem bv_nonzero_eq (x : BV) : bv_nonzero x = (uintNat x != 0) := by
  rw [bv_nonzero_eq_fast]; rfl

theorem bv_uint_eq (x : BV) : bv_uint x = ((uintNat x : Nat) : Int) := bv_uint_eq_uintNat x

theorem modNat_ite (w : Nat) (c : Prop) [Decidable c] :
    modNat w (if c then 1 else 0) = truncNat w (if c then 1 else 0) := by
  by_cases h : c <;> simp [h, modNat, truncNat]

theorem foldl_congr_mem {α β : Type} {f g : β → α → β} :
    ∀ (l : List α) (init : β), (∀ x ∈ l, ∀ acc, f acc x = g acc x) → l.foldl f init = l.foldl g init
  | [], _, _ => rfl
  | x :: l, init, h => by
    simp only [List.foldl_cons]
    rw [h x (List.mem_cons_self ..) init]
    exact foldl_congr_mem l _ (fun y hy acc => h y (List.mem_cons_of_mem _ hy) acc)

/-! ## Each specialized operator, on reduced values -/

/-- A bitwise fold, `f false false = false` (AND, OR, XOR). -/
theorem fold_bitwise (w : Nat) (f : Bool → Bool → Bool) (hf : f false false = false)
    (g : Nat → Nat → Nat) (hg : ∀ A B, bitwiseDirect f A B = g A B) :
    ∀ (bs : List BV) (n : Nat), n < 2 ^ w →
      bs.foldl (fun acc b => bv_bitwise w f acc b) (ofNatLt w n) =
        ofNatLt w (bs.foldl (fun acc b => modPow2 w (g acc (uintNat b))) n)
  | [], _, _ => rfl
  | b :: bs, n, hn => by
    simp only [List.foldl_cons]
    have h1 : bv_bitwise w f (ofNatLt w n) b = ofNatLt w (modPow2 w (g n (uintNat b))) := by
      rw [bv_bitwise_eq_fast]
      unfold bvBitwiseFast
      simp only [hf, Bool.false_eq_true, ↓reduceIte]
      rw [uintNat_ofNatLt hn, hg]
    rw [h1]
    exact fold_bitwise w f hf g hg bs _ (modPow2_lt _ _)

theorem fold_lt (w : Nat) (g : Nat → Nat → Nat) :
    ∀ (bs : List BV) (n : Nat), n < 2 ^ w → bs.foldl (fun acc b => modPow2 w (g acc (uintNat b))) n < 2 ^ w
  | [], _, h => h
  | _ :: bs, _, _ => fold_lt w g bs _ (modPow2_lt _ _)

theorem and_val (w : Nat) (a : BV) (bs : List BV) :
    eval_op .Op_And w (a :: bs) =
      ofNatLt w (bs.foldl (fun acc b => modPow2 w (acc &&& uintNat b)) (truncNat w (uintNat a))) := by
  show bs.foldl (fun acc b => bv_bitwise w (fun x y => x && y) acc b) (bv_resize w a) = _
  rw [bv_resize_eq_ofNatLt]
  exact fold_bitwise w _ rfl (fun A B => A &&& B) (fun A B => by simp [bitwiseDirect]) bs _ (truncNat_lt _ _)

theorem or_val (w : Nat) (bs : List BV) :
    eval_op .Op_Or w bs = ofNatLt w (bs.foldl (fun acc b => modPow2 w (acc ||| uintNat b)) 0) := by
  show bs.foldl (fun acc b => bv_bitwise w (fun x y => x || y) acc b) (mk_bv w 0) = _
  rw [mk_bv_eq_ofNatLt]
  have h0 : modNat w 0 = 0 := by simp [modNat, modPow2_eq]
  rw [h0]
  exact fold_bitwise w _ rfl (fun A B => A ||| B) (fun A B => by simp [bitwiseDirect]) bs _ (Nat.two_pow_pos w)

theorem xor_val (w : Nat) (bs : List BV) :
    eval_op .Op_Xor w bs = ofNatLt w (bs.foldl (fun acc b => modPow2 w (acc ^^^ uintNat b)) 0) := by
  show bs.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc b) (mk_bv w 0) = _
  rw [mk_bv_eq_ofNatLt]
  have h0 : modNat w 0 = 0 := by simp [modNat, modPow2_eq]
  rw [h0]
  exact fold_bitwise w _ rfl (fun A B => A ^^^ B) (fun A B => by simp [bitwiseDirect]) bs _ (Nat.two_pow_pos w)

theorem ror_val (w : Nat) (bs : List BV) :
    eval_op .Op_Ror w bs = ofNatLt w (truncNat w (if bs.any (fun b => uintNat b != 0) then 1 else 0)) := by
  show mk_bv w (if bs.any bv_nonzero then 1 else 0) = _
  rw [mk_bv_eq_ofNatLt]
  have : bs.any bv_nonzero = bs.any (fun b => uintNat b != 0) := by
    congr 1; funext b; exact bv_nonzero_eq b
  rw [this]
  by_cases h : bs.any (fun b => uintNat b != 0) = true <;> simp [h, modNat, truncNat]

theorem not_val (w : Nat) (a : BV) :
    eval_op .Op_Not w [a] = ofNatLt w (bitwiseNatTab w (fun x _ => !x) (uintNat a) 0) := by
  show bv_not w a = _
  rw [bv_not_eq_fast]; rfl

theorem mux_val (w : Nat) (s f t : BV) :
    eval_op .Op_MuxBool w [s, f, t] =
      ofNatLt w (if uintNat s != 0 then truncNat w (uintNat t) else truncNat w (uintNat f)) := by
  show (if bv_nonzero s then bv_resize w t else bv_resize w f) = _
  rw [bv_nonzero_eq, bv_resize_eq_ofNatLt, bv_resize_eq_ofNatLt]
  split <;> rfl

theorem eq_val (w : Nat) (a : BV) (bs : List BV) :
    eval_op .Op_EQ w (a :: bs) =
      ofNatLt w (truncNat w (if bs.all (fun b => uintNat b == uintNat a) then 1 else 0)) := by
  show mk_bv w (if bs.all fun b => bv_uint b = bv_uint a then 1 else 0) = _
  rw [mk_bv_eq_ofNatLt]
  have : (bs.all fun b => decide (bv_uint b = bv_uint a)) = bs.all (fun b => uintNat b == uintNat a) := by
    congr 1; funext b
    rw [bv_uint_eq, bv_uint_eq]
    by_cases hb : uintNat b = uintNat a
    · simp [hb]
    · have hb' : ((uintNat b : Nat) : Int) ≠ ((uintNat a : Nat) : Int) := fun h => hb (Int.ofNat.inj h)
      simp [hb, hb']
  rw [this]
  by_cases h : bs.all (fun b => uintNat b == uintNat a) = true <;> simp [h, modNat, truncNat]

theorem ult_val (w : Nat) (a b : BV) :
    eval_op .Op_ULT w [a, b] = ofNatLt w (truncNat w (if uintNat a < uintNat b then 1 else 0)) := by
  show mk_bv w (if bv_uint a < bv_uint b then 1 else 0) = _
  rw [mk_bv_eq_ofNatLt, bv_uint_eq, bv_uint_eq]
  by_cases h : uintNat a < uintNat b <;> simp [h, modNat, truncNat]

theorem lt_val (w : Nat) (a b : BV) :
    eval_op .Op_LT w [a, b] = ofNatLt w (truncNat w (if uintNat a < uintNat b then 1 else 0)) := by
  show mk_bv w (if bv_uint a < bv_uint b then 1 else 0) = _
  rw [mk_bv_eq_ofNatLt, bv_uint_eq, bv_uint_eq]
  by_cases h : uintNat a < uintNat b <;> simp [h, modNat, truncNat]

theorem ugt_val (w : Nat) (a b : BV) :
    eval_op .Op_UGT w [a, b] = ofNatLt w (truncNat w (if uintNat a > uintNat b then 1 else 0)) := by
  show mk_bv w (if bv_uint a > bv_uint b then 1 else 0) = _
  rw [mk_bv_eq_ofNatLt, bv_uint_eq, bv_uint_eq]
  by_cases h : uintNat a > uintNat b <;> simp [h, modNat, truncNat]

theorem gt_val (w : Nat) (a b : BV) :
    eval_op .Op_GT w [a, b] = ofNatLt w (truncNat w (if uintNat a > uintNat b then 1 else 0)) := by
  show mk_bv w (if bv_uint a > bv_uint b then 1 else 0) = _
  rw [mk_bv_eq_ofNatLt, bv_uint_eq, bv_uint_eq]
  by_cases h : uintNat a > uintNat b <;> simp [h, modNat, truncNat]

theorem getMask_val (w : Nat) (x m : BV) (s L : Nat) (hm : uintNat m = (2 ^ L - 1) * 2 ^ s)
    (hle : s + L ≤ m.width) :
    eval_op .Op_GetMask w [x, m] = ofNatLt w (truncNat w ((uintNat x >>> s) &&& (2 ^ L - 1))) := by
  show bv_get_mask w x m = _
  unfold bv_get_mask
  rw [mask_indices_bv_contig m s L hm hle, pack_low_bv_range', mk_bv_natCast_mod,
    Nat.and_two_pow_sub_one_eq_mod, truncNat_eq]

theorem sext_val (w : Nat) (a n : BV) :
    eval_op .Op_Sext w [a, n] = ofNatLt w (sextNat w a.width (uintNat a) (uintNat n)) := by
  show sextOp w a n = _
  rw [sextOp_eq_fast]
  unfold sextOpFast sextNat
  simp only []
  split
  · rfl
  · split
    · rfl
    · split
      · split <;> rfl
      · split <;> rfl

theorem sra_val (w : Nat) (x k : BV) :
    eval_op .Op_SRA w [x, k] = ofNatLt w (sraNat w x.width (uintNat x) (uintNat k)) := by
  show bv_sra w x k = _
  rw [bv_sra_eq_fast]
  unfold bvSraFast sraNat
  simp only []
  split
  · rw [mk_bv_eq_ofNatLt]
  · split
    · rfl
    · rw [mk_bv_eq_ofNatLt]

theorem shlStep_val (w : Nat) (a b : BV) : bv_shl_step w a b = ofNatLt w (shlStepNat w (uintNat a) (uintNat b)) := by
  rw [bv_shl_step_eq_fast]
  unfold bvShlStepFast shlStepNat
  simp only []
  split
  · rfl
  · split <;> rfl

theorem shl_val (w : Nat) (a : BV) (bs : List BV) :
    eval_op .Op_SHL w (a :: bs) =
      ofNatLt w (bs.foldl (fun acc b => modPow2 w (acc ^^^ shlStepNat w (uintNat a) (uintNat b))) 0) := by
  show bs.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc (bv_shl_step w a b)) (mk_bv w 0) = _
  rw [mk_bv_eq_ofNatLt]
  have h0 : modNat w 0 = 0 := by simp [modNat, modPow2_eq]
  rw [h0]
  have hlt : ∀ b : BV, shlStepNat w (uintNat a) (uintNat b) < 2 ^ w := by
    intro b
    have h1 := shlStep_val w a b
    unfold bv_shl_step at h1
    rw [mk_bv_eq_ofNatLt] at h1
    have h2 : ((modNat w (if (bv_uint b).toNat ≥ w then 0 else bv_uint a * (2 : Int) ^ (bv_uint b).toNat) : Nat) : Int)
        = ((shlStepNat w (uintNat a) (uintNat b) : Nat) : Int) := congrArg BV.value h1
    have h3 := Int.ofNat.inj h2
    rw [← h3]
    exact modNat_lt _ _
  have key : ∀ (bs : List BV) (n : Nat), n < 2 ^ w →
      bs.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc (bv_shl_step w a b)) (ofNatLt w n) =
        ofNatLt w (bs.foldl (fun acc b => modPow2 w (acc ^^^ shlStepNat w (uintNat a) (uintNat b))) n) := by
    intro bs
    induction bs with
    | nil => intro n _; rfl
    | cons b bs ih =>
      intro n hn
      simp only [List.foldl_cons]
      have h1 : bv_bitwise w (fun x y => xor x y) (ofNatLt w n) (bv_shl_step w a b) =
          ofNatLt w (modPow2 w (n ^^^ shlStepNat w (uintNat a) (uintNat b))) := by
        rw [bv_bitwise_eq_fast]
        unfold bvBitwiseFast
        simp only [Bool.xor_false, Bool.false_eq_true, ↓reduceIte]
        rw [uintNat_ofNatLt hn, shlStep_val, uintNat_ofNatLt (hlt b)]
        congr 2
      rw [h1]
      exact ih _ (modPow2_lt _ _)
  exact key bs 0 (Nat.two_pow_pos w)

/-! ## Operands -/

/-- Slot `r` as the staged step sees it: the value array holds its reduced value; a constant
source is its constant; a slot that is not a side slot is exactly `⟨W[r], vals[r]⟩`. -/
structure OpdOK (D : DesignCert) (W : Array Nat) (side : Array Bool) (vals : Array Nat)
    (rho : Nat → CertVal) (r : Nat) : Prop where
  val   : vals.getD r 0 = uintNat (rho r).asBV
  const : ∀ w v, D.sources[r]? = some (.const w v) → rho r = .bv (mk_bv w v)
  plain : side.getD r true = false → r < W.size →
            rho r = .bv { width := W[r]!, value := .ofNat (vals.getD r 0) }

theorem opd_get {D W side vals rho r} (h : OpdOK D W side vals rho r) :
    (opdOf D r).get vals = uintNat (rho r).asBV := by
  unfold opdOf
  split
  · rename_i w v hc
    rw [h.const w v hc]
    show modNat w v = uintNat (mk_bv w v)
    rw [mk_bv_eq_ofNatLt, uintNat_ofNatLt (modNat_lt _ _)]
  · exact h.val

theorem plain_asBV {D W side vals rho r aw} (h : OpdOK D W side vals rho r)
    (hp : plainWidth W side r = some aw) :
    (rho r).asBV = { width := aw, value := .ofNat (vals.getD r 0) } := by
  unfold plainWidth at hp
  split at hp
  · rename_i hc
    cases hp
    rw [h.plain hc.2 hc.1]; rfl
  · cases hp

theorem foldl_opds {D W side vals rho} (ds : List Nat) (hops : ∀ d ∈ ds, OpdOK D W side vals rho d)
    (f : Nat → Nat → Nat) (init : Nat) :
    (ds.map (opdOf D)).foldl (fun acc o => f acc (o.get vals)) init =
      (ds.map fun d => (rho d).asBV).foldl (fun acc b => f acc (uintNat b)) init := by
  rw [List.foldl_map, List.foldl_map]
  exact foldl_congr_mem ds init (fun d hd acc => by rw [opd_get (hops d hd)])

theorem any_opds {D W side vals rho} : ∀ (ds : List Nat), (∀ d ∈ ds, OpdOK D W side vals rho d) →
    (ds.map (opdOf D)).any (fun o => o.get vals != 0) =
      (ds.map fun d => (rho d).asBV).any (fun b => uintNat b != 0)
  | [], _ => rfl
  | d :: ds, h => by
    simp only [List.map_cons, List.any_cons]
    rw [opd_get (h d (List.mem_cons_self ..)), any_opds ds (fun x hx => h x (List.mem_cons_of_mem _ hx))]

theorem all_opds {D W side vals rho} (u : Nat) : ∀ (ds : List Nat), (∀ d ∈ ds, OpdOK D W side vals rho d) →
    (ds.map (opdOf D)).all (fun o => o.get vals == u) =
      (ds.map fun d => (rho d).asBV).all (fun b => uintNat b == u)
  | [], _ => rfl
  | d :: ds, h => by
    simp only [List.map_cons, List.all_cons]
    rw [opd_get (h d (List.mem_cons_self ..)), all_opds u ds (fun x hx => h x (List.mem_cons_of_mem _ hx))]

theorem runOf_spec {M mw s mask : Nat} (h : runOf M mw = some (s, mask)) :
    ∃ L, s + L ≤ mw ∧ M = (2 ^ L - 1) * 2 ^ s ∧ mask = 2 ^ L - 1 := by
  unfold runOf at h
  simp only [] at h
  split at h
  · rename_i hc
    cases h
    exact ⟨_, hc.1, hc.2, rfl⟩
  · cases h

/-! ## A specialized node is its operator -/

theorem toList_two {ds : Array Nat} (h : ds.size = 2) : ∃ d0 d1, ds.toList = [d0, d1] := by
  obtain ⟨l, hl⟩ : ∃ l, ds.toList = l := ⟨_, rfl⟩
  have hlen : l.length = 2 := by rw [← hl]; simp [h]
  match l, hlen with
  | [d0, d1], _ => exact ⟨d0, d1, hl⟩

theorem toList_three {ds : Array Nat} (h : ds.size = 3) : ∃ d0 d1 d2, ds.toList = [d0, d1, d2] := by
  obtain ⟨l, hl⟩ : ∃ l, ds.toList = l := ⟨_, rfl⟩
  have hlen : l.length = 3 := by rw [← hl]; simp [h]
  match l, hlen with
  | [d0, d1, d2], _ => exact ⟨d0, d1, d2, hl⟩

theorem toList_one {ds : Array Nat} (h : ds.size = 1) : ∃ d0, ds.toList = [d0] := by
  obtain ⟨l, hl⟩ : ∃ l, ds.toList = l := ⟨_, rfl⟩
  have hlen : l.length = 1 := by rw [← hl]; simp [h]
  match l, hlen with
  | [d0], _ => exact ⟨d0, hl⟩

theorem toList_pos {ds : Array Nat} (h : ds.size ≥ 1) : ∃ d0 dt, ds.toList = d0 :: dt := by
  obtain ⟨l, hl⟩ : ∃ l, ds.toList = l := ⟨_, rfl⟩
  have hlen : l.length ≥ 1 := by rw [← hl]; simp; omega
  match l, hlen with
  | d0 :: dt, _ => exact ⟨d0, dt, hl⟩

theorem getD_of_toList {ds : Array Nat} {l : List Nat} (h : ds.toList = l) (k : Nat) :
    ds.getD k 0 = l.getD k 0 := by
  subst h
  simp [Array.getD_eq_getD_getElem?, List.getD_eq_getElem?_getD]

theorem extract_rest {ds : Array Nat} {d0 : Nat} {dt : List Nat} (h : ds.toList = d0 :: dt) :
    (ds.extract 1 ds.size).toList = dt := by
  rw [Array.toList_extract, h, ← Array.length_toList, h]
  simp [List.extract]

theorem rest_foldl {D W side vals rho} {ds : Array Nat} {d0 : Nat} {dt : List Nat}
    (hl : ds.toList = d0 :: dt) (hops : ∀ d ∈ ds.toList, OpdOK D W side vals rho d)
    (f : Nat → Nat → Nat) (init : Nat) :
    ((ds.extract 1 ds.size).map (opdOf D)).foldl (fun acc o => f acc (o.get vals)) init =
      (dt.map fun d => (rho d).asBV).foldl (fun acc b => f acc (uintNat b)) init := by
  rw [← Array.foldl_toList, Array.toList_map, extract_rest hl]
  exact foldl_opds dt (fun d hd => hops d (by rw [hl]; exact List.mem_cons_of_mem _ hd)) f init

theorem all_foldl {D W side vals rho} {ds : Array Nat} (hops : ∀ d ∈ ds.toList, OpdOK D W side vals rho d)
    (f : Nat → Nat → Nat) (init : Nat) :
    (ds.map (opdOf D)).foldl (fun acc o => f acc (o.get vals)) init =
      (ds.toList.map fun d => (rho d).asBV).foldl (fun acc b => f acc (uintNat b)) init := by
  rw [← Array.foldl_toList, Array.toList_map]
  exact foldl_opds ds.toList hops f init

theorem first_get {D W side vals rho} {ds : Array Nat} {d0 : Nat} {dt : List Nat}
    (hl : ds.toList = d0 :: dt) (hops : ∀ d ∈ ds.toList, OpdOK D W side vals rho d) :
    (opdOf D (ds.getD 0 0)).get vals = uintNat (rho d0).asBV := by
  rw [getD_of_toList hl]
  exact opd_get (hops d0 (by rw [hl]; exact List.mem_cons_self ..))

theorem sextNat_lt (w aw u n : Nat) : sextNat w aw u n < 2 ^ w := by
  unfold sextNat
  simp only []
  split
  · exact Nat.two_pow_pos w
  · split
    · exact truncNat_lt _ _
    · split
      · rename_i h
        have hm : maskTab[w]'h.2 < 2 ^ w := by rw [maskTab_get _ h.2]; have := Nat.two_pow_pos w; omega
        split
        · exact Nat.lt_of_le_of_lt Nat.and_le_right hm
        · exact Nat.lt_of_le_of_lt Nat.and_le_right hm
      · split <;> exact modPow2_lt _ _

theorem sraNat_lt (w xw u k : Nat) : sraNat w xw u k < 2 ^ w := by
  unfold sraNat
  split
  · exact modNat_lt _ _
  · split
    · exact modPow2_lt _ _
    · exact modNat_lt _ _

/-- The staged value of a specialized node: `eval_op_cert`'s value on the direct step's slots,
and below `2 ^ width`. -/
theorem node_val {D : DesignCert} {W : Array Nat} {side : Array Bool} {vals : Array Nat}
    {rho : Nat → CertVal} (c : DenseNodeCert)
    (hops : ∀ d ∈ c.deps.toList, OpdOK D W side vals rho d)
    (hgen : ∀ c', specialize D W side c ≠ .generic c') :
    eval_op_cert c.op c.width (c.deps.toList.map rho) =
        .bv (ofNatLt c.width ((specialize D W side c).val vals)) ∧
      (specialize D W side c).val vals < 2 ^ c.width := by
  unfold specialize at hgen ⊢
  simp only [] at hgen ⊢
  cases hop : c.op <;> simp only [hop] at hgen ⊢
  all_goals first | exact absurd rfl (hgen _) | skip
  case Op_And =>
    by_cases hs : c.deps.size ≥ 1
    · obtain ⟨d0, dt, hl⟩ := toList_pos hs
      simp only [hs, ↓reduceIte, SNode.val]
      rw [rest_foldl hl hops (fun acc x => modPow2 c.width (acc &&& x)), first_get hl hops]
      refine ⟨?_, fold_lt _ _ _ _ (truncNat_lt _ _)⟩
      rw [hl]
      show CertVal.bv (eval_op .Op_And c.width ((rho d0 :: dt.map rho).map CertVal.asBV)) = _
      rw [List.map_cons, List.map_map, and_val]
      rfl
    · simp only [hs, ↓reduceIte] at hgen
      exact absurd rfl (hgen c)
  case Op_Or =>
    simp only [SNode.val]
    rw [all_foldl hops (fun acc x => modPow2 c.width (acc ||| x))]
    refine ⟨?_, fold_lt _ _ _ _ (Nat.two_pow_pos _)⟩
    show CertVal.bv (eval_op .Op_Or c.width ((c.deps.toList.map rho).map CertVal.asBV)) = _
    rw [List.map_map, or_val]; rfl
  case Op_Xor =>
    simp only [SNode.val]
    rw [all_foldl hops (fun acc x => modPow2 c.width (acc ^^^ x))]
    refine ⟨?_, fold_lt _ _ _ _ (Nat.two_pow_pos _)⟩
    show CertVal.bv (eval_op .Op_Xor c.width ((c.deps.toList.map rho).map CertVal.asBV)) = _
    rw [List.map_map, xor_val]; rfl
  case Op_Ror =>
    simp only [SNode.val]
    rw [← Array.any_toList, Array.toList_map, any_opds _ hops]
    refine ⟨?_, truncNat_lt _ _⟩
    show CertVal.bv (eval_op .Op_Ror c.width ((c.deps.toList.map rho).map CertVal.asBV)) = _
    rw [List.map_map, ror_val]; rfl
  case Op_Not =>
    by_cases hs : c.deps.size = 1
    · obtain ⟨d0, hl⟩ := toList_one hs
      simp only [hs, ↓reduceIte, SNode.val]
      rw [first_get (dt := []) hl hops]
      refine ⟨?_, by rw [bitwiseNatTab_eq]; exact bitwiseNat_lt _ _ _ _⟩
      rw [hl]
      show CertVal.bv (eval_op .Op_Not c.width [(rho d0).asBV]) = _
      rw [not_val]
    · simp only [hs, ↓reduceIte] at hgen
      exact absurd rfl (hgen c)
  case Op_MuxBool =>
    by_cases hs : c.deps.size = 3
    · obtain ⟨d0, d1, d2, hl⟩ := toList_three hs
      have h0 := opd_get (hops d0 (by rw [hl]; simp))
      have h1 := opd_get (hops d1 (by rw [hl]; simp))
      have h2 := opd_get (hops d2 (by rw [hl]; simp))
      simp only [hs, ↓reduceIte, SNode.val, getD_of_toList hl]
      simp only [List.getD_cons_zero, List.getD_cons_succ]
      rw [h0, h1, h2]
      refine ⟨?_, by split <;> exact truncNat_lt _ _⟩
      rw [hl]
      show CertVal.bv (eval_op .Op_MuxBool c.width [(rho d0).asBV, (rho d1).asBV, (rho d2).asBV]) = _
      rw [mux_val]
    · simp only [hs, ↓reduceIte] at hgen
      exact absurd rfl (hgen c)
  case Op_EQ =>
    by_cases hs : c.deps.size ≥ 1
    · obtain ⟨d0, dt, hl⟩ := toList_pos hs
      simp only [hs, ↓reduceIte, SNode.val]
      rw [first_get hl hops, ← Array.all_toList, Array.toList_map, extract_rest hl,
        all_opds _ dt (fun d hd => hops d (by rw [hl]; exact List.mem_cons_of_mem _ hd))]
      refine ⟨?_, truncNat_lt _ _⟩
      rw [hl]
      show CertVal.bv (eval_op .Op_EQ c.width ((rho d0 :: dt.map rho).map CertVal.asBV)) = _
      rw [List.map_cons, List.map_map, eq_val]; rfl
    · simp only [hs, ↓reduceIte] at hgen
      exact absurd rfl (hgen c)
  case Op_SHL =>
    by_cases hs : c.deps.size ≥ 1
    · obtain ⟨d0, dt, hl⟩ := toList_pos hs
      simp only [hs, ↓reduceIte, SNode.val]
      rw [first_get hl hops,
        rest_foldl hl hops (fun acc x => modPow2 c.width (acc ^^^ shlStepNat c.width (uintNat (rho d0).asBV) x))]
      refine ⟨?_, fold_lt c.width (fun acc x => acc ^^^ shlStepNat c.width (uintNat (rho d0).asBV) x) _ 0
        (Nat.two_pow_pos _)⟩
      rw [hl]
      show CertVal.bv (eval_op .Op_SHL c.width ((rho d0 :: dt.map rho).map CertVal.asBV)) = _
      rw [List.map_cons, List.map_map, shl_val]; rfl
    · simp only [hs, ↓reduceIte] at hgen
      exact absurd rfl (hgen c)
  case Op_ULT =>
    by_cases hs : c.deps.size = 2
    · obtain ⟨d0, d1, hl⟩ := toList_two hs
      simp only [hs, ↓reduceIte, SNode.val, getD_of_toList hl, List.getD_cons_zero, List.getD_cons_succ]
      rw [opd_get (hops d0 (by rw [hl]; simp)), opd_get (hops d1 (by rw [hl]; simp))]
      refine ⟨?_, truncNat_lt _ _⟩
      rw [hl]
      show CertVal.bv (eval_op .Op_ULT c.width [(rho d0).asBV, (rho d1).asBV]) = _
      rw [ult_val]
    · simp only [hs, ↓reduceIte] at hgen
      exact absurd rfl (hgen c)
  case Op_LT =>
    by_cases hs : c.deps.size = 2
    · obtain ⟨d0, d1, hl⟩ := toList_two hs
      simp only [hs, ↓reduceIte, SNode.val, getD_of_toList hl, List.getD_cons_zero, List.getD_cons_succ]
      rw [opd_get (hops d0 (by rw [hl]; simp)), opd_get (hops d1 (by rw [hl]; simp))]
      refine ⟨?_, truncNat_lt _ _⟩
      rw [hl]
      show CertVal.bv (eval_op .Op_LT c.width [(rho d0).asBV, (rho d1).asBV]) = _
      rw [lt_val]
    · simp only [hs, ↓reduceIte] at hgen
      exact absurd rfl (hgen c)
  case Op_UGT =>
    by_cases hs : c.deps.size = 2
    · obtain ⟨d0, d1, hl⟩ := toList_two hs
      simp only [hs, ↓reduceIte, SNode.val, getD_of_toList hl, List.getD_cons_zero, List.getD_cons_succ]
      rw [opd_get (hops d0 (by rw [hl]; simp)), opd_get (hops d1 (by rw [hl]; simp))]
      refine ⟨?_, truncNat_lt _ _⟩
      rw [hl]
      show CertVal.bv (eval_op .Op_UGT c.width [(rho d0).asBV, (rho d1).asBV]) = _
      rw [ugt_val]
    · simp only [hs, ↓reduceIte] at hgen
      exact absurd rfl (hgen c)
  case Op_GT =>
    by_cases hs : c.deps.size = 2
    · obtain ⟨d0, d1, hl⟩ := toList_two hs
      simp only [hs, ↓reduceIte, SNode.val, getD_of_toList hl, List.getD_cons_zero, List.getD_cons_succ]
      rw [opd_get (hops d0 (by rw [hl]; simp)), opd_get (hops d1 (by rw [hl]; simp))]
      refine ⟨?_, truncNat_lt _ _⟩
      rw [hl]
      show CertVal.bv (eval_op .Op_GT c.width [(rho d0).asBV, (rho d1).asBV]) = _
      rw [gt_val]
    · simp only [hs, ↓reduceIte] at hgen
      exact absurd rfl (hgen c)
  case Op_Sext =>
    by_cases hs : c.deps.size = 2
    · obtain ⟨d0, d1, hl⟩ := toList_two hs
      simp only [hs, ↓reduceIte, getD_of_toList hl, List.getD_cons_zero, List.getD_cons_succ] at hgen ⊢
      cases hp : plainWidth W side d0 with
      | none => simp only [hp] at hgen; exact absurd rfl (hgen c)
      | some aw =>
        simp only [SNode.val]
        have hw : ((rho d0).asBV).width = aw := by rw [plain_asBV (hops d0 (by rw [hl]; simp)) hp]
        rw [opd_get (hops d0 (by rw [hl]; simp)), opd_get (hops d1 (by rw [hl]; simp)), ← hw]
        refine ⟨?_, sextNat_lt _ _ _ _⟩
        rw [hl]
        show CertVal.bv (eval_op .Op_Sext c.width [(rho d0).asBV, (rho d1).asBV]) = _
        rw [sext_val]
    · simp only [hs, ↓reduceIte] at hgen
      exact absurd rfl (hgen c)
  case Op_SRA =>
    by_cases hs : c.deps.size = 2
    · obtain ⟨d0, d1, hl⟩ := toList_two hs
      simp only [hs, ↓reduceIte, getD_of_toList hl, List.getD_cons_zero, List.getD_cons_succ] at hgen ⊢
      cases hp : plainWidth W side d0 with
      | none => simp only [hp] at hgen; exact absurd rfl (hgen c)
      | some aw =>
        simp only [SNode.val]
        have hw : ((rho d0).asBV).width = aw := by rw [plain_asBV (hops d0 (by rw [hl]; simp)) hp]
        rw [opd_get (hops d0 (by rw [hl]; simp)), opd_get (hops d1 (by rw [hl]; simp)), ← hw]
        refine ⟨?_, sraNat_lt _ _ _ _⟩
        rw [hl]
        show CertVal.bv (eval_op .Op_SRA c.width [(rho d0).asBV, (rho d1).asBV]) = _
        rw [sra_val]
    · simp only [hs, ↓reduceIte] at hgen
      exact absurd rfl (hgen c)
  case Op_GetMask =>
    by_cases hs : c.deps.size = 2
    · obtain ⟨d0, d1, hl⟩ := toList_two hs
      simp only [hs, ↓reduceIte, getD_of_toList hl, List.getD_cons_zero, List.getD_cons_succ] at hgen ⊢
      have h1 := hops d1 (by rw [hl]; simp)
      cases hsrc : D.sources[d1]? with
      | none => simp only [hsrc] at hgen; exact absurd rfl (hgen c)
      | some sd =>
        cases sd with
        | const mw v =>
          simp only [hsrc] at hgen ⊢
          cases hr : runOf (modNat mw v) mw with
          | none => simp only [hr] at hgen; exact absurd rfl (hgen c)
          | some p =>
            obtain ⟨s', mask⟩ := p
            obtain ⟨L, hle, hM, hmask⟩ := runOf_spec hr
            simp only [SNode.val]
            rw [opd_get (hops d0 (by rw [hl]; simp)), hmask]
            refine ⟨?_, truncNat_lt _ _⟩
            rw [hl]
            show CertVal.bv (eval_op .Op_GetMask c.width [(rho d0).asBV, (rho d1).asBV]) = _
            rw [h1.const mw v hsrc]
            have hu : uintNat (mk_bv mw v) = (2 ^ L - 1) * 2 ^ s' := by
              rw [mk_bv_eq_ofNatLt, uintNat_ofNatLt (modNat_lt _ _), hM]
            show CertVal.bv (eval_op .Op_GetMask c.width [(rho d0).asBV, mk_bv mw v]) = _
            rw [getMask_val c.width _ _ s' L hu (by show s' + L ≤ mw; exact hle)]
        | _ => simp only [hsrc] at hgen; exact absurd rfl (hgen c)
    · simp only [hs, ↓reduceIte] at hgen
      exact absurd rfl (hgen c)

/-! ## The prepared design -/

theorem plainWidth_ext {W : Array Nat} {side1 side2 : Array Bool} {r : Nat}
    (h : side1.getD r true = side2.getD r true) : plainWidth W side1 r = plainWidth W side2 r := by
  unfold plainWidth; rw [h]

/-- `specialize` reads the side flags only at the node's own operands. -/
theorem specialize_ext {D : DesignCert} {W : Array Nat} {side1 side2 : Array Bool} {c : DenseNodeCert}
    (h : ∀ r ∈ c.deps.toList, side1.getD r true = side2.getD r true) :
    specialize D W side1 c = specialize D W side2 c := by
  unfold specialize
  simp only []
  by_cases hs : c.deps.size = 2
  · obtain ⟨d0, d1, hl⟩ := toList_two hs
    have hm : side1.getD (c.deps.getD 0 0) true = side2.getD (c.deps.getD 0 0) true := by
      rw [getD_of_toList hl]; exact h d0 (by rw [hl]; simp)
    simp only [plainWidth_ext hm]
  · simp only [hs, ↓reduceIte]

/-- A generic node carries the node itself. -/
theorem specialize_generic {D : DesignCert} {W : Array Nat} {side : Array Bool} {c c' : DenseNodeCert}
    (h : specialize D W side c = .generic c') : c' = c := by
  unfold specialize at h
  simp only [] at h
  cases hop : c.op <;> simp only [hop] at h <;> (repeat' split at h) <;> simp_all

theorem foldSide (D : DesignCert) (W : Array Nat) : ∀ (cs : List DenseNodeCert) (acc : Array Bool),
    (cs.foldl (fun side c => side.push (specialize D W side c).isGen) acc).size = acc.size + cs.length ∧
    (∀ k, k < acc.size →
      (cs.foldl (fun side c => side.push (specialize D W side c).isGen) acc).getD k true = acc.getD k true) ∧
    (∀ j (hj : j < cs.length),
      (cs.foldl (fun side c => side.push (specialize D W side c).isGen) acc).getD (acc.size + j) true =
        (specialize D W ((cs.take j).foldl (fun side c => side.push (specialize D W side c).isGen) acc)
          cs[j]).isGen)
  | [], acc => ⟨by simp, fun _ _ => rfl, fun j hj => absurd hj (by simp)⟩
  | c :: cs, acc => by
    obtain ⟨h1, h2, h3⟩ := foldSide D W cs (acc.push (specialize D W acc c).isGen)
    simp only [List.foldl_cons, Array.size_push] at h1 h2 h3 ⊢
    refine ⟨by rw [h1]; simp; omega, fun k hk => ?_, fun j hj => ?_⟩
    · rw [h2 k (by omega)]
      simp [Array.getD_eq_getD_getElem?, Array.getElem?_push, show k ≠ acc.size by omega]
    · cases j with
      | zero =>
        rw [Nat.add_zero, h2 acc.size (by omega)]
        simp [Array.getD_eq_getD_getElem?]
      | succ j =>
        have := h3 j (by simp at hj; omega)
        rw [show acc.size + (j + 1) = acc.size + 1 + j by omega, this]
        simp [List.take_succ_cons]

section
variable (D : DesignCert)

theorem prep_W_size : (prepare D).W.size = D.sources.size + D.nodes.size := by
  simp [prepare]

theorem prep_W_src {r : Nat} (h : r < D.sources.size) :
    (prepare D).W.getD r 0 = srcWidth (D.sources[r]) := by
  simp only [prepare, Array.getD_eq_getD_getElem?]
  rw [Array.getElem?_append_left (by simpa using h)]
  simp [h]

theorem prep_W_node {j : Nat} (h : j < D.nodes.size) :
    (prepare D).W.getD (D.sources.size + j) 0 = (D.nodes[j]).width := by
  simp [prepare, Array.getD_eq_getD_getElem?, h]

theorem prep_side_size : (prepare D).side.size = D.sources.size + D.nodes.size := by
  simp only [prepare, sideOf, ← Array.foldl_toList]
  rw [(foldSide D _ D.nodes.toList _).1]
  simp

theorem prep_side_src {r : Nat} (h : r < D.sources.size) :
    (prepare D).side.getD r true = srcSide (D.sources[r]) := by
  simp only [prepare, sideOf, ← Array.foldl_toList]
  rw [(foldSide D _ D.nodes.toList _).2.1 r (by simpa using h)]
  simp [Array.getD_eq_getD_getElem?, h]

theorem prep_nodes_size : (prepare D).nodes.size = D.nodes.size := by simp [prepare]

theorem prep_node {j : Nat} (h : j < D.nodes.size) :
    (prepare D).nodes[j]! = specialize D (prepare D).W (prepare D).side (D.nodes[j]) := by
  simp [prepare, h]

/-- A node's side flag: whether its specialization (against every flag) is generic. -/
theorem prep_side_node (hdb : DesignCert.DepsBounded D) {j : Nat} (h : j < D.nodes.size) :
    (prepare D).side.getD (D.sources.size + j) true =
      (specialize D (prepare D).W (prepare D).side (D.nodes[j])).isGen := by
  have hf := foldSide D (prepare D).W D.nodes.toList (D.sources.map srcSide)
  have hside : (prepare D).side =
      D.nodes.toList.foldl (fun side c => side.push (specialize D (prepare D).W side c).isGen)
        (D.sources.map srcSide) := by
    simp only [prepare, sideOf, ← Array.foldl_toList]
  have h3 := hf.2.2 j (by simpa using h)
  simp only [Array.size_map, Array.getElem_toList] at h3
  rw [hside, h3]
  congr 1
  apply specialize_ext
  intro r hr
  have hr' : r < D.sources.size + j := hdb j (D.nodes[j]) (by simp [h]) r hr
  have hsz := (foldSide D (prepare D).W (D.nodes.toList.take j) (D.sources.map srcSide)).1
  simp only [Array.size_map, List.length_take, Array.length_toList] at hsz
  have hpre := (foldSide D (prepare D).W (D.nodes.toList.drop j)
    ((D.nodes.toList.take j).foldl (fun side c => side.push (specialize D (prepare D).W side c).isGen)
      (D.sources.map srcSide))).2.1 r (by rw [hsz]; omega)
  have hfull : D.nodes.toList.foldl (fun side c => side.push (specialize D (prepare D).W side c).isGen)
      (D.sources.map srcSide) = (D.nodes.toList.drop j).foldl
      (fun side c => side.push (specialize D (prepare D).W side c).isGen)
      ((D.nodes.toList.take j).foldl (fun side c => side.push (specialize D (prepare D).W side c).isGen)
        (D.sources.map srcSide)) := by
    rw [← List.foldl_append, List.take_append_drop]
  rw [hfull, hpre]

end

/-! ## The direct step's slots -/

section
variable (D : DesignCert) (i : RuntimeInput) (s : RuntimeState)

/-- The direct step's slot environment. -/
abbrev envOf : SlotEnv := evalDenseList D.nodes.toList (sourceEnvArr D.sources i s)

theorem envOf_size : (envOf D i s).size = D.sources.size + D.nodes.size := by
  simp [envOf, evalDenseList_size, sourceEnvArr_size]

theorem envOf_src {r : Nat} (h : r < D.sources.size) :
    denoteRef (envOf D i s) r = sourceValue i s (D.sources[r]) := by
  unfold denoteRef envOf
  rw [evalDenseList_stable _ _ r (by rw [sourceEnvArr_size]; exact h)]
  simp [sourceEnvArr, h]

theorem envOf_node (hdb : DesignCert.DepsBounded D) {j : Nat} (h : j < D.nodes.size) :
    denoteRef (envOf D i s) (D.sources.size + j) =
      eval_op_cert (D.nodes[j]).op (D.nodes[j]).width
        ((D.nodes[j]).deps.toList.map (denoteRef (envOf D i s))) := by
  have hat := evalDenseList_at D.nodes.toList (sourceEnvArr D.sources i s) j (by simpa using h)
  rw [sourceEnvArr_size] at hat
  unfold denoteRef envOf
  rw [hat]
  simp only [Array.getElem?_toList, Array.getElem?_eq_getElem h, Option.map_some, Option.getD_some]
  show eval_op_cert _ _ _ = _
  congr 1
  apply List.map_congr_left
  intro d hd
  have hd' : d < D.sources.size + j := hdb j (D.nodes[j]) (by simp [h]) d hd
  unfold denoteRef
  rw [evalDenseList_prefix_agree D.nodes.toList (sourceEnvArr D.sources i s) j d (by simp; omega)
    (by rw [sourceEnvArr_size]; exact hd')]

end

theorem ite_src_lt (b : Bool) (w : Nat) (rv : Int) (x : Nat) :
    (if b then modNat w rv else truncNat w x) < 2 ^ w := by
  cases b
  · exact truncNat_lt _ _
  · exact modNat_lt _ _

theorem ite_src (b : Bool) (w : Nat) (rv : Int) (x : BV) :
    CertVal.bv (if b then mk_bv w rv else bv_resize w x) =
      .bv (ofNatLt w (if b then modNat w rv else truncNat w (uintNat x))) := by
  cases b
  · simp only [Bool.false_eq_true, ↓reduceIte, bv_resize_eq_ofNatLt]
  · simp only [↓reduceIte, mk_bv_eq_ofNatLt]

theorem srcNat_lt (i : RuntimeInput) (s : RuntimeState) (sd : SourceDesc) :
    srcNat i s sd < 2 ^ srcWidth sd := by
  cases sd
  · exact truncNat_lt _ _
  · exact modNat_lt _ _
  · exact truncNat_lt _ _
  · unfold srcNat srcWidth; exact ite_src_lt _ _ _ _
  all_goals exact Nat.two_pow_pos _

/-- A bit-vector source's value, as the staged step keeps it. -/
theorem sourceValue_bv (i : RuntimeInput) (s : RuntimeState) (sd : SourceDesc) (hs : srcSide sd = false) :
    sourceValue i s sd = .bv (ofNatLt (srcWidth sd) (srcNat i s sd)) := by
  cases sd <;> simp only [srcSide, Bool.true_eq_false] at hs
  · simp only [sourceValue, srcNat, srcWidth, bv_resize_eq_ofNatLt]
  · simp only [sourceValue, srcNat, srcWidth, mk_bv_eq_ofNatLt]
  · simp only [sourceValue, srcNat, srcWidth, bv_resize_eq_ofNatLt]
  · unfold sourceValue srcNat srcWidth
    exact ite_src _ _ _ _

theorem uintNat_mem_asBV (f : Int → BV) : uintNat (CertVal.mem f).asBV = 0 := by
  show uintNat (mk_bv 0 0) = 0
  rw [mk_bv_eq_ofNatLt, uintNat_ofNatLt (modNat_lt _ _)]
  simp [modNat, modPow2_eq]

theorem sourceValue_side (i : RuntimeInput) (s : RuntimeState) (sd : SourceDesc) (hs : srcSide sd = true) :
    uintNat (sourceValue i s sd).asBV = 0 := by
  cases sd <;> simp only [srcSide, Bool.false_eq_true] at hs <;> exact uintNat_mem_asBV _

/-! ## The source loop -/

theorem getD_set! {α : Type} (a : Array α) (k r : Nat) (x d : α) (hk : k < a.size) :
    (a.set! k x).getD r d = if r = k then x else a.getD r d := by
  simp only [Array.set!_eq_setIfInBounds, Array.getD_eq_getD_getElem?, Array.getElem?_setIfInBounds]
  by_cases h : r = k
  · subst h; simp [hk]
  · simp [Ne.symm h, h]

/-- Source `r` of the design, as the loop reads it. -/
abbrev sdOf (P : Prep) (r : Nat) : SourceDesc := P.D.sources[r]?.getD (.const 0 0)

theorem srcLoop_spec (P : Prep) (i : RuntimeInput) (s : RuntimeState) (k : Nat) (vals : Array Nat)
    (memv : Array CertVal) (hidx : ∀ x ∈ P.srcIdx.toList, x < vals.size ∧ x < memv.size) :
    (srcLoop P i s k vals memv).1.size = vals.size ∧ (srcLoop P i s k vals memv).2.size = memv.size ∧
    (∀ r, (srcLoop P i s k vals memv).1.getD r 0 =
      if r ∈ P.srcIdx.toList.drop k ∧ srcSide (sdOf P r) = false then srcNat i s (sdOf P r)
      else vals.getD r 0) ∧
    (∀ r, (srcLoop P i s k vals memv).2.getD r (.bv (mk_bv 0 0)) =
      if r ∈ P.srcIdx.toList.drop k ∧ srcSide (sdOf P r) = true then sourceValue i s (sdOf P r)
      else memv.getD r (.bv (mk_bv 0 0))) := by
  unfold srcLoop
  split
  · rename_i hk
    dsimp only
    have hmem : P.srcIdx[k] ∈ P.srcIdx.toList := by simp
    have hdrop : P.srcIdx.toList.drop k = P.srcIdx[k] :: P.srcIdx.toList.drop (k + 1) := by
      rw [List.drop_eq_getElem_cons (by simpa using hk)]; simp
    obtain ⟨hv, hm⟩ := hidx _ hmem
    split
    · rename_i hside
      obtain ⟨h1, h2, h3, h4⟩ := srcLoop_spec P i s (k + 1) vals (memv.set! P.srcIdx[k] _)
        (fun x hx => by simp only [Array.size_set!]; exact hidx x hx)
      refine ⟨h1, by rw [h2]; simp, fun r => ?_, fun r => ?_⟩
      · rw [h3, hdrop]
        by_cases hr : r = P.srcIdx[k]
        · subst hr; simp [sdOf, hside]
        · simp [hr]
      · rw [h4, hdrop, getD_set! _ _ _ _ _ hm]
        by_cases hr : r = P.srcIdx[k]
        · subst hr; simp [sdOf, hside]
        · simp [hr]
    · rename_i hside
      obtain ⟨h1, h2, h3, h4⟩ := srcLoop_spec P i s (k + 1) (vals.set! P.srcIdx[k] _) memv
        (fun x hx => by simp only [Array.size_set!]; exact hidx x hx)
      refine ⟨by rw [h1]; simp, h2, fun r => ?_, fun r => ?_⟩
      · rw [h3, hdrop, getD_set! _ _ _ _ _ hv]
        by_cases hr : r = P.srcIdx[k]
        · subst hr; simp [sdOf, hside]
        · simp [hr]
      · rw [h4, hdrop]
        by_cases hr : r = P.srcIdx[k]
        · subst hr; simp [sdOf, hside]
        · simp [hr]
  · rename_i hk
    have : P.srcIdx.toList.drop k = [] := by simp; omega
    simp [this]
termination_by P.srcIdx.size - k

/-! ## The slot invariant -/

/-- Slot `r` of the staged arrays is the direct step's slot. -/
def SlotOK (P : Prep) (env : SlotEnv) (vals : Array Nat) (memv : Array CertVal) (r : Nat) : Prop :=
  vals.getD r 0 = uintNat (denoteRef env r).asBV ∧ exact P vals memv r = denoteRef env r

theorem getD_in {α : Type} {a : Array α} {r : Nat} (h : r < a.size) (d1 d2 : α) : a.getD r d1 = a.getD r d2 := by
  simp [Array.getD_eq_getD_getElem?, Array.getElem?_eq_getElem h]

theorem getD_bang {a : Array Nat} {r : Nat} (h : r < a.size) : a.getD r 0 = a[r]! := by
  simp [Array.getD_eq_getD_getElem?, Array.getElem?_eq_getElem h, getElem!_pos a r h]

section
variable (D : DesignCert) (i : RuntimeInput) (s : RuntimeState)

theorem opdOK_of (vals : Array Nat) (memv : Array CertVal) (r : Nat)
    (hr : r < D.sources.size + D.nodes.size) (h : SlotOK (prepare D) (envOf D i s) vals memv r) :
    OpdOK D (prepare D).W (prepare D).side vals (denoteRef (envOf D i s)) r where
  val := h.1
  const := by
    intro w v hc
    have hrs : r < D.sources.size := by
      rcases Nat.lt_or_ge r D.sources.size with h' | h'
      · exact h'
      · rw [Array.getElem?_eq_none h'] at hc; cases hc
    rw [envOf_src D i s hrs]
    rw [Array.getElem?_eq_getElem hrs] at hc
    rw [Option.some.inj hc]; rfl
  plain := by
    intro hside hW
    have h2 := h.2
    unfold exact at h2
    rw [getD_in (by rw [prep_side_size]; exact hr) false true, hside] at h2
    simp only [Bool.false_eq_true, ↓reduceIte] at h2
    rw [← h2, getD_bang hW]

theorem srcConst_some {i : RuntimeInput} {s : RuntimeState} {sd : SourceDesc} {n : Nat}
    (h : srcConst sd = some n) : srcNat i s sd = n ∧ srcSide sd = false := by
  cases sd <;> simp_all [srcConst, srcNat, srcSide]

theorem srcSide_const {sd : SourceDesc} (h : srcSide sd = true) : srcConst sd = none := by
  cases sd <;> simp_all [srcConst, srcSide]

theorem src_ok :
    let r0 := srcLoop (prepare D) i s 0 (prepare D).vals0 (prepare D).memv0
    r0.1.size = D.sources.size + D.nodes.size ∧ r0.2.size = D.sources.size + D.nodes.size ∧
    ∀ r < D.sources.size, SlotOK (prepare D) (envOf D i s) r0.1 r0.2 r := by
  have hv0 : (prepare D).vals0.size = D.sources.size + D.nodes.size := by simp [prepare]
  have hm0 : (prepare D).memv0.size = D.sources.size + D.nodes.size := by simp [prepare]
  have hidx : ∀ x ∈ (prepare D).srcIdx.toList, x < D.sources.size := by
    intro x hx; simp [prepare] at hx; exact hx.1
  obtain ⟨h1, h2, h3, h4⟩ := srcLoop_spec (prepare D) i s 0 (prepare D).vals0 (prepare D).memv0
    (fun x hx => by rw [hv0, hm0]; have := hidx x hx; omega)
  refine ⟨by rw [h1, hv0], by rw [h2, hm0], fun r hr => ?_⟩
  have hsd : sdOf (prepare D) r = D.sources[r] := by simp [sdOf, prepare, hr]
  have hmem : r ∈ (prepare D).srcIdx.toList.drop 0 ↔ srcConst (D.sources[r]) = none := by
    simp [prepare, hr]
  have hv0r : (prepare D).vals0.getD r 0 = (srcConst (D.sources[r])).getD 0 := by
    simp only [prepare, Array.getD_eq_getD_getElem?]
    rw [Array.getElem?_append_left (by simpa using hr)]
    simp [hr]
  have hsr : (prepare D).side.getD r false = srcSide (D.sources[r]) := by
    rw [getD_in (by rw [prep_side_size]; omega) false true, prep_side_src D hr]
  have hWr : (prepare D).W.getD r 0 = srcWidth (D.sources[r]) := prep_W_src D hr
  -- the value array and the side array at `r`
  have hval : srcSide (D.sources[r]) = false →
      (srcLoop (prepare D) i s 0 (prepare D).vals0 (prepare D).memv0).1.getD r 0 = srcNat i s (D.sources[r]) := by
    intro hside
    rw [h3, hsd, hside]
    cases hc : srcConst (D.sources[r]) with
    | none => rw [if_pos ⟨hmem.mpr hc, rfl⟩]
    | some n =>
      have hn : ¬ r ∈ (prepare D).srcIdx.toList.drop 0 := fun h => by rw [hmem.mp h] at hc; cases hc
      simp only [hn, false_and, ↓reduceIte, hv0r, hc, Option.getD_some]
      exact ((srcConst_some (i := i) (s := s) hc).1).symm
  have hval0 : srcSide (D.sources[r]) = true →
      (srcLoop (prepare D) i s 0 (prepare D).vals0 (prepare D).memv0).1.getD r 0 = 0 := by
    intro hside
    rw [h3, hsd, hside, if_neg (fun h => Bool.noConfusion h.2), hv0r, srcSide_const hside]
    rfl
  have hmv : srcSide (D.sources[r]) = true →
      (srcLoop (prepare D) i s 0 (prepare D).vals0 (prepare D).memv0).2.getD r (.bv (mk_bv 0 0)) =
        sourceValue i s (D.sources[r]) := by
    intro hside
    rw [h4, hsd, hside, if_pos ⟨hmem.mpr (srcSide_const hside), rfl⟩]
  unfold SlotOK
  rw [envOf_src D i s hr]
  cases hside : srcSide (D.sources[r])
  · rw [sourceValue_bv i s _ hside, hval hside]
    refine ⟨by simp only [CertVal.asBV]; rw [uintNat_ofNatLt (srcNat_lt i s _)], ?_⟩
    unfold exact
    rw [hsr, hside, hWr, hval hside]
    rfl
  · refine ⟨by rw [hval0 hside, sourceValue_side i s _ hside], ?_⟩
    unfold exact
    rw [hsr, hside]
    simp only [↓reduceIte]
    rw [getD_in (by rw [h2, hm0]; omega) _ (.bv (mk_bv 0 0)), hmv hside]

theorem exact_set_ne (P : Prep) (vals : Array Nat) (memv : Array CertVal) (k r : Nat) (x : Nat) (v : CertVal)
    (hk : k < vals.size) (hkm : k < memv.size) (hr : r ≠ k) :
    exact P (vals.set! k x) (memv.set! k v) r = exact P vals memv r ∧
    exact P (vals.set! k x) memv r = exact P vals memv r := by
  constructor
  · unfold exact; simp only [getD_set! _ _ _ _ _ hk, getD_set! _ _ _ _ _ hkm, hr, ↓reduceIte]
  · unfold exact; simp only [getD_set! _ _ _ _ _ hk, hr, ↓reduceIte]

theorem prep_D : (prepare D).D = D := rfl

theorem nodeLoop_ok (hdb : DesignCert.DepsBounded D) (j : Nat) (vals : Array Nat) (memv : Array CertVal)
    (hsz : vals.size = D.sources.size + D.nodes.size) (hmsz : memv.size = D.sources.size + D.nodes.size)
    (hok : ∀ r < D.sources.size + j, SlotOK (prepare D) (envOf D i s) vals memv r) :
    (nodeLoop (prepare D) j vals memv).1.size = D.sources.size + D.nodes.size ∧
    (nodeLoop (prepare D) j vals memv).2.size = D.sources.size + D.nodes.size ∧
    ∀ r < D.sources.size + D.nodes.size,
      SlotOK (prepare D) (envOf D i s) (nodeLoop (prepare D) j vals memv).1 (nodeLoop (prepare D) j vals memv).2 r := by
  unfold nodeLoop
  simp only [prep_D]
  split
  · rename_i hj
    have hjN : j < D.nodes.size := by rwa [prep_nodes_size] at hj
    have hslot : D.sources.size + j < vals.size := by omega
    have hslotm : D.sources.size + j < memv.size := by omega
    have hnode : (prepare D).nodes[j] = specialize D (prepare D).W (prepare D).side (D.nodes[j]) := by
      have := prep_node D hjN
      rwa [getElem!_pos (prepare D).nodes j hj] at this
    have hdeps : ∀ d ∈ (D.nodes[j]).deps.toList, d < D.sources.size + j :=
      fun d hd => hdb j (D.nodes[j]) (by simp [hjN]) d hd
    have hops : ∀ d ∈ (D.nodes[j]).deps.toList,
        OpdOK D (prepare D).W (prepare D).side vals (denoteRef (envOf D i s)) d :=
      fun d hd => opdOK_of D i s vals memv d (by have := hdeps d hd; omega) (hok d (hdeps d hd))
    have hsideF : (prepare D).side.getD (D.sources.size + j) false =
        (specialize D (prepare D).W (prepare D).side (D.nodes[j])).isGen := by
      rw [getD_in (by rw [prep_side_size]; omega) false true, prep_side_node D hdb hjN]
    have hW : (prepare D).W.getD (D.sources.size + j) 0 = (D.nodes[j]).width := prep_W_node D hjN
    -- every slot below `S + j` keeps its value when slot `S + j` is written
    have keep : ∀ (x : Nat) (v : CertVal) (r : Nat), r < D.sources.size + j →
        SlotOK (prepare D) (envOf D i s) (vals.set! (D.sources.size + j) x) (memv.set! (D.sources.size + j) v) r ∧
        SlotOK (prepare D) (envOf D i s) (vals.set! (D.sources.size + j) x) memv r := by
      intro x v r hr
      have hne : r ≠ D.sources.size + j := by omega
      have e := exact_set_ne (prepare D) vals memv _ r x v hslot hslotm hne
      have hv : (vals.set! (D.sources.size + j) x).getD r 0 = vals.getD r 0 := by
        rw [getD_set! _ _ _ _ _ hslot, if_neg hne]
      exact ⟨⟨by rw [hv]; exact (hok r hr).1, by rw [e.1]; exact (hok r hr).2⟩,
             ⟨by rw [hv]; exact (hok r hr).1, by rw [e.2]; exact (hok r hr).2⟩⟩
    split
    · rename_i c heq
      rw [hnode] at heq
      have hc := specialize_generic heq
      subst hc
      have hv : eval_op_cert (D.nodes[j]).op (D.nodes[j]).width
            ((D.nodes[j]).deps.toList.map (exact (prepare D) vals memv)) =
          denoteRef (envOf D i s) (D.sources.size + j) := by
        rw [envOf_node D i s hdb hjN]
        congr 1
        exact List.map_congr_left (fun d hd => (hok d (hdeps d hd)).2)
      rw [hv]
      apply nodeLoop_ok hdb (j + 1) _ _ (by simp [hsz]) (by simp [hmsz])
      intro r hr
      by_cases hrj : r = D.sources.size + j
      · subst hrj
        refine ⟨by rw [getD_set! _ _ _ _ _ hslot, if_pos rfl], ?_⟩
        unfold exact
        rw [hsideF, heq]
        simp only [SNode.isGen, ↓reduceIte]
        rw [getD_set! _ _ _ _ _ hslotm, if_pos rfl]
      · exact (keep _ _ r (by omega)).1
    · rename_i hng
      have hgen : ∀ c', specialize D (prepare D).W (prepare D).side (D.nodes[j]) ≠ .generic c' :=
        fun c' h => hng c' (by rw [hnode]; exact h)
      obtain ⟨hval, hlt⟩ := node_val (D.nodes[j]) hops hgen
      have hden : denoteRef (envOf D i s) (D.sources.size + j) =
          .bv (ofNatLt (D.nodes[j]).width ((specialize D (prepare D).W (prepare D).side (D.nodes[j])).val vals)) := by
        rw [envOf_node D i s hdb hjN, hval]
      have hisGen : (specialize D (prepare D).W (prepare D).side (D.nodes[j])).isGen = false := by
        cases hk : specialize D (prepare D).W (prepare D).side (D.nodes[j]) <;> simp_all [SNode.isGen]
      rw [hnode]
      apply nodeLoop_ok hdb (j + 1) _ _ (by simp [hsz]) hmsz
      intro r hr
      by_cases hrj : r = D.sources.size + j
      · subst hrj
        refine ⟨?_, ?_⟩
        · rw [getD_set! _ _ _ _ _ hslot, if_pos rfl, hden]
          show _ = uintNat (ofNatLt _ _)
          rw [uintNat_ofNatLt hlt]
        · unfold exact
          rw [hsideF, hisGen]
          simp only [Bool.false_eq_true, ↓reduceIte]
          rw [getD_set! _ _ _ _ _ hslot, if_pos rfl, hW, hden]
          rfl
      · exact (keep _ (.bv (mk_bv 0 0)) r (by omega)).2
  · rename_i hj
    have hjN : D.nodes.size ≤ j := by rw [prep_nodes_size] at hj; omega
    exact ⟨hsz, hmsz, fun r hr => hok r (by omega)⟩
termination_by D.nodes.size - j
decreasing_by all_goals simp at *; omega

end

/-! ## The staged step is the direct step -/

theorem run_ok (D : DesignCert) (hdb : DesignCert.DepsBounded D) (i : RuntimeInput) (s : RuntimeState) :
    ∀ r, (run (prepare D) i s).1.getD r 0 = uintNat (denoteRef (envOf D i s) r).asBV ∧
      exact (prepare D) (run (prepare D) i s).1 (run (prepare D) i s).2 r = denoteRef (envOf D i s) r := by
  obtain ⟨s1, s2, s3⟩ := src_ok D i s
  obtain ⟨n1, n2, n3⟩ := nodeLoop_ok D i s hdb 0 _ _ s1 s2 (fun r hr => s3 r (by omega))
  intro r
  unfold run
  simp only []
  by_cases hr : r < D.sources.size + D.nodes.size
  · exact n3 r hr
  · -- past the last slot both sides read the default
    have hbad : denoteRef (envOf D i s) r = badVal := by
      unfold denoteRef
      rw [Array.getElem?_eq_none (by rw [envOf_size]; omega)]
      rfl
    have hv : (nodeLoop (prepare D) 0 (srcLoop (prepare D) i s 0 (prepare D).vals0 (prepare D).memv0).1
        (srcLoop (prepare D) i s 0 (prepare D).vals0 (prepare D).memv0).2).1.getD r 0 = 0 := by
      rw [Array.getD_eq_getD_getElem?, Array.getElem?_eq_none (by rw [n1]; omega)]; rfl
    have hside : (prepare D).side.getD r false = false := by
      rw [Array.getD_eq_getD_getElem?, Array.getElem?_eq_none (by rw [prep_side_size]; omega)]; rfl
    have hW : (prepare D).W.getD r 0 = 0 := by
      rw [Array.getD_eq_getD_getElem?, Array.getElem?_eq_none (by rw [prep_W_size]; omega)]; rfl
    have h00 : mk_bv 0 0 = { width := 0, value := .ofNat 0 } := by decide
    refine ⟨?_, ?_⟩
    · rw [hv, hbad]; show 0 = uintNat (mk_bv 0 0); rw [h00]
      exact (uintNat_ofNatLt (w := 0) (n := 0) (by decide)).symm
    · unfold exact
      rw [hside, hW, hv, hbad]
      simp only [Bool.false_eq_true, ↓reduceIte, badVal, h00]

/-- **The staged step is the direct step**: for a certificate whose nodes read only earlier
slots, `step (prepare D)` returns exactly `directStepRaw D`'s outputs, flops and memories. -/
theorem step_eq (D : DesignCert) (hdb : DesignCert.DepsBounded D) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) : step (prepare D) e i s = directStepRaw D e i s := by
  have hok := run_ok D hdb i s
  have hrho : exact (prepare D) (run (prepare D) i s).1 (run (prepare D) i s).2 = denoteRef (envOf D i s) :=
    funext fun r => (hok r).2
  unfold step directStepRaw
  simp only [evalDense_eq_list]
  rw [hrho]
  simp only [prep_D]
  congr 2
  all_goals first
    | rw [srcMems_eq]
    | (funext o; rw [(hok o.slot).1, bv_resize_eq_ofNatLt])

/-- `directStepChecked`, staged: the same refusals, the same results. -/
theorem stepChecked_eq (D : DesignCert) (hdb : DesignCert.DepsBounded D) (e : ClockEdges)
    (i : RuntimeInput) (s : RuntimeState) :
    stepChecked (prepare D) e i s = directStepChecked D e i s := by
  unfold stepChecked directStepChecked
  simp only [prep_D]
  cases checkRuntime D e i s with
  | error err => rfl
  | ok u => simp only [step_eq D hdb]

#audit_axioms Compiler.Staged.step_eq
#audit_axioms Compiler.Staged.stepChecked_eq

end Staged
end Compiler
