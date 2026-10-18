/-
`SetMaskLoop` -- `bv_set_mask` as the BOUNDED positional loop the projection's
object program runs (D6 Tier 1, `Op_SetMask`).

`bv_set_mask w a m v` scatters the value's low bits into the accumulator at the
mask's set positions, in ascending order, and resizes the result to `w`. The
object program cannot iterate over the mask's set positions directly: the mask
is a RUNTIME value, so a walk bounded by its width would not unroll when the
interpreter is specialized. It walks the positions `0 … w-1` instead -- `w` is
the node width, which the certificate fixes -- and `set_mask_eq_smRun` proves
that loses nothing: a write at a position at or above `w` cannot reach the
`w`-bit result, whatever the operand widths.

Mathlib-free; core `Int`/`List` lemmas only.
-/
import LeanSemanticPrimitives.Translation.LGraphModel

namespace Projection
namespace SetMaskLoop

/-- One position of the scatter: when the mask's bit `j` is set, write the
value's next bit (`k`) into the accumulator at `j` and advance `k`. -/
def smStep (m v : BV) (st : BV × Nat) (j : Nat) : BV × Nat :=
  if bv_bit m j then (bv_set_bit st.1 j (bv_bit v st.2), st.2 + 1) else st

/-- The loop the object program runs: positions `i, i+1, …, i+n-1`. -/
def smRun (m v : BV) : Nat → Nat → BV → Nat → BV
  | 0,     _, acc, _ => acc
  | n + 1, i, acc, k =>
      if bv_bit m i then smRun m v n (i + 1) (bv_set_bit acc i (bv_bit v k)) (k + 1)
      else smRun m v n (i + 1) acc k

