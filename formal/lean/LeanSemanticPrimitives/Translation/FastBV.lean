/-
# Proved fast paths for the `BV` primitives

`BV` keeps its value as an `Int` and reduces it with `% 2 ^ width` on every operation.
Lean v4.31 computes `2 ^ w` through GMP on every call (`lean_nat_pow` has no scalar path),
and an `Int` at or above `2 ^ 31` is a GMP object. Bitwise operators are written bit by bit
(`bits_to_int` over `List.range`, with `2 ^ i` per bit). So one 32-bit AND costs about 30 µs,
and the IR-semantics and TV simulators spend nearly all their time in GMP and `malloc`.

This module gives each hot primitive `f` a fast form `fFast` and a theorem
`@[csimp] f_eq_fast : @f = @fFast`. The compiler then calls `fFast` wherever code compiled
AFTER this module (in a module that imports it) calls `f`. The theorems are checked, so the
substitution changes no result; nothing here is `@[implemented_by]`. The definitions in
`LGraphModel` are untouched, and so is every proof about them.

The fast forms work on the unsigned value as a `Nat` (`uintNat`), take masks from a table
built once (`maskTab`), and use word operations (`&&&`, `|||`, `^^^`, shifts) that Lean
inlines for values below `2 ^ 63`. Widths of 63 and more fall back to the definition.

A `@[csimp]` lemma reaches only call sites compiled after it, so the modules whose code runs
per simulated cycle import this one: `Compiler/ResidualSemantics` (TV's step and the
compiled IR mode) and `Compiler/DirectSemantics` (the raw IR mode). This module must not
import Mathlib: the simulators' native builds compile every module they import.
-/
import LeanSemanticPrimitives.Translation.LGraphModel

namespace FastBV

/-! ## Tables -/

/-- `2 ^ w - 1` for `w < 63`; every entry is an unboxed `Nat`. -/
def maskTab : Array Nat := (Array.range 63).map fun w => 2 ^ w - 1

/-- `2 ^ w` for `w < 63`. -/
def pow2Tab : Array Nat := (Array.range 63).map fun w => 2 ^ w

theorem maskTab_size : maskTab.size = 63 := by simp [maskTab]
theorem pow2Tab_size : pow2Tab.size = 63 := by simp [pow2Tab]

theorem maskTab_get (w : Nat) (h : w < maskTab.size) : maskTab[w] = 2 ^ w - 1 := by
  simp [maskTab]

theorem pow2Tab_get (w : Nat) (h : w < pow2Tab.size) : pow2Tab[w] = 2 ^ w := by
  simp [pow2Tab]

theorem and_maskTab (x w : Nat) (h : w < maskTab.size) : x &&& maskTab[w] = x % 2 ^ w := by
  rw [maskTab_get w h, Nat.and_two_pow_sub_one_eq_mod]

theorem two_pow_int (w : Nat) : (2 : Int) ^ w = ((2 ^ w : Nat) : Int) := (Int.natCast_pow 2 w).symm

/-- `2 ^ w` for `w ≤ 128`. The entries from `2 ^ 63` up are big numbers, but they are built
once, so a wide value (a 64-bit counter) never computes a power at run time. -/
def pow2Big : Array Nat := (Array.range 129).map fun w => 2 ^ w

theorem pow2Big_get (w : Nat) (h : w < pow2Big.size) : pow2Big[w] = 2 ^ w := by
  simp [pow2Big]

/-- `2 ^ w`, from the table when it has `w`. -/
@[inline] def pow2Of (w : Nat) : Nat := if h : w < pow2Big.size then pow2Big[w] else 2 ^ w

theorem pow2Of_eq (w : Nat) : pow2Of w = 2 ^ w := by
  unfold pow2Of; split
  · rename_i h; exact pow2Big_get w h
  · rfl

/-- `x % 2 ^ w` without computing `2 ^ w`: a table mask below 63 bits; up to 128 bits, a
comparison with the tabled power (a scalar `x` is always below `2 ^ 63`), and the
remainder only when `x` really is wider. -/
@[inline] def modPow2 (w x : Nat) : Nat :=
  if h : w < maskTab.size then x &&& maskTab[w]
  else if h2 : w < pow2Big.size then (if x < pow2Big[w] then x else x % pow2Big[w])
  else x % 2 ^ w

theorem modPow2_eq (w x : Nat) : modPow2 w x = x % 2 ^ w := by
  unfold modPow2
  split
  · rename_i h; exact and_maskTab x w h
  · split
    · rename_i h2
      rw [pow2Big_get _ h2]
      split
      · rename_i hx; exact (Nat.mod_eq_of_lt hx).symm
      · rfl
    · rfl

