/-
# Whole-array memory operators on the ACTUAL `hwMemS` (decision #2)

`Op_MemLoad n` loads entries `0 .. n-1` of an image from one bus when its enable
is nonzero (`cert_mem_load`), and `Op_MemToBus n` packs entries `0 .. n-1` into
one bus (`cert_mem_to_bus`). `hwMemS` computes them with five small functions
(`HardwareMem`): `pow2O`, `loadW`, `memLoadO`, `packW`, `memToBusO`.

* §1 states the image-level operations the functions compute (`loadL`,
  `memLoadImg`, `packL`, `memToBusBV`) and proves each equal to the reference:
  `denote_memLoadImg` (given `n ≤ words.length`: a finite table cannot load an
  entry it does not hold, the same contract a write has) and
  `memToBusBV_eq` (no premise: a missing word reads the default on both sides).
* §2 proves, per function, that `SEval` returns exactly that value.
* §3 composes them with `applyOpM`'s dispatch (opcodes 29 and 30) and the slot
  reads, as `HardwareMemOp` does for the other memory operators.
-/
import LeanSemanticPrimitives.Projection.HardwareMemOp
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. The image-level operations, and the reference -/

/-- The loaded word list: from index `i`, while `i < n`, word `i` is the low `dw`
bits of `rest`, and `rest` moves on by `p`; from `n` on the list is unchanged.
This is `loadW`'s recursion, on the list. -/
def loadL (dw : Nat) (p : Int) (n : Nat) : List BV → Int → Nat → List BV
  | [],     _,    _ => []
  | w :: t, rest, i => if i < n then mk_bv dw rest :: loadL dw p n t (rest / p) (i + 1) else w :: t

theorem loadL_length (dw : Nat) (p : Int) (n : Nat) :
    ∀ (ws : List BV) (rest : Int) (i : Nat), (loadL dw p n ws rest i).length = ws.length
  | [],     _,    _ => rfl
  | w :: t, rest, i => by
      unfold loadL
      split
      · simp [loadL_length dw p n t]
      · rfl

/-- Word `k` of the loaded list: loaded while `i + k < n`, unchanged after. -/
theorem loadL_get (dw : Nat) {p : Int} (hp : 0 ≤ p) (n : Nat) :
    ∀ (ws : List BV) (rest : Int) (i k : Nat),
      (loadL dw p n ws rest i)[k]? =
        if i + k < n then (ws[k]?).map (fun _ => mk_bv dw (rest / p ^ k)) else ws[k]?
  | [],     _,    i, k => by simp [loadL]
  | w :: t, rest, i, k => by
      unfold loadL
      by_cases hi : i < n
      · rw [if_pos hi]
        cases k with
        | zero => simp [hi]
        | succ k =>
            simp only [List.getElem?_cons_succ]
            rw [loadL_get dw hp n t (rest / p) (i + 1) k]
            have hk : i + 1 + k = i + (k + 1) := by omega
            rw [hk, Int.ediv_ediv_of_nonneg hp, Int.pow_succ, Int.mul_comm (p ^ k) p]
      · rw [if_neg hi, if_neg (by omega)]

/-- The enabled load, as a table: words `0 .. n-1` from the bus, the rest and
the default unchanged. -/
def loadImg (w n : Nat) (m : MemImg) (bus : BV) : MemImg :=
  ⟨loadL w (2 ^ w) n m.words (bv_uint bus) 0, m.dflt⟩

/-- `Op_MemLoad n`, as a table: the load when enabled, the image itself when not. -/
def memLoadImg (w n : Nat) (m : MemImg) (bus en : BV) : MemImg :=
  if bv_nonzero en then loadImg w n m bus else m

theorem memLoadImg_length (w n : Nat) (m : MemImg) (bus en : BV) :
    (memLoadImg w n m bus en).words.length = m.words.length := by
  unfold memLoadImg loadImg
  split
  · exact loadL_length _ _ _ _ _ _
  · rfl