theorem smRun_foldl (m v : BV) :
    ∀ (n i : Nat) (acc : BV) (k : Nat),
      smRun m v n i acc k = ((List.range' i n).foldl (smStep m v) (acc, k)).1
  | 0,     _, _,   _ => rfl
  | n + 1, i, acc, k => by
      simp only [smRun, List.range'_succ, List.foldl_cons, smStep]
      split <;> exact smRun_foldl m v n (i + 1) _ _

/-- The reference's `zip (range L) idxs` fold IS the positional fold. -/
theorem zip_fold (m v : BV) :
    ∀ (is : List Nat) (acc : BV) (k : Nat),
      (List.zip (List.range' k (is.filter (bv_bit m)).length) (is.filter (bv_bit m))).foldl
          (fun acc p => bv_set_bit acc p.2 (bv_bit v p.1)) acc
        = (is.foldl (smStep m v) (acc, k)).1
  | [],      _,   _ => by simp
  | j :: is, acc, k => by
      by_cases h : bv_bit m j = true
      · simp only [List.filter_cons, h, if_true, List.length_cons, List.range'_succ,
          List.zip_cons_cons, List.foldl_cons, smStep]
        exact zip_fold m v is _ _
      · have h' : bv_bit m j = false := by simpa using h
        simp only [List.filter_cons, h', List.foldl_cons, smStep]
        simpa using zip_fold m v is acc k

theorem bv_uint_lt (x : BV) : bv_uint x < 2 ^ x.width := by
  unfold bv_uint
  exact Int.emod_lt_of_pos _ (Int.pow_pos (by decide))

theorem bv_uint_nonneg (x : BV) : 0 ≤ bv_uint x := by
  unfold bv_uint
  exact Int.emod_nonneg _ (Int.ne_of_gt (Int.pow_pos (by decide)))

/-- A bit at or beyond the width is clear. -/
theorem bv_bit_high (x : BV) {j : Nat} (hj : x.width ≤ j) : bv_bit x j = false := by
  have h0 := bv_uint_nonneg x
  have h1 := bv_uint_lt x
  have hlt : (bv_uint x).toNat < 2 ^ j := by
    have : (bv_uint x).toNat < 2 ^ x.width := by
      have e : ((2:Int) ^ x.width) = ((2 ^ x.width : Nat) : Int) := by simp [Int.natCast_pow]
      rw [e] at h1
      omega
    exact Nat.lt_of_lt_of_le this (Nat.pow_le_pow_right (by decide) hj)
  unfold bv_bit
  simp [Nat.shiftRight_eq_div_pow, Nat.div_eq_of_lt hlt]


theorem bv_uint_mk (aw : Nat) (z : Int) : bv_uint (mk_bv aw z) = z % 2 ^ aw := by
  simp [bv_uint, mk_bv]

theorem pow_split {w j : Nat} (h : w ≤ j) : (2:Int) ^ j = 2 ^ w * 2 ^ (j - w) := by
  rw [← Int.pow_add]; congr 1; omega

/-- Writing a bit at or above `w` leaves the low `w` bits alone. -/
theorem resize_set_bit_high (x : BV) {w j : Nat} (hj : w ≤ j) (b : Bool) :
    bv_resize w (bv_set_bit x j b) = bv_resize w x := by
  have hu0 := bv_uint_nonneg x
  have hu1 := bv_uint_lt x
  unfold bv_resize bv_set_bit
  simp only [bv_uint_mk]
  simp only [mk_bv, BV.mk.injEq, true_and]
  by_cases hw : x.width ≤ j
  · -- the bit is clear and 2^width divides 2^j: the write vanishes at the width
    have hb : bv_bit x j = false := bv_bit_high x hw
    have hsplit := pow_split hw
    cases b
    · simp [hb, Int.emod_eq_of_lt hu0 hu1]
    · simp only [hb, Bool.false_eq_true, ↓reduceIte]
      rw [hsplit, Int.add_mul_emod_self_left, Int.emod_eq_of_lt hu0 hu1]
  · -- 2^w divides 2^width, and the write is a multiple of 2^w
    have hdvd : (2:Int) ^ w ∣ 2 ^ x.width := ⟨2 ^ (x.width - w), pow_split (by omega)⟩
    rw [Int.emod_emod_of_dvd _ hdvd]
    have hsplit := pow_split hj
    cases b <;> by_cases hb : bv_bit x j = true
    · simp only [hb, Bool.false_eq_true, ↓reduceIte]
      rw [hsplit, Int.sub_eq_add_neg, ← Int.mul_neg, Int.add_mul_emod_self_left]
    · simp [hb]
    · simp [hb]
    · have hb' : bv_bit x j = false := by simpa using hb
      simp only [hb', Bool.false_eq_true, ↓reduceIte]
      rw [hsplit, Int.add_mul_emod_self_left]

theorem fold_high_id (m v : BV) :
    ∀ (is : List Nat) (st : BV × Nat), (∀ j ∈ is, bv_bit m j = false) →
      is.foldl (smStep m v) st = st
  | [],      _,  _ => rfl
  | j :: is, st, h => by
      simp only [List.foldl_cons, smStep, h j List.mem_cons_self]
      exact fold_high_id m v is st (fun x hx => h x (List.mem_cons_of_mem _ hx))

theorem fold_resize_inv (m v : BV) (w : Nat) :
    ∀ (is : List Nat) (st : BV × Nat), (∀ j ∈ is, w ≤ j) →
      bv_resize w (is.foldl (smStep m v) st).1 = bv_resize w st.1
  | [],      _,  _ => rfl
  | j :: is, st, h => by
      simp only [List.foldl_cons]
      rw [fold_resize_inv m v w is _ (fun x hx => h x (List.mem_cons_of_mem _ hx))]
      unfold smStep
      split
      · exact resize_set_bit_high _ (h j List.mem_cons_self) _
      · rfl

/-- **`bv_set_mask` is the bounded positional loop.**  Only positions below the
node width `w` can reach the result, so the object program walks exactly
`0 … w-1`, a STATIC bound, whatever the mask's own width. -/
theorem set_mask_eq_smRun (w : Nat) (a m v : BV) :
    bv_set_mask w a m v = bv_resize w (smRun m v w 0 a 0) := by
  have hz := zip_fold m v (List.range' 0 m.width) a 0
  unfold bv_set_mask mask_indices_bv
  simp only [List.range_eq_range'] at *
  rw [hz, smRun_foldl]
  -- extend the mask's positions by `w` clear bits, then split at `w`
  have hext : (List.range' 0 m.width).foldl (smStep m v) (a, 0)
      = (List.range' 0 (m.width + w)).foldl (smStep m v) (a, 0) := by
    rw [← List.range'_append_1, List.foldl_append,
      fold_high_id m v (List.range' (0 + m.width) w) _ ?_]
    intro j hj
    exact bv_bit_high m (by rw [List.mem_range'_1] at hj; omega)
  rw [hext, show m.width + w = w + m.width by omega, ← List.range'_append_1, List.foldl_append,
    fold_resize_inv m v w]
  intro j hj
  rw [List.mem_range'_1] at hj
  omega

end SetMaskLoop
end Projection