/-- `2 ^ w - 1`, from a table when it has `w`. -/
@[inline] def maskOf (w : Nat) : Nat := if h : w < maskTab.size then maskTab[w] else pow2Of w - 1

theorem maskOf_eq (w : Nat) : maskOf w = 2 ^ w - 1 := by
  unfold maskOf; split
  · rename_i h; exact maskTab_get w h
  · rw [pow2Of_eq]

/-! ## Reduction modulo `2 ^ w` -/

/-- `(v % 2 ^ w).toNat` with no `2 ^ w` computed at run time: the reduced value for a
nonnegative `v`, and `2 ^ w - 1` minus the reduced complement for a negative one. -/
@[inline] def modNat (w : Nat) (v : Int) : Nat :=
  match v with
  | .ofNat n => modPow2 w n
  | .negSucc n => pow2Of w - 1 - modPow2 w n

theorem emod_two_pow_eq (w : Nat) (v : Int) : v % (2 ^ w : Int) = ((modNat w v : Nat) : Int) := by
  rw [two_pow_int]
  cases v with
  | ofNat n =>
    simp only [modNat, modPow2_eq]
    rfl
  | negSucc n =>
    simp only [modNat, modPow2_eq, pow2Of_eq]
    have hm : n % 2 ^ w < 2 ^ w := Nat.mod_lt _ (Nat.two_pow_pos w)
    rw [Int.negSucc_emod_ofNat, Int.subNatNat_of_le (by omega)]
    congr 1
    omega

theorem modNat_lt (w : Nat) (v : Int) : modNat w v < 2 ^ w := by
  have h := emod_two_pow_eq w v
  have hlt : v % (2 ^ w : Int) < (2 ^ w : Int) :=
    Int.emod_lt_of_pos _ (by rw [two_pow_int]; exact Int.natCast_pos.mpr (Nat.two_pow_pos w))
  rw [h, two_pow_int] at hlt
  exact Int.ofNat_lt.mp hlt

/-! ## `bv_uint`, `mk_bv`, `bv_resize`, `bv_nonzero`, `bv_bit` -/

/-- The unsigned value of a `BV` as a `Nat`: `(bv_uint x).toNat`. -/
@[inline] def uintNat (x : BV) : Nat := modNat x.width x.value

theorem bv_uint_eq_uintNat (x : BV) : bv_uint x = ((uintNat x : Nat) : Int) :=
  emod_two_pow_eq x.width x.value

theorem uintNat_lt (x : BV) : uintNat x < 2 ^ x.width := modNat_lt _ _

def bvUintFast (x : BV) : Int := ((uintNat x : Nat) : Int)

@[csimp] theorem bv_uint_eq_fast : @bv_uint = @bvUintFast := by
  funext x; exact bv_uint_eq_uintNat x

def mkBvFast (w : Nat) (v : Int) : BV := { width := w, value := ((modNat w v : Nat) : Int) }

@[csimp] theorem mk_bv_eq_fast : @mk_bv = @mkBvFast := by
  funext w v
  simp only [mk_bv, mkBvFast, emod_two_pow_eq]

/-- A `BV` from a `Nat` that is already below `2 ^ w`. -/
@[inline] def ofNatLt (w n : Nat) : BV := { width := w, value := (n : Int) }

theorem mk_bv_natCast (w n : Nat) (h : n < 2 ^ w) : mk_bv w (n : Int) = ofNatLt w n := by
  simp only [mk_bv, ofNatLt, two_pow_int]
  rw [← Int.natCast_emod, Nat.mod_eq_of_lt h]

theorem mk_bv_natCast_mod (w n : Nat) : mk_bv w (n : Int) = ofNatLt w (n % 2 ^ w) := by
  simp only [mk_bv, ofNatLt, two_pow_int, ← Int.natCast_emod]

/-- `x % 2 ^ w`, by `modPow2`. -/
@[inline] def truncNat (w x : Nat) : Nat := modPow2 w x

theorem truncNat_eq (w x : Nat) : truncNat w x = x % 2 ^ w := modPow2_eq w x

def bvResizeFast (w : Nat) (x : BV) : BV := ofNatLt w (truncNat w (uintNat x))

@[csimp] theorem bv_resize_eq_fast : @bv_resize = @bvResizeFast := by
  funext w x
  simp only [bv_resize, bvResizeFast, bv_uint_eq_uintNat, truncNat_eq]
  rw [← mk_bv_natCast w _ (Nat.mod_lt _ (Nat.two_pow_pos w))]
  simp only [mk_bv, two_pow_int, ← Int.natCast_emod, Nat.mod_mod]

def bvNonzeroFast (x : BV) : Bool := uintNat x != 0