/-- **The load is faithful, where the table holds every loaded entry.** The
premise is the contract a write has (`InRange`): a finite table cannot hold an
entry past its end. -/
theorem denote_memLoadImg (w n : Nat) (m : MemImg) (bus en : BV)
    (hn : n ≤ m.words.length) :
    MemImg.denote (memLoadImg w n m bus en) = cert_mem_load w n (MemImg.denote m) bus en := by
  unfold memLoadImg cert_mem_load
  by_cases hen : bv_nonzero en
  · rw [if_pos hen, if_pos hen]
    funext x
    have hp : (0 : Int) ≤ 2 ^ w := Int.le_of_lt (Int.pow_pos (by decide))
    simp only [MemImg.denote_eq_get, MemImg.get, loadImg]
    by_cases hx0 : 0 ≤ x
    · rw [if_pos hx0, if_pos hx0, loadL_get w hp n m.words (bv_uint bus) 0 x.toNat, Nat.zero_add]
      by_cases hxn : x < n
      · rw [if_pos (show x.toNat < n by omega), if_pos ⟨hx0, hxn⟩,
          List.getElem?_eq_getElem (show x.toNat < m.words.length by omega)]
        simp only [Option.map_some, Option.getD_some]
        rw [← Int.pow_mul, Nat.mul_comm w]
      · rw [if_neg (show ¬ x.toNat < n by omega), if_neg (fun h => hxn h.2)]
    · rw [if_neg hx0, if_neg (fun h => hx0 h.1), if_neg hx0]
  · rw [if_neg hen, if_neg hen]

/-- The packed bus from index `i`: word `i` (the default once the list is spent),
cut by `p`, plus `p` times the rest. This is `packW`'s recursion. -/
def packL (df : BV) (p : Int) (n : Nat) (ws : List BV) (i : Nat) : Int :=
  if i < n then
    match ws with
    | []     => bv_uint df % p + p * packL df p n [] (i + 1)
    | w :: t => bv_uint w % p + p * packL df p n t (i + 1)
  else 0
termination_by n - i

/-- One step of `packL`, with the word read through `getD`. -/
theorem packL_step (df : BV) (p : Int) (n : Nat) (ws : List BV) (i : Nat) (hi : i < n) :
    packL df p n ws i = bv_uint (ws.headD df) % p + p * packL df p n ws.tail (i + 1) := by
  rw [packL.eq_def, if_pos hi]
  cases ws <;> rfl

theorem packL_ge (df : BV) (p : Int) (n : Nat) (ws : List BV) (i : Nat) (hi : ¬ i < n) :
    packL df p n ws i = 0 := by
  rw [packL.eq_def, if_neg hi]

/-- `Op_MemToBus n` on a table: entries `0 .. n-1`, each `w / n` bits. -/
def memToBusBV (w n : Nat) (m : MemImg) : BV :=
  mk_bv w (packL m.dflt (2 ^ (w / n)) n m.words 0)

/-- The certificate's left fold, from `j`, against the table's packing from `j`. -/
theorem certPack_split (d : Nat) (m : MemImg) (n : Nat) :
    ∀ (r j : Nat), j + r = n →
      cert_mem_pack d (MemImg.denote m) n
        = cert_mem_pack d (MemImg.denote m) j
          + 2 ^ (j * d) * packL m.dflt (2 ^ d) n (m.words.drop j) j
  | 0, j, h => by
      subst h
      rw [packL_ge _ _ _ _ _ (by omega)]
      simp
  | r + 1, j, h => by
      have hj : j < n := by omega
      rw [certPack_split d m n r (j + 1) (by omega), packL_step _ _ _ _ _ hj]
      have hhead : (m.words.drop j).headD m.dflt = MemImg.denote m (j : Int) := by
        simp only [MemImg.denote_eq_get, MemImg.get, if_pos (Int.natCast_nonneg j),
          Int.toNat_natCast]
        have h0 : (m.words.drop j)[0]? = m.words[j]? := by simp [List.getElem?_drop]
        rw [← h0]
        cases (m.words.drop j) <;> rfl
      have htail : (m.words.drop j).tail = m.words.drop (j + 1) := by
        rw [← List.drop_one, List.drop_drop, Nat.add_comm]
      rw [hhead, htail]
      simp only [cert_mem_pack]
      rw [show (j + 1) * d = j * d + d by rw [Nat.add_mul, Nat.one_mul], Int.pow_add]
      simp only [Int.mul_add, Int.add_mul, Int.mul_assoc, Int.add_assoc]
      rw [Int.mul_comm (bv_uint (MemImg.denote m ↑j) % 2 ^ d) (2 ^ (j * d))]