@[csimp] theorem bv_nonzero_eq_fast : @bv_nonzero = @bvNonzeroFast := by
  funext x
  simp only [bv_nonzero, bvNonzeroFast, bv_uint_eq_uintNat]
  by_cases h : uintNat x = 0 <;> simp [h]

theorem bv_bit_eq (x : BV) (i : Nat) : bv_bit x i = (uintNat x).testBit i := by
  simp only [bv_bit, bv_uint_eq_uintNat, Int.toNat_natCast, Nat.testBit, Nat.one_and_eq_mod_two]
  by_cases h : (uintNat x >>> i) % 2 = 1 <;> simp [h]

def bvBitFast (x : BV) (i : Nat) : Bool := (uintNat x).testBit i

@[csimp] theorem bv_bit_eq_fast : @bv_bit = @bvBitFast := by
  funext x i; exact bv_bit_eq x i

/-! ## `bits_to_int`: a sum of powers of two is the number with those bits -/

theorem bits_to_int_succ (w : Nat) (g : Nat → Bool) :
    bits_to_int (w + 1) g = bits_to_int w g + (if g w then (2 ^ w : Int) else 0) := by
  simp only [bits_to_int, List.range_succ, List.filterMap_append, List.sum_append]
  congr 1
  by_cases h : g w <;> simp [h]

/-- `bits_to_int w g` is the `Nat` below `2 ^ w` whose bit `i` is `g i`. -/
theorem bits_to_int_eq (w : Nat) (g : Nat → Bool) (N : Nat) (hN : N < 2 ^ w)
    (hb : ∀ i, i < w → N.testBit i = g i) : bits_to_int w g = (N : Int) := by
  induction w generalizing N with
  | zero =>
    have : N = 0 := by simpa using hN
    simp [bits_to_int, this]
  | succ w ih =>
    rw [bits_to_int_succ, ih (N % 2 ^ w) (Nat.mod_lt _ (Nat.two_pow_pos w))
      (fun i hi => by rw [Nat.testBit_mod_two_pow]; simp [hi, hb i (by omega)])]
    have hsplit : N = N % 2 ^ w + (if N.testBit w then 2 ^ w else 0) := by
      have h := Nat.mod_pow_succ (b := 2) (x := N) (k := w)
      rw [Nat.mod_eq_of_lt hN] at h
      rw [Nat.testBit_eq_decide_div_mod_eq]
      have hd : N / 2 ^ w % 2 < 2 := Nat.mod_lt _ (by decide)
      by_cases h1 : N / 2 ^ w % 2 = 1
      · rw [h1, Nat.mul_one] at h
        simp only [h1, decide_true, if_true]
        exact h
      · have h0 : N / 2 ^ w % 2 = 0 := by omega
        rw [h0, Nat.mul_zero, Nat.add_zero] at h
        simp only [h1, decide_false]
        simpa using h
    rw [← hb w (by omega)]
    conv => rhs; rw [hsplit]
    by_cases h : N.testBit w <;> simp [h]

/-! ## Bitwise operators -/

/-- Any two-input bitwise operator as word operations: `f` is read as its truth table, and
each of the four minterms is a word AND of the operands or their complements (within `w`
bits). -/
@[inline] def bitwiseNat (w : Nat) (f : Bool → Bool → Bool) (A B : Nat) : Nat :=
  let m := 2 ^ w - 1
  let nA := A ^^^ m
  let nB := B ^^^ m
  ((if f true true then A &&& B else 0) ||| (if f true false then A &&& nB else 0)
    ||| (if f false true then nA &&& B else 0) ||| (if f false false then nA &&& nB else 0)) &&& m

theorem testBit_bitwiseNat (w : Nat) (f : Bool → Bool → Bool) (A B i : Nat) :
    (bitwiseNat w f A B).testBit i = (decide (i < w) && f (A.testBit i) (B.testBit i)) := by
  simp only [bitwiseNat, Nat.testBit_and, Nat.testBit_or, Nat.testBit_two_pow_sub_one]
  by_cases hi : i < w
  · cases ha : A.testBit i <;> cases hb : B.testBit i <;>
      cases h1 : f true true <;> cases h2 : f true false <;> cases h3 : f false true <;>
      cases h4 : f false false <;> simp_all [Nat.testBit_and]
  · simp [hi]

theorem bitwiseNat_lt (w : Nat) (f : Bool → Bool → Bool) (A B : Nat) : bitwiseNat w f A B < 2 ^ w := by
  unfold bitwiseNat
  simp only []
  exact Nat.lt_of_le_of_lt Nat.and_le_right (Nat.sub_lt (Nat.two_pow_pos w) (by decide))