/-- **The packing is the reference's**, with no premise: a word the table does
not hold reads the default on both sides. -/
theorem memToBusBV_eq (w n : Nat) (m : MemImg) :
    memToBusBV w n m = cert_mem_to_bus w n (MemImg.denote m) := by
  unfold memToBusBV cert_mem_to_bus
  rw [certPack_split (w / n) m n n 0 (by omega)]
  simp [cert_mem_pack]

/-! ## 2. Each object function computes its mirror -/

theorem pow2_agree : ∀ (k : Nat) {σ : SEnv} {ek : SExp},
    SEval hwMemS σ ek (.int (Int.ofNat k)) →
    SEval hwMemS σ (.call "pow2O" [ek]) (.int ((2 : Int) ^ k))
  | 0, _, _, hk => by
      refine SEval_call1 hk rfl rfl ?_
      exact SEval.iteT (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim])) .lit
  | k + 1, _, _, hk => by
      refine SEval_call1 hk rfl rfl ?_
      refine SEval.iteF (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim])) ?_
      refine SEval_prim2 .lit (pow2_agree k (SEval_prim2 (.ref rfl) .lit ?_)) ?_
      · simp [evalPrim]
      · simp [evalPrim, Int.pow_succ, Int.mul_comm]

theorem loadW_agree (dw : Nat) (p : Int) (n : Nat) :
    ∀ (ws : List BV) (rest : Int) (i : Nat) {σ : SEnv} {e1 e2 e3 e4 e5 e6 : SExp},
      SEval hwMemS σ e1 (encListG ofBV ws) → SEval hwMemS σ e2 (.int rest) →
      SEval hwMemS σ e3 (.int (Int.ofNat i)) → SEval hwMemS σ e4 (.int (Int.ofNat n)) →
      SEval hwMemS σ e5 (.int (Int.ofNat dw)) → SEval hwMemS σ e6 (.int p) →
      SEval hwMemS σ (.call "loadW" [e1, e2, e3, e4, e5, e6])
        (encListG ofBV (loadL dw p n ws rest i))
  | [], rest, i, _, _, _, _, _, _, _, h1, h2, h3, h4, h5, h6 => by
      refine SEval_call6 h1 h2 h3 h4 h5 h6 rfl rfl ?_
      by_cases hi : i < n
      · refine SEval.iteT (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim]; omega)) ?_
        exact SEval.iteT (SEval_isNil_nil (.ref rfl)) .lit
      · refine SEval.iteF (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim]; omega)) ?_
        exact .ref rfl
  | w :: t, rest, i, _, _, _, _, _, _, _, h1, h2, h3, h4, h5, h6 => by
      refine SEval_call6 h1 h2 h3 h4 h5 h6 rfl rfl ?_
      by_cases hi : i < n
      · refine SEval.iteT (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim]; omega)) ?_
        refine SEval.iteF (SEval_isNil_cons (.ref rfl)) ?_
        have hl : loadL dw p n (w :: t) rest i
            = mk_bv dw rest :: loadL dw p n t (rest / p) (i + 1) := by
          simp [loadL, hi]
        rw [hl]
        refine SEval_consP (SEval_prim2 (.ref rfl) (.ref rfl) ?_) ?_
        · simp [evalPrim, widthOf]
        · refine loadW_agree dw p n t (rest / p) (i + 1)
            (SEval_tl (.ref rfl)) (SEval_prim2 (.ref rfl) (.ref rfl) ?_)
            (SEval_prim2 (.ref rfl) .lit ?_) (.ref rfl) (.ref rfl) (.ref rfl)
          · simp only [evalPrim]
            by_cases hp : p = 0 <;> simp [hp]
          · simp [evalPrim]
      · refine SEval.iteF (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim]; omega)) ?_
        have hl : loadL dw p n (w :: t) rest i = w :: t := by simp [loadL, hi]
        rw [hl]
        exact .ref rfl