/-- `bitwiseNat` with the mask from a table. -/
@[inline] def bitwiseNatTab (w : Nat) (f : Bool → Bool → Bool) (A B : Nat) : Nat :=
  let m := maskOf w
  let nA := A ^^^ m
  let nB := B ^^^ m
  ((if f true true then A &&& B else 0) ||| (if f true false then A &&& nB else 0)
    ||| (if f false true then nA &&& B else 0) ||| (if f false false then nA &&& nB else 0)) &&& m

theorem bitwiseNatTab_eq (w : Nat) (f : Bool → Bool → Bool) (A B : Nat) :
    bitwiseNatTab w f A B = bitwiseNat w f A B := by
  unfold bitwiseNatTab bitwiseNat
  rw [maskOf_eq]

/-- Inlined, so at a call site with a literal operator (`fun x y => x && y`) the compiler
evaluates the truth table itself and keeps one word operation, instead of calling the
operator's closure four times per use. -/
@[inline] def bvBitwiseFast (w : Nat) (f : Bool → Bool → Bool) (a b : BV) : BV :=
  ofNatLt w (bitwiseNatTab w f (uintNat a) (uintNat b))

@[csimp] theorem bv_bitwise_eq_fast : @bv_bitwise = @bvBitwiseFast := by
  funext w f a b
  simp only [bv_bitwise, bvBitwiseFast, bitwiseNatTab_eq]
  rw [bits_to_int_eq w _ (bitwiseNat w f (uintNat a) (uintNat b)) (bitwiseNat_lt _ _ _ _)
      (fun i hi => by rw [testBit_bitwiseNat]; simp [hi, bv_bit_eq]),
    mk_bv_natCast w _ (bitwiseNat_lt _ _ _ _)]

@[inline] def bvNotFast (w : Nat) (a : BV) : BV :=
  ofNatLt w (bitwiseNatTab w (fun x _ => !x) (uintNat a) 0)

@[csimp] theorem bv_not_eq_fast : @bv_not = @bvNotFast := by
  funext w a
  simp only [bv_not, bvNotFast, bitwiseNatTab_eq]
  rw [bits_to_int_eq w _ (bitwiseNat w (fun x _ => !x) (uintNat a) 0) (bitwiseNat_lt _ _ _ _)
      (fun i hi => by rw [testBit_bitwiseNat]; simp [hi, bv_bit_eq]),
    mk_bv_natCast w _ (bitwiseNat_lt _ _ _ _)]

/-! ## Signed value -/

/-- `bv_sint` on scalars. A negative result is built as `Int.negSucc` of a small `Nat`, so
it never passes through the unsigned value as a (possibly big) `Int`. -/
def bvSintFast (x : BV) : Int :=
  if h : x.width < maskTab.size then
    let u := uintNat x
    if x.width = 0 then 0
    else if u ≤ maskTab[x.width - 1] then (u : Int)
    else Int.negSucc (maskTab[x.width] - u)
  else bv_sint x

@[csimp] theorem bv_sint_eq_fast : @bv_sint = @bvSintFast := by
  funext x
  unfold bvSintFast
  split
  · rename_i h
    simp only [bv_sint, bv_uint_eq_uintNat]
    rw [maskTab_get _ (by omega), maskTab_get _ h]
    have hu := uintNat_lt x
    have hp := Nat.two_pow_pos (x.width - 1)
    by_cases h0 : x.width = 0
    · simp [h0]
    · have hw : 2 ^ x.width = 2 * 2 ^ (x.width - 1) := by
        rw [← Nat.pow_succ']; congr 1; omega
      simp only [h0, if_false, two_pow_int]
      by_cases hlt : uintNat x ≤ 2 ^ (x.width - 1) - 1
      · have : ((uintNat x : Nat) : Int) < ((2 ^ (x.width - 1) : Nat) : Int) := by omega
        rw [if_pos this, if_pos hlt]
      · have : ¬ ((uintNat x : Nat) : Int) < ((2 ^ (x.width - 1) : Nat) : Int) := by omega
        rw [if_neg this, if_neg hlt, Int.negSucc_eq]
        omega
  · rfl

/-! ## One `Op_SHL` term -/

/-- `bv_shl_step` without `2 ^ k`: keep the low `w - k` bits of `a` and multiply by `2 ^ k`
from the table; the product is below `2 ^ w`. -/
def bvShlStepFast (w : Nat) (a b : BV) : BV :=
  let k := uintNat b
  if hk : k ≥ w then ofNatLt w 0
  else if h : w < maskTab.size then
    ofNatLt w ((uintNat a &&& maskTab[w - k]'(by omega)) *
      pow2Tab[k]'(by rw [pow2Tab_size]; rw [maskTab_size] at h; omega))
  else ofNatLt w (modPow2 w (uintNat a * pow2Of k))

theorem mul_two_pow_mod (x s w : Nat) (hs : s ≤ w) :
    x * 2 ^ s % 2 ^ w = x % 2 ^ (w - s) * 2 ^ s := by
  have : 2 ^ w = 2 ^ (w - s) * 2 ^ s := by rw [← Nat.pow_add]; congr 1; omega
  rw [this, Nat.mul_mod_mul_right]

@[csimp] theorem bv_shl_step_eq_fast : @bv_shl_step = @bvShlStepFast := by
  funext w a b
  unfold bvShlStepFast
  simp only [bv_shl_step, bv_uint_eq_uintNat, Int.toNat_natCast]
  by_cases hk : uintNat b ≥ w
  · rw [dif_pos hk, if_pos hk]
    exact mk_bv_natCast w 0 (Nat.two_pow_pos w)
  · rw [dif_neg hk, if_neg hk]
    split
    · rename_i h
      rw [and_maskTab _ _ (by omega), pow2Tab_get, two_pow_int, ← Int.natCast_mul, mk_bv_natCast_mod,
        mul_two_pow_mod _ _ _ (by omega)]
    · rw [two_pow_int, ← Int.natCast_mul, mk_bv_natCast_mod, modPow2_eq, pow2Of_eq]

/-! ## `bv_get_mask` on a contiguous mask -/

theorem mask_indices_bv_contig (m : BV) (s L : Nat)
    (hm : uintNat m = (2 ^ L - 1) * 2 ^ s) (hle : s + L ≤ m.width) :
    mask_indices_bv m = List.range' s L := by
  have key : ∀ i, bv_bit m i = decide (s ≤ i ∧ i < s + L) := by
    intro i
    simp only [bv_bit_eq, hm, Nat.testBit_mul_two_pow, Nat.testBit_two_pow_sub_one]
    by_cases h1 : s ≤ i <;> by_cases h2 : i < s + L <;> simp [h1, h2] <;> omega
  have e1 : List.range' 0 s ++ List.range' s L = List.range' 0 (s + L) := by
    have := @List.range'_append 0 s L 1
    simpa using this
  have e2 : List.range' 0 (s + L) ++ List.range' (s + L) (m.width - (s + L)) = List.range' 0 m.width := by
    have := @List.range'_append 0 (s + L) (m.width - (s + L)) 1
    simp only [Nat.zero_add, Nat.one_mul] at this
    rw [this]
    congr 1
    omega
  unfold mask_indices_bv
  rw [List.range_eq_range', ← e2, ← e1, List.filter_append, List.filter_append]
  have f1 : (List.range' 0 s).filter (fun i => bv_bit m i) = [] := by
    rw [List.filter_eq_nil_iff]; intro i hi
    simp only [List.mem_range'_1] at hi; simp [key]; omega
  have f2 : (List.range' s L).filter (fun i => bv_bit m i) = List.range' s L := by
    rw [List.filter_eq_self]; intro i hi
    simp only [List.mem_range'_1] at hi; simp [key]; omega
  have f3 : (List.range' (s + L) (m.width - (s + L))).filter (fun i => bv_bit m i) = [] := by
    rw [List.filter_eq_nil_iff]; intro i hi
    simp only [List.mem_range'_1] at hi; simp [key]; omega
  rw [f1, f2, f3, List.nil_append, List.append_nil]

theorem pack_low_bv_range' (x : BV) (s L : Nat) :
    pack_low_bv x (List.range' s L).reverse = (((uintNat x >>> s) % 2 ^ L : Nat) : Int) := by
  induction L with
  | zero => simp [pack_low_bv]
  | succ L ih =>
    rw [List.range'_concat, List.reverse_append]
    simp only [Nat.one_mul, List.reverse_cons, List.reverse_nil, List.nil_append, List.cons_append]
    rw [pack_low_bv]
    simp only [List.length_reverse, List.length_range', ih]
    have hsplit := Nat.mod_pow_succ (b := 2) (x := uintNat x >>> s) (k := L)
    have hbit : bv_bit x (s + L) = decide ((uintNat x >>> s) / 2 ^ L % 2 = 1) := by
      rw [bv_bit_eq, ← Nat.testBit_shiftRight, Nat.testBit_eq_decide_div_mod_eq]
    have hd : (uintNat x >>> s) / 2 ^ L % 2 < 2 := Nat.mod_lt _ (by decide)
    rw [hbit, hsplit, two_pow_int]
    by_cases h1 : (uintNat x >>> s) / 2 ^ L % 2 = 1
    · simp only [h1, decide_true, if_true, Nat.mul_one, Int.natCast_add]
      omega
    · have h0 : (uintNat x >>> s) / 2 ^ L % 2 = 0 := by omega
      simp only [h0, Nat.mul_zero, Nat.add_zero]
      simp

/-- Trailing zero bits of `n`, by a plain loop (64 when `n = 0`). Its answer is checked
before it is used, so this needs no proof. -/
def ctz (n : Nat) : Nat := go n 0 64
where
  go (n acc : Nat) : Nat → Nat
    | 0 => acc
    | f + 1 => if n % 2 == 1 then acc else go (n >>> 1) (acc + 1) f

/-- `bv_get_mask` on a run-of-ones mask `(2 ^ L - 1) * 2 ^ s`: one shift and one mask.
`s` and `L` come from `ctz` and are checked against the mask before they are used. Every
mask LiveHD emits for PicoRV32 has this shape; any other mask takes the definition. -/
def bvGetMaskFast (w : Nat) (x m : BV) : BV :=
  let M := uintNat m
  let s := ctz M
  let L := ctz ((M >>> s) + 1)
  if h : s < pow2Tab.size ∧ L < maskTab.size then
    if s + L ≤ m.width ∧ M = maskTab[L] * pow2Tab[s] then
      ofNatLt w (truncNat w ((uintNat x >>> s) &&& maskTab[L]))
    else bv_get_mask w x m
  else bv_get_mask w x m

@[csimp] theorem bv_get_mask_eq_fast : @bv_get_mask = @bvGetMaskFast := by
  funext w x m
  unfold bvGetMaskFast
  simp only []
  split
  · rename_i h
    split
    · rename_i h2
      obtain ⟨hs, hL⟩ := h
      obtain ⟨hle, heq⟩ := h2
      rw [maskTab_get _ hL, pow2Tab_get _ hs] at heq
      unfold bv_get_mask
      rw [mask_indices_bv_contig m _ _ heq hle, pack_low_bv_range', and_maskTab _ _ hL, truncNat_eq]
      exact mk_bv_natCast_mod w _
    · rfl
  · rfl

/-! ## `Op_Sext` -/

/-- `eval_op`'s `Op_Sext` case, word for word, under a name a fast form can replace. -/
def sextOp (w : Nat) (a amount : BV) : BV :=
  let n := (bv_uint amount).toNat
  if n = 0 then mk_bv w 0
  else if n > a.width then mk_bv w (bv_uint a)
  else
    let u := bv_uint a % (2 ^ n : Int)
    if u < (2 ^ (n - 1) : Int) then mk_bv w u
    else mk_bv w (u - (2 ^ n : Int))

/-- A negative `n`-bit value `u - 2 ^ n`, reduced to `w` bits, is `u` plus the ones between
bits `n` and `w` (none when `w ≤ n`). -/
theorem sext_mod (u n w : Nat) :
    ((u : Int) - ((2 ^ n : Nat) : Int)) % ((2 ^ w : Nat) : Int)
      = (((u + (2 ^ w - 1 - (2 ^ n - 1))) % 2 ^ w : Nat) : Int) := by
  have hn := Nat.two_pow_pos n
  have hw := Nat.two_pow_pos w
  by_cases hnw : n ≤ w
  · have hp : 2 ^ n ≤ 2 ^ w := Nat.pow_le_pow_right (by decide) hnw
    have e : (u : Int) - ((2 ^ n : Nat) : Int)
        = ((u + (2 ^ w - 1 - (2 ^ n - 1)) : Nat) : Int) + (-1) * ((2 ^ w : Nat) : Int) := by omega
    rw [e, Int.add_mul_emod_self_right, ← Int.natCast_emod]
  · have hp : 2 ^ w ≤ 2 ^ n := Nat.pow_le_pow_right (by decide) (by omega)
    have hz : 2 ^ w - 1 - (2 ^ n - 1) = 0 := by omega
    have hpow : 2 ^ n = 2 ^ (n - w) * 2 ^ w := by rw [← Nat.pow_add]; congr 1; omega
    have e : (u : Int) - ((2 ^ n : Nat) : Int)
        = (u : Int) + (-((2 ^ (n - w) : Nat) : Int)) * ((2 ^ w : Nat) : Int) := by
      rw [hpow, Int.natCast_mul, Int.neg_mul, ← Int.sub_eq_add_neg]
    rw [e, Int.add_mul_emod_self_right, hz, Nat.add_zero, ← Int.natCast_emod]

/-- `sextOp` on scalars: truncate to `n` bits, and when bit `n - 1` is set, fill bits `n`
to `w - 1` with ones. -/
def sextOpFast (w : Nat) (a amount : BV) : BV :=
  let n := uintNat amount
  if n = 0 then ofNatLt w 0
  else if n > a.width then ofNatLt w (truncNat w (uintNat a))
  else if h : n < maskTab.size ∧ w < maskTab.size then
    let u := uintNat a &&& maskTab[n]
    if u ≤ maskTab[n - 1]'(by omega) then ofNatLt w (u &&& maskTab[w])
    else ofNatLt w ((u + (maskTab[w] - maskTab[n])) &&& maskTab[w])
  else sextOp w a amount

@[csimp] theorem sextOp_eq_fast : @sextOp = @sextOpFast := by
  funext w a amount
  unfold sextOpFast
  simp only []
  by_cases h0 : uintNat amount = 0
  · rw [if_pos h0]
    unfold sextOp
    simp only [bv_uint_eq_uintNat, Int.toNat_natCast, if_pos h0]
    exact mk_bv_natCast w 0 (Nat.two_pow_pos w)
  · rw [if_neg h0]
    by_cases hg : uintNat amount > a.width
    · rw [if_pos hg]
      unfold sextOp
      simp only [bv_uint_eq_uintNat, Int.toNat_natCast, if_neg h0, if_pos hg]
      rw [truncNat_eq, mk_bv_natCast_mod]
    · rw [if_neg hg]
      by_cases h : uintNat amount < maskTab.size ∧ w < maskTab.size
      · rw [dif_pos h]
        unfold sextOp
        simp only [bv_uint_eq_uintNat, Int.toNat_natCast, if_neg h0, if_neg hg]
        have hn1 := Nat.two_pow_pos (uintNat amount - 1)
        have hpw : 2 ^ uintNat amount = 2 * 2 ^ (uintNat amount - 1) := by
          rw [← Nat.pow_succ']; congr 1; omega
        simp only [and_maskTab _ _ h.1, and_maskTab _ _ h.2]
        rw [maskTab_get _ (by omega), maskTab_get _ h.2, maskTab_get _ h.1]
        simp only [two_pow_int]
        rw [← Int.natCast_emod]
        by_cases hl : uintNat a % 2 ^ uintNat amount ≤ 2 ^ (uintNat amount - 1) - 1
        · have hlt : ((uintNat a % 2 ^ uintNat amount : Nat) : Int) < ((2 ^ (uintNat amount - 1) : Nat) : Int) := by
            omega
          rw [if_pos hlt, if_pos hl, mk_bv_natCast_mod]
        · have hlt : ¬ ((uintNat a % 2 ^ uintNat amount : Nat) : Int) < ((2 ^ (uintNat amount - 1) : Nat) : Int) := by
            omega
          rw [if_neg hlt, if_neg hl]
          simp only [mk_bv, ofNatLt, two_pow_int]
          rw [sext_mod]
      · rw [dif_neg h]

/-! ## `eval_op` and `eval_op_cert`, recompiled

`eval_op` and `eval_op_cert` were compiled in `LGraphModel`, before the lemmas above, so
their compiled bodies call the slow forms. Each gets a copy here, compiled after the
lemmas. A copy is the same term except that two `eval_op` cases name their bodies
(`bv_shl_step` is exactly the `Op_SHL` fold term, `sextOp` exactly the `Op_Sext` case), so
each `@[csimp]` theorem below holds by unfolding: `rfl`. -/

def evalOpFast : LGraphOp → Nat → List BV → BV
  | LGraphOp.Op_Const c, w, _       => mk_bv w c
  | LGraphOp.Op_Sum n_add, w, args =>
    let all := args.map bv_uint
    let adds := all.take n_add
    let subs := all.drop n_add
    mk_bv w (adds.sum - subs.sum)
  | LGraphOp.Op_Sub, w, [a, b]     => mk_bv w (bv_uint a - bv_uint b)
  | LGraphOp.Op_Mult, w, args       => mk_bv w (args.map bv_uint |>.prod)
  | LGraphOp.Op_Div, w, [a, b]     =>
    mk_bv w (if bv_uint b = 0 then 0 else bv_uint a / bv_uint b)
  | LGraphOp.Op_UDiv, w, [a, b]    =>
    mk_bv w (if bv_uint b = 0 then 0 else bv_uint a / bv_uint b)
  | LGraphOp.Op_SDiv, w, [a, b]    => bv_sdiv w a b
  | LGraphOp.Op_And, w, []         => mk_bv w 0
  | LGraphOp.Op_And, w, (a :: args) =>
    args.foldl (fun acc b => bv_bitwise w (fun x y => x && y) acc b) (bv_resize w a)
  | LGraphOp.Op_Or, w, args        =>
    args.foldl (fun acc b => bv_bitwise w (fun x y => x || y) acc b) (mk_bv w 0)
  | LGraphOp.Op_Xor, w, args       =>
    args.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc b) (mk_bv w 0)
  | LGraphOp.Op_Ror, w, xs         =>
    mk_bv w (if xs.any bv_nonzero then 1 else 0)
  | LGraphOp.Op_Not, w, [a]        => bv_not w a
  | LGraphOp.Op_LT, w, [a, b]      => mk_bv w (if bv_uint a < bv_uint b then 1 else 0)
  | LGraphOp.Op_GT, w, [a, b]      => mk_bv w (if bv_uint a > bv_uint b then 1 else 0)
  | LGraphOp.Op_ULT, w, [a, b]     => mk_bv w (if bv_uint a < bv_uint b then 1 else 0)
  | LGraphOp.Op_UGT, w, [a, b]     => mk_bv w (if bv_uint a > bv_uint b then 1 else 0)
  | LGraphOp.Op_SLT, w, [a, b]     => mk_bv w (if bv_sint a < bv_sint b then 1 else 0)
  | LGraphOp.Op_SGT, w, [a, b]     => mk_bv w (if bv_sint a > bv_sint b then 1 else 0)
  | LGraphOp.Op_EQ, w, []          => mk_bv w 1
  | LGraphOp.Op_EQ, w, (a :: args) =>
    mk_bv w (if args.all fun b => bv_uint b = bv_uint a then 1 else 0)
  | LGraphOp.Op_SHL, w, []         => mk_bv w 0
  | LGraphOp.Op_SHL, w, (a :: bs)  =>
    bs.foldl (fun acc b => bv_bitwise w (fun x y => xor x y) acc (bv_shl_step w a b)) (mk_bv w 0)
  | LGraphOp.Op_SRA, w, [a, b]     => bv_sra w a b
  | LGraphOp.Op_MuxBool, w, [sel, false_v, true_v] =>
    if bv_nonzero sel then bv_resize w true_v else bv_resize w false_v
  | LGraphOp.Op_MuxN, w, []        => mk_bv w 0
  | LGraphOp.Op_MuxN, w, (sel :: args) =>
    let idx := (bv_uint sel).toNat
    if idx < args.length then bv_resize w ((args[idx]?).getD (mk_bv w 0)) else mk_bv w 0
  | LGraphOp.Op_Sext, w, [a, amount] => sextOp w a amount
  | LGraphOp.Op_GetMask, w, [a, m] => bv_get_mask w a m
  | LGraphOp.Op_SetMask, w, [a, m, v] => bv_set_mask w a m v
  | _, w, _                        => mk_bv w 0

@[csimp] theorem eval_op_eq_fast : @eval_op = @evalOpFast := by
  funext op w args
  rfl

def certMemReadFast (w : Nat) (m : Int → BV) (a : BV) (en : BV) : BV :=
  if bv_nonzero en then bv_resize w (m (bv_uint a)) else mk_bv w 0

@[csimp] theorem cert_mem_read_eq_fast : @cert_mem_read = @certMemReadFast := rfl

def certMemWriteFast (m : Int → BV) (a d en : BV) : Int → BV :=
  if bv_nonzero en then (fun x => if x = bv_uint a then d else m x) else m

@[csimp] theorem cert_mem_write_eq_fast : @cert_mem_write = @certMemWriteFast := rfl

def certMemWriteBeFast (w : Nat) (m : Int → BV) (a d bev : BV) (byte_w : Nat) : Int → BV :=
  if bv_nonzero bev then
    (fun x => if x = bv_uint a then cert_masked_update w (m (bv_uint a)) d bev byte_w else m x)
  else m

@[csimp] theorem cert_mem_write_be_eq_fast : @cert_mem_write_be = @certMemWriteBeFast := rfl

def evalOpCertFast : LGraphOp → Nat → List CertVal → CertVal
  | LGraphOp.Op_MemRead, w, [m, a, en] => .bv (cert_mem_read w m.asMem a.asBV en.asBV)
  | LGraphOp.Op_MemWrite, _w, [m, a, d, en] => .mem (cert_mem_write m.asMem a.asBV d.asBV en.asBV)
  | LGraphOp.Op_MemWriteBE byte_w, w, [m, a, d, be] =>
    .mem (cert_mem_write_be w m.asMem a.asBV d.asBV be.asBV byte_w)
  | LGraphOp.Op_MemRead, w, _ => .bv (mk_bv w 0)
  | LGraphOp.Op_MemWrite, _w, _ => .mem (fun _ => mk_bv 0 0)
  | LGraphOp.Op_MemWriteBE _, _w, _ => .mem (fun _ => mk_bv 0 0)
  | op, w, vs => .bv (eval_op op w (vs.map CertVal.asBV))

@[csimp] theorem eval_op_cert_eq_fast : @eval_op_cert = @evalOpCertFast := by
  funext op w args
  rfl

end FastBV