theorem packW_agree (df : BV) {p : Int} (hp : p ≠ 0) (n : Nat) :
    ∀ (r : Nat) (ws : List BV) (i : Nat), i + r = n → ∀ {σ : SEnv} {e1 e2 e3 e4 e5 : SExp},
      SEval hwMemS σ e1 (encListG ofBV ws) → SEval hwMemS σ e2 (ofBV df) →
      SEval hwMemS σ e3 (.int (Int.ofNat i)) → SEval hwMemS σ e4 (.int (Int.ofNat n)) →
      SEval hwMemS σ e5 (.int p) →
      SEval hwMemS σ (.call "packW" [e1, e2, e3, e4, e5]) (.int (packL df p n ws i))
  | 0, ws, i, hr, _, _, _, _, _, _, h1, h2, h3, h4, h5 => by
      refine SEval_call5 h1 h2 h3 h4 h5 rfl rfl ?_
      rw [packL_ge _ _ _ _ _ (by omega)]
      exact SEval.iteF (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim]; omega)) .lit
  | r + 1, ws, i, hr, _, _, _, _, _, _, h1, h2, h3, h4, h5 => by
      refine SEval_call5 h1 h2 h3 h4 h5 rfl rfl ?_
      have hi : i < n := by omega
      rw [packL_step _ _ _ _ _ hi]
      refine SEval.iteT (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim]; omega)) ?_
      have hi1 : SEval hwMemS ((["ws", "df", "i", "n", "p"].zip
          [encListG ofBV ws, ofBV df, Val.int (Int.ofNat i), Val.int (Int.ofNat n), Val.int p]))
          (.prim .addI [.ref "i", int 1]) (.int (Int.ofNat (i + 1))) :=
        SEval_prim2 (.ref rfl) .lit (by simp [evalPrim])
      cases ws with
      | nil =>
          refine SEval.iteT (SEval_isNil_nil (.ref rfl)) ?_
          refine SEval_prim2 (a := .int (bv_uint df % p)) (b := .int (p * packL df p n [] (i + 1)))
            (SEval_prim2 (a := .int (bv_uint df)) (SEval_prim1 (.ref rfl) (by simp [evalPrim]))
              (.ref rfl) (by simp [evalPrim, hp]))
            (SEval_prim2 (.ref rfl)
              (packW_agree df hp n r [] (i + 1) (by omega) (.ref rfl) (.ref rfl) hi1 (.ref rfl)
                (.ref rfl)) (by simp [evalPrim])) ?_
          simp [evalPrim]
      | cons w t =>
          refine SEval.iteF (SEval_isNil_cons (.ref rfl)) ?_
          refine SEval_prim2 (a := .int (bv_uint w % p)) (b := .int (p * packL df p n t (i + 1)))
            (SEval_prim2 (a := .int (bv_uint w)) (SEval_prim1 (SEval_hd (.ref rfl))
              (by simp [evalPrim])) (.ref rfl) (by simp [evalPrim, hp]))
            (SEval_prim2 (.ref rfl)
              (packW_agree df hp n r t (i + 1) (by omega) (SEval_tl (.ref rfl)) (.ref rfl) hi1
                (.ref rfl) (.ref rfl)) (by simp [evalPrim])) ?_
          simp [evalPrim]

theorem memLoadO_agree (w n : Nat) (m : MemImg) (bus en : BV) {σ : SEnv}
    {e1 e2 e3 e4 e5 : SExp}
    (h1 : SEval hwMemS σ e1 (.int (Int.ofNat w))) (h2 : SEval hwMemS σ e2 (.int (Int.ofNat n)))
    (h3 : SEval hwMemS σ e3 (MemImg.encMemImg m)) (h4 : SEval hwMemS σ e4 (encBV bus))
    (h5 : SEval hwMemS σ e5 (encBV en)) :
    SEval hwMemS σ (.call "memLoadO" [e1, e2, e3, e4, e5])
      (MemImg.encMemImg (memLoadImg w n m bus en)) := by
  refine SEval_call5 h1 h2 h3 h4 h5 rfl rfl ?_
  have hnz := lift (nz_agree (σ := ["w", "n", "img", "bus", "en"].zip
      [Val.int (Int.ofNat w), Val.int (Int.ofNat n), MemImg.encMemImg m, encBV bus, encBV en])
      (ev := .ref "en") (.ref rfl))
  unfold memLoadImg
  by_cases hen : bv_nonzero en
  · rw [if_pos hen]
    rw [hen] at hnz
    refine SEval.iteT hnz ?_
    refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
    refine SEval.mk (.cons ?_ (.cons (.ref rfl) .nil))
    exact loadW_agree w ((2 : Int) ^ w) n m.words (bv_uint bus) 0 (.ref rfl)
      (SEval_prim1 (.ref rfl) (by simp [evalPrim])) .lit (.ref rfl) (.ref rfl)
      (pow2_agree w (.ref rfl))
  · rw [if_neg hen]
    have hf : bv_nonzero en = false := by simpa using hen
    rw [hf] at hnz
    exact SEval.iteF hnz (.ref rfl)

theorem memToBusO_agree (w n : Nat) (m : MemImg) {σ : SEnv} {e1 e2 e3 : SExp}
    (h1 : SEval hwMemS σ e1 (.int (Int.ofNat w))) (h2 : SEval hwMemS σ e2 (.int (Int.ofNat n)))
    (h3 : SEval hwMemS σ e3 (MemImg.encMemImg m)) :
    SEval hwMemS σ (.call "memToBusO" [e1, e2, e3]) (encBV (memToBusBV w n m)) := by
  refine SEval_call3 h1 h2 h3 rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  have hp : ((2 : Int) ^ (w / n)) ≠ 0 := Int.ne_of_gt (Int.pow_pos (by decide))
  have hd : SEval hwMemS ((["ws", "df"].zip [encListG ofBV m.words, ofBV m.dflt]) ++
      (["w", "n", "img"].zip [Val.int (Int.ofNat w), Val.int (Int.ofNat n), MemImg.encMemImg m]))
      (.prim .divI [.ref "w", .ref "n"]) (.int (Int.ofNat (w / n))) := by
    refine SEval_prim2 (.ref rfl) (.ref rfl) ?_
    simp only [evalPrim]
    by_cases hn : n = 0
    · subst hn; simp
    · have h0 : ¬ (Int.ofNat n = 0) := fun h => hn (Int.ofNat.inj h)
      rw [if_neg h0]
      exact congrArg (fun x => Except.ok (Val.int x)) (Int.ofNat_ediv_ofNat (a := w) (b := n))
  refine SEval_prim2 (.ref rfl)
    (packW_agree m.dflt hp n n m.words 0 (by omega) (.ref rfl) (.ref rfl) .lit (.ref rfl)
      (pow2_agree (w / n) hd)) ?_
  simp [evalPrim, widthOf, memToBusBV, encBV]

/-! ## 3. Through `applyOpM`'s dispatch -/

/-- `Op_MemLoad k` (opcode 29): the slot reads in dependency order, the node
width and the payload `k`, then `memLoadO`. -/
theorem applyOpM_memLoad {vals : List Val} {σ : SEnv} {eo ew ed ee en : SExp}
    {w k d0 d1 d2 : Nat} {m : MemImg} {bus enb : BV}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length) (hb2 : d2 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV bus)
    (hv2 : vals[d2]'hb2 = encBV enb)
    (hop : SEval hwMemS σ eo (encOp (.Op_MemLoad k)))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat [d0, d1, d2]))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en])
      (MemImg.encMemImg (memLoadImg w k m bus enb)) := by
  refine SEval_call5 hop hw hdv henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
  refine SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl) ?_
  have hv1' : vals[d1]'hb1 = ofBV bus := hv1
  have hv2' : vals[d2]'hb2 = ofBV enb := hv2
  refine memLoadO_agree w k m bus enb (.ref rfl) (.ref rfl) ?_ ?_ ?_
  · rw [← hv0]; exact lift (slot_agree hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
  · rw [← hv1]
    exact lift (slot_agree hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl))))
  · rw [← hv2]
    exact lift (slot_agree hb2 (.ref rfl) (.ref rfl)
      (SEval_hd (SEval_tl (SEval_tl (.ref rfl)))))

/-- `Op_MemToBus k` (opcode 30): the image slot, the node width and `k`, then
`memToBusO`. -/
theorem applyOpM_memToBus {vals : List Val} {σ : SEnv} {eo ew ed ee en : SExp}
    {w k d0 : Nat} {m : MemImg}
    (hb0 : d0 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hop : SEval hwMemS σ eo (encOp (.Op_MemToBus k)))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat [d0]))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en]) (encBV (memToBusBV w k m)) := by
  refine SEval_call5 hop hw hdv henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl) ?_
  refine SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl) ?_
  refine memToBusO_agree w k m (.ref rfl) (.ref rfl) ?_
  rw [← hv0]; exact lift (slot_agree hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))

/-- …against the reference's single `eval_op_cert`. The load carries the table
contract `k ≤ words.length`. -/
theorem applyOpM_memLoad_cert {vals : List Val} {σ : SEnv} {eo ew ed ee en : SExp}
    {w k d0 d1 d2 : Nat} {m : MemImg} {bus enb : BV}
    (hb0 : d0 < vals.length) (hb1 : d1 < vals.length) (hb2 : d2 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hv1 : vals[d1]'hb1 = encBV bus)
    (hv2 : vals[d2]'hb2 = encBV enb)
    (hk : k ≤ m.words.length)
    (hop : SEval hwMemS σ eo (encOp (.Op_MemLoad k)))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat [d0, d1, d2]))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    ∃ v, SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en]) v
       ∧ SrcRel v (eval_op_cert (.Op_MemLoad k) w [.mem (MemImg.denote m), .bv bus, .bv enb]) :=
  ⟨_, applyOpM_memLoad hb0 hb1 hb2 hv0 hv1 hv2 hop hw hdv henv hlen,
   memLoadImg w k m bus enb, rfl, denote_memLoadImg w k m bus enb hk⟩

theorem applyOpM_memToBus_cert {vals : List Val} {σ : SEnv} {eo ew ed ee en : SExp}
    {w k d0 : Nat} {m : MemImg}
    (hb0 : d0 < vals.length)
    (hv0 : vals[d0]'hb0 = MemImg.encMemImg m)
    (hop : SEval hwMemS σ eo (encOp (.Op_MemToBus k)))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat [d0]))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    ∃ v, SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en]) v
       ∧ SrcRel v (eval_op_cert (.Op_MemToBus k) w [.mem (MemImg.denote m)]) :=
  ⟨_, applyOpM_memToBus hb0 hv0 hop hw hdv henv hlen, by
    show encBV (memToBusBV w k m) = encBV (cert_mem_to_bus w k (MemImg.denote m))
    rw [memToBusBV_eq]⟩

/-! ## 4. Execution -/

private def applyOpMIdL : Nat := (idOf "applyOpM").getD 999

/- Op_MemLoad 3 (code 29) on a four-entry image, enabled: entries 0..2 from the
bus 0x040302_01's low 24 bits, entry 3 unchanged -/
#guard (match evalFuel 40000 hwMemP []
          (.call applyOpMIdL
             [ .lit (.ctor tagOp [.int 29, .int 3]), .lit (.int 8)
             , .lit (encListG encNat [0, 1, 2])
             , .lit (.cons (encBV (mk_bv 1 1)) (.cons (encBV (mk_bv 32 0x04030201))
                 (.cons (MemImg.encMemImg ⟨[mk_bv 8 9, mk_bv 8 9, mk_bv 8 9, mk_bv 8 9], mk_bv 8 0⟩)
                   .nil)))
             , .lit (.int 3) ]) with
        | .value v => v == MemImg.encMemImg ⟨[mk_bv 8 1, mk_bv 8 2, mk_bv 8 3, mk_bv 8 9], mk_bv 8 0⟩
        | _ => false)

/- Op_MemToBus 3 (code 30): entries 0..2 of a four-entry image as one 24-bit bus -/
#guard (match evalFuel 40000 hwMemP []
          (.call applyOpMIdL
             [ .lit (.ctor tagOp [.int 30, .int 3]), .lit (.int 24)
             , .lit (encListG encNat [0])
             , .lit (.cons (MemImg.encMemImg ⟨[mk_bv 8 1, mk_bv 8 2, mk_bv 8 3, mk_bv 8 4], mk_bv 8 0⟩)
                 .nil)
             , .lit (.int 1) ]) with
        | .value v => v == encBV (mk_bv 24 0x030201)
        | _ => false)

/-! ## 5. Axiom audit -/

#audit_axioms loadL_get
#audit_axioms denote_memLoadImg
#audit_axioms certPack_split
#audit_axioms memToBusBV_eq
#audit_axioms pow2_agree
#audit_axioms loadW_agree
#audit_axioms packW_agree
#audit_axioms memLoadO_agree
#audit_axioms memToBusO_agree
#audit_axioms applyOpM_memLoad
#audit_axioms applyOpM_memToBus
#audit_axioms applyOpM_memLoad_cert
#audit_axioms applyOpM_memToBus_cert

end HwMem
end Projection
