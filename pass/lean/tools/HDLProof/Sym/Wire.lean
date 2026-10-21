/-
`Sym.Wire` -- bit-level WIRING: an expression every output bit of which is a
constant or one bit of a variable (possibly negated) costs no gate, so it needs
no cut variable.

Why: a concatenation reaches the residual as a chain of full-width
`acc | (x << k)` nodes (LiveHD has no concat), and a `let` per node. On a
symbolic state each node used to become a cut variable with a `var == e`
definition of its full width -- about a dozen clauses per bit for what is only
wiring. The contract-3 relation model binds ~3,800 AIG latch bits into 992- and
675-bit vectors that way: 9,316 such shifts, 6.2M bits, and a 292M-clause step
obligation where yosys builds 9.6M.

`wires e` reads `e` bit by bit (LOWEST first) when it is wiring and fails
otherwise; `ofBits` rebuilds a compact expression -- maximal runs of one
variable's consecutive bits as `extract`, constant runs as `const`, joined by
`append`. `norm` composes them, and `eval_norm` says the result evaluates as
`e`. Mathlib-free.
-/
import Std.Tactic.BVDecide
import HDLProof.Sym.Kit

namespace HDLProof.Sym
open Std.Tactic.BVDecide

/-! ## Bits -/

/-- One output bit: a constant, or bit `pos` of the `w`-bit variable `idx`,
negated when `neg`. -/
inductive WBit where
  | c (b : Bool)
  | v (idx w pos : Nat) (neg : Bool)
  deriving Inhabited

namespace WBit

def eval (σ : Asg) : WBit → Bool
  | .c b => b
  | .v idx w pos neg => xor (((BVExpr.var idx : BVExpr w).eval σ).getLsbD pos) neg

def flip : WBit → WBit
  | .c b => .c (!b)
  | .v i w p n => .v i w p (!n)

@[simp] theorem eval_c (σ : Asg) (b : Bool) : (WBit.c b).eval σ = b := rfl

theorem eval_flip (σ : Asg) (b : WBit) : b.flip.eval σ = !b.eval σ := by
  cases b with
  | c b => rfl
  | v i w p n => cases n <;> simp [flip, eval]

/-- `a && b` when one side is a constant. -/
def and2 : WBit → WBit → Option WBit
  | .c false, _ => some (.c false)
  | .c true, b => some b
  | _, .c false => some (.c false)
  | a, .c true => some a
  | _, _ => none

/-- `a || b` when one side is a constant. -/
def or2 : WBit → WBit → Option WBit
  | .c true, _ => some (.c true)
  | .c false, b => some b
  | _, .c true => some (.c true)
  | a, .c false => some a
  | _, _ => none

/-- `a ^^ b` when one side is a constant. -/
def xor2 : WBit → WBit → Option WBit
  | .c x, b => some (if x then b.flip else b)
  | a, .c y => some (if y then a.flip else a)
  | _, _ => none

theorem eval_and2 {σ : Asg} {a b r : WBit} (h : and2 a b = some r) :
    r.eval σ = (a.eval σ && b.eval σ) := by
  unfold and2 at h
  split at h <;> (try cases h) <;> simp_all [eval]

theorem eval_or2 {σ : Asg} {a b r : WBit} (h : or2 a b = some r) :
    r.eval σ = (a.eval σ || b.eval σ) := by
  unfold or2 at h
  split at h <;> (try cases h) <;> simp_all [eval]

theorem eval_xor2 {σ : Asg} {a b r : WBit} (h : xor2 a b = some r) :
    r.eval σ = (a.eval σ ^^ b.eval σ) := by
  cases a with
  | c x => simp only [xor2, Option.some.injEq] at h; subst h; cases x <;> simp [eval_flip]
  | v i w p n =>
    cases b with
    | c y => simp only [xor2, Option.some.injEq] at h; subst h; cases y <;> simp [eval_flip]
    | v => simp [xor2] at h

end WBit

/-! ## Bit arrays -/

/-- The bit at `i`, `false` past the end. -/
@[inline] def bitAt (a : Array WBit) (i : Nat) : WBit := a.getD i (.c false)

/-- `n` bits, bit `i` being `f i`. -/
def mkBits (n : Nat) (f : Nat → WBit) : Array WBit := (Array.range n).map f

@[simp] theorem size_mkBits (n : Nat) (f : Nat → WBit) : (mkBits n f).size = n := by
  simp [mkBits]

theorem bitAt_mkBits (n : Nat) (f : Nat → WBit) (i : Nat) :
    bitAt (mkBits n f) i = if i < n then f i else .c false := by
  unfold bitAt mkBits
  rw [Array.getD_eq_getD_getElem?]
  by_cases h : i < n <;> simp [h]

theorem bitAt_of_ge {a : Array WBit} {i : Nat} (h : a.size ≤ i) : bitAt a i = .c false := by
  unfold bitAt; rw [Array.getD_eq_getD_getElem?]; simp [Array.getElem?_eq_none h]

/-- The constant a bit array denotes, when every bit is a constant. -/
def constNat (a : Array WBit) : Option Nat :=
  a.foldr (fun b acc => match b, acc with
    | .c x, some n => some (2 * n + if x then 1 else 0)
    | _, _ => none) (some 0)

/-- `e` meets the bit array `a`: same width, and every bit agrees. -/
def WSpec (σ : Asg) {w : Nat} (e : BVExpr w) (a : Array WBit) : Prop :=
  a.size = w ∧ ∀ i, (e.eval σ).getLsbD i = (bitAt a i).eval σ

/-! ## Reading an expression as wiring -/

/-- Bitwise combination of two arrays of one width, bit by bit. -/
def zipBits (f : WBit → WBit → Option WBit) (a b : Array WBit) : Option (Array WBit) :=
  if a.size = b.size then
    (List.range a.size).foldlM (fun acc i => (f (bitAt a i) (bitAt b i)).map acc.push) #[]
  else none

/-- The bits of a shift by the CONSTANT `k`: left (`shl`), logical right, or
arithmetic right, over the `m` bits of `a`. -/
def shlBits (m k : Nat) (a : Array WBit) : Array WBit :=
  mkBits m fun i => if i < k then .c false else bitAt a (i - k)
def shrBits (m k : Nat) (a : Array WBit) : Array WBit :=
  mkBits m fun i => bitAt a (k + i)
def sarBits (m k : Nat) (a : Array WBit) : Array WBit :=
  mkBits m fun i => if k + i < m then bitAt a (k + i) else bitAt a (m - 1)

/-- The bits of `e`, LOWEST first, when `e` is wiring. -/
def wires : {w : Nat} → BVExpr w → Option (Array WBit)
  | w, .var idx => some (mkBits w fun i => .v idx w i false)
  | w, .const c => some (mkBits w fun i => .c (c.getLsbD i))
  | _, .extract start len e => do
      let a ← wires e
      pure (mkBits len fun i => bitAt a (start + i))
  | _, .append (l := l) (r := r) hi lo _ => do
      let a ← wires hi
      let b ← wires lo
      pure (mkBits (l + r) fun i => if i < r then bitAt b i else bitAt a (i - r))
  | w, .un .not e => do
      let a ← wires e
      pure (mkBits w fun i => (bitAt a i).flip)
  | w, .un (.arithShiftRightConst k) e => do
      let a ← wires e
      pure (sarBits w k a)
  | _, .bin x .and y => do zipBits WBit.and2 (← wires x) (← wires y)
  | _, .bin x .or y => do zipBits WBit.or2 (← wires x) (← wires y)
  | _, .bin x .xor y => do zipBits WBit.xor2 (← wires x) (← wires y)
  | m, .shiftLeft x (.const c) => do
      let a ← wires x
      pure (shlBits m c.toNat a)
  | m, .shiftRight x (.const c) => do
      let a ← wires x
      pure (shrBits m c.toNat a)
  | m, .arithShiftRight x (.const c) => do
      let a ← wires x
      pure (sarBits m c.toNat a)
  | _, _ => none

/-! ### Soundness of `wires` -/

theorem zipBits_spec {f : WBit → WBit → Option WBit} {a b r : Array WBit} {n : Nat}
    (ha : a.size = n) (hb : b.size = n) (h : zipBits f a b = some r) :
    r.size = n ∧ ∀ i, i < n → f (bitAt a i) (bitAt b i) = some (bitAt r i) := by
  unfold zipBits at h
  rw [if_pos (by omega)] at h
  -- invariant over the fold: after `j` steps the accumulator holds bits `0..j-1`
  suffices key : ∀ (l : List Nat) (acc : Array WBit), (∀ k ∈ l, acc.size ≤ k) →
      l.Pairwise (· < ·) → (∀ k ∈ l, ∀ k' ∈ l, k < k' → True) →
      ∀ res, l.foldlM (fun acc i => (f (bitAt a i) (bitAt b i)).map acc.push) acc = some res →
        res.size = acc.size + l.length ∧
        (∀ i, i < acc.size → bitAt res i = bitAt acc i) ∧
        (∀ (j : Nat) (hj : j < l.length), f (bitAt a l[j]) (bitAt b l[j]) = some (bitAt res (acc.size + j))) by
    have := key (List.range a.size) #[] (by simp) (List.pairwise_lt_range) (by simp) r h
    simp only [Array.size_empty, Nat.zero_add, List.length_range] at this
    refine ⟨by omega, fun i hi => ?_⟩
    have := this.2.2 i (by omega)
    simpa using this
  intro l
  induction l with
  | nil => intro acc _ _ _ res h; simp at h; subst h; simp
  | cons x xs ih =>
    intro acc hle hpw _ res h
    simp only [List.foldlM_cons] at h
    cases hfx : f (bitAt a x) (bitAt b x) with
    | none => simp [hfx] at h
    | some y =>
      simp only [hfx, Option.map_some, Option.bind_eq_bind, Option.bind_some] at h
      have hle' : ∀ k ∈ xs, (acc.push y).size ≤ k := by
        intro k hk
        have h1 := hle x (by simp)
        have h2 := List.rel_of_pairwise_cons hpw hk
        simp; omega
      obtain ⟨hs, hpre, hat⟩ := ih (acc.push y) hle' (List.Pairwise.of_cons hpw) (by simp) res h
      refine ⟨by simp at hs ⊢; omega, fun i hi => ?_, fun j hj => ?_⟩
      · rw [hpre i (by simp; omega)]
        unfold bitAt; rw [Array.getD_eq_getD_getElem?, Array.getD_eq_getD_getElem?]
        simp [Array.getElem?_push, show i ≠ acc.size by omega]
      · cases j with
        | zero =>
          have := hpre acc.size (by simp)
          simp only [List.getElem_cons_zero, Nat.add_zero]
          rw [hfx, this]
          unfold bitAt; rw [Array.getD_eq_getD_getElem?]; simp
        | succ j =>
          have := hat j (by simp at hj; omega)
          simp only [List.getElem_cons_succ, Array.size_push] at this ⊢
          rw [this]; congr 2; omega

theorem wspec_zip {σ : Asg} {w : Nat} {x y : BVExpr w} {a b r : Array WBit}
    {f : WBit → WBit → Option WBit} {g : Bool → Bool → Bool}
    (hf : ∀ p q s, f p q = some s → s.eval σ = g (p.eval σ) (q.eval σ))
    (hg : ∀ i, ((BVExpr.bin x op y).eval σ).getLsbD i = g ((x.eval σ).getLsbD i) ((y.eval σ).getLsbD i))
    (ha : WSpec σ x a) (hb : WSpec σ y b) (h : zipBits f a b = some r) :
    WSpec σ (BVExpr.bin x op y) r := by
  obtain ⟨hrs, hr⟩ := zipBits_spec ha.1 hb.1 h
  refine ⟨hrs, fun i => ?_⟩
  rw [hg, ha.2, hb.2]
  by_cases hi : i < w
  · exact (hf _ _ _ (hr i hi)).symm
  · rw [bitAt_of_ge (by rw [ha.1]; omega), bitAt_of_ge (by rw [hb.1]; omega),
      bitAt_of_ge (by rw [hrs]; omega)]
    have := hg i
    rw [BitVec.getLsbD_of_ge _ _ (by omega), BitVec.getLsbD_of_ge _ _ (by omega),
      BitVec.getLsbD_of_ge _ _ (by omega)] at this
    simpa using this.symm

theorem wspec_shl {σ : Asg} {m : Nat} {x : BVExpr m} {a : Array WBit} (k : Nat) (ha : WSpec σ x a) :
    ∀ i, ((x.eval σ) <<< k).getLsbD i = (bitAt (shlBits m k a) i).eval σ := by
  intro i
  rw [BitVec.getLsbD_shiftLeft, shlBits, bitAt_mkBits]
  by_cases hi : i < m
  · by_cases hk : i < k
    · simp [hi, hk]
    · simp [hi, hk, ha.2]
  · simp [hi]

theorem wspec_shr {σ : Asg} {m : Nat} {x : BVExpr m} {a : Array WBit} (k : Nat) (ha : WSpec σ x a) :
    ∀ i, ((x.eval σ) >>> k).getLsbD i = (bitAt (shrBits m k a) i).eval σ := by
  intro i
  rw [BitVec.getLsbD_ushiftRight, shrBits, bitAt_mkBits, ha.2]
  by_cases hi : i < m
  · simp [hi]
  · simp only [hi, if_false, WBit.eval_c]
    rw [bitAt_of_ge (by rw [ha.1]; omega)]; rfl

theorem wspec_sar {σ : Asg} {m : Nat} {x : BVExpr m} {a : Array WBit} (k : Nat) (ha : WSpec σ x a) :
    ∀ i, ((x.eval σ).sshiftRight k).getLsbD i = (bitAt (sarBits m k a) i).eval σ := by
  intro i
  rw [BitVec.getLsbD_sshiftRight, sarBits, bitAt_mkBits]
  by_cases hi : i < m
  · simp only [show ¬ (m ≤ i) by omega, decide_false, Bool.not_false, Bool.true_and, hi, if_true]
    split
    · rw [ha.2]
    · rw [BitVec.msb_eq_getLsbD_last, ha.2]
  · simp [hi, show m ≤ i by omega]

/-- Reading `e` as wiring is exact. -/
theorem wires_spec (σ : Asg) : ∀ {w : Nat} (e : BVExpr w) {a : Array WBit},
    wires e = some a → WSpec σ e a
  | w, .var idx, a, h => by
      simp only [wires, Option.some.injEq] at h; subst h
      refine ⟨by simp, fun i => ?_⟩
      rw [bitAt_mkBits]
      by_cases hi : i < w
      · simp [hi, WBit.eval]
      · simp [hi, BitVec.getLsbD_of_ge _ _ (by omega : w ≤ i)]
  | w, .const c, a, h => by
      simp only [wires, Option.some.injEq] at h; subst h
      refine ⟨by simp, fun i => ?_⟩
      rw [bitAt_mkBits, BVExpr.eval_const]
      by_cases hi : i < w
      · simp [hi]
      · simp [hi, BitVec.getLsbD_of_ge _ _ (by omega : w ≤ i)]
  | _, .extract start len e, a, h => by
      simp only [wires, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨b, hb, rfl⟩ := h
      have hs := wires_spec σ e hb
      refine ⟨by simp, fun i => ?_⟩
      rw [BVExpr.eval_extract, BitVec.getLsbD_extractLsb', bitAt_mkBits]
      by_cases hi : i < len
      · simp [hi, hs.2]
      · simp [hi]
  | _, .append (l := l) (r := r) hi lo hw, a, h => by
      simp only [wires, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨x, hx, y, hy, rfl⟩ := h
      have sx := wires_spec σ hi hx
      have sy := wires_spec σ lo hy
      refine ⟨by simp; omega, fun i => ?_⟩
      rw [eval_append' σ hi lo hw, getLsbD_cast_eq hw, BitVec.getLsbD_append, bitAt_mkBits]
      by_cases h1 : i < r
      · rw [if_pos h1, if_pos (show i < l + r by omega), if_pos h1, sy.2]
      · by_cases h2 : i < l + r
        · rw [if_neg h1, if_pos h2, if_neg h1, sx.2]
        · rw [if_neg h1, if_neg h2, sx.2, bitAt_of_ge (by rw [sx.1]; omega)]
  | w, .un .not e, a, h => by
      simp only [wires, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨b, hb, rfl⟩ := h
      have hs := wires_spec σ e hb
      refine ⟨by simp, fun i => ?_⟩
      rw [BVExpr.eval_un, BVUnOp.eval_not, BitVec.getLsbD_not, bitAt_mkBits]
      by_cases hi : i < w
      · rw [if_pos hi, WBit.eval_flip, ← hs.2]; simp only [hi, decide_true, Bool.true_and]
      · rw [if_neg hi]; simp only [hi, decide_false, Bool.false_and, WBit.eval_c]
  | w, .un (.arithShiftRightConst k) e, a, h => by
      simp only [wires, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨b, hb, rfl⟩ := h
      have hs := wires_spec σ e hb
      refine ⟨by simp [sarBits], fun i => ?_⟩
      rw [BVExpr.eval_un]
      exact wspec_sar k hs i
  | _, .bin x .and y, a, h => by
      simp only [wires, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
      obtain ⟨p, hp, q, hq, h⟩ := h
      exact wspec_zip (fun _ _ _ hs => WBit.eval_and2 hs)
        (fun i => by simp [BVExpr.eval_bin]) (wires_spec σ x hp) (wires_spec σ y hq) h
  | _, .bin x .or y, a, h => by
      simp only [wires, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
      obtain ⟨p, hp, q, hq, h⟩ := h
      exact wspec_zip (fun _ _ _ hs => WBit.eval_or2 hs)
        (fun i => by simp [BVExpr.eval_bin]) (wires_spec σ x hp) (wires_spec σ y hq) h
  | _, .bin x .xor y, a, h => by
      simp only [wires, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
      obtain ⟨p, hp, q, hq, h⟩ := h
      exact wspec_zip (fun _ _ _ hs => WBit.eval_xor2 hs)
        (fun i => by simp [BVExpr.eval_bin]) (wires_spec σ x hp) (wires_spec σ y hq) h
  | m, .shiftLeft x (.const c), a, h => by
      simp only [wires, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨b, hb, rfl⟩ := h
      have hs := wires_spec σ x hb
      refine ⟨by simp [shlBits], fun i => ?_⟩
      rw [BVExpr.eval_shiftLeft, BVExpr.eval_const, BitVec.shiftLeft_eq']
      exact wspec_shl _ hs i
  | m, .shiftRight x (.const c), a, h => by
      simp only [wires, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨b, hb, rfl⟩ := h
      have hs := wires_spec σ x hb
      refine ⟨by simp [shrBits], fun i => ?_⟩
      rw [BVExpr.eval_shiftRight, BVExpr.eval_const, BitVec.ushiftRight_eq']
      exact wspec_shr _ hs i
  | m, .arithShiftRight x (.const c), a, h => by
      simp only [wires, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨b, hb, rfl⟩ := h
      have hs := wires_spec σ x hb
      refine ⟨by simp [sarBits], fun i => ?_⟩
      rw [BVExpr.eval_arithShiftRight, BVExpr.eval_const, BitVec.sshiftRight_eq']
      exact wspec_sar _ hs i

/-! ## Rebuilding a compact expression -/

/-- A maximal run: constant bits (LOWEST first), or `len` consecutive bits of a
variable from `pos` up, all negated or none. -/
inductive Run where
  | c (bs : List Bool)
  | v (idx w pos len : Nat) (neg : Bool)

def Run.bits : Run → List WBit
  | .c bs => bs.map .c
  | .v idx w pos len neg => (List.range len).map fun j => .v idx w (pos + j) neg

def Run.single : WBit → Run
  | .c x => .c [x]
  | .v idx w pos neg => .v idx w pos 1 neg

/-- Put `b` below the first run when it extends it, else start a run. -/
def Run.cons (b : WBit) (rs : List Run) : List Run :=
  match b, rs with
  | .c x, .c bs :: rs => .c (x :: bs) :: rs
  | .v idx' w' pos' neg', .v idx w pos len neg :: rs =>
      if idx' = idx ∧ w' = w ∧ neg' = neg ∧ pos' + 1 = pos then .v idx w pos' (len + 1) neg :: rs
      else Run.single (.v idx' w' pos' neg') :: .v idx w pos len neg :: rs
  | b, rs => Run.single b :: rs

def runsOf : List WBit → List Run
  | [] => []
  | b :: bs => Run.cons b (runsOf bs)

def runsBits (rs : List Run) : List WBit := (rs.map Run.bits).flatten

theorem bits_single (b : WBit) : (Run.single b).bits = [b] := by
  cases b <;> simp [Run.single, Run.bits]

theorem runsBits_cons (b : WBit) (rs : List Run) : runsBits (Run.cons b rs) = b :: runsBits rs := by
  unfold Run.cons
  split
  · simp [runsBits, Run.bits]
  · split
    · rename_i hc
      obtain ⟨rfl, rfl, rfl, rfl⟩ := hc
      simp only [runsBits, List.map_cons, List.flatten_cons, Run.bits, List.range_succ_eq_map,
        List.map_map, List.cons_append, Nat.add_zero, List.cons.injEq, true_and]
      congr 1
      apply List.map_congr_left
      intro j _
      simp only [Function.comp_apply]
      congr 1
      omega
    · simp [runsBits, bits_single]
  · simp [runsBits, bits_single]

theorem runsBits_runsOf : ∀ bs : List WBit, runsBits (runsOf bs) = bs
  | [] => rfl
  | b :: bs => by rw [runsOf, runsBits_cons, runsBits_runsOf bs]

/-- The expression of one run. -/
def Run.expr : Run → (Σ n, BVExpr n)
  | .c bs => ⟨bs.length, .const (BitVec.ofBoolListLE bs)⟩
  | .v idx w pos len false => ⟨len, .extract pos len (.var idx : BVExpr w)⟩
  | .v idx w pos len true => ⟨len, .un .not (.extract pos len (.var idx : BVExpr w))⟩

/-- Runs joined, the FIRST run lowest. -/
def runsExpr : List Run → (Σ n, BVExpr n)
  | [] => ⟨0, .const 0#0⟩
  | [r] => r.expr
  | r :: rs =>
      let lo := r.expr
      let hi := runsExpr rs
      ⟨hi.1 + lo.1, .append hi.2 lo.2 rfl⟩

theorem Run.expr_len (r : Run) : r.expr.1 = r.bits.length := by
  cases r with
  | c bs => simp [Run.expr, Run.bits]
  | v idx w pos len neg => cases neg <;> simp [Run.expr, Run.bits]

theorem Run.bits_v (idx w pos len : Nat) (neg : Bool) (j : Nat) :
    (Run.v idx w pos len neg).bits.getD j (.c false)
      = if j < len then .v idx w (pos + j) neg else .c false := by
  simp only [Run.bits]
  rw [List.getD_eq_getElem?_getD, List.getElem?_map]
  by_cases hj : j < len
  · rw [List.getElem?_range hj, if_pos hj]; rfl
  · rw [List.getElem?_eq_none (by simp; omega), if_neg hj]; rfl

theorem Run.expr_bit (σ : Asg) (r : Run) (i : Nat) :
    (r.expr.2.eval σ).getLsbD i = (r.bits.getD i (.c false)).eval σ := by
  cases r with
  | c bs =>
      simp only [Run.expr, Run.bits, BVExpr.eval_const, BitVec.getLsbD_ofBoolListLE]
      rw [List.getD_eq_getElem?_getD, List.getD_eq_getElem?_getD, List.getElem?_map]
      cases bs[i]? <;> rfl
  | v idx w pos len neg =>
      rw [Run.bits_v]
      cases neg
      · show ((BVExpr.extract pos len (.var idx : BVExpr w)).eval σ).getLsbD i = _
        rw [BVExpr.eval_extract, BitVec.getLsbD_extractLsb']
        by_cases hi : i < len
        · rw [if_pos hi]; simp only [hi, decide_true, Bool.true_and, WBit.eval, Bool.xor_false]
        · rw [if_neg hi]; simp only [hi, decide_false, Bool.false_and, WBit.eval_c]
      · show ((BVExpr.un .not (.extract pos len (.var idx : BVExpr w))).eval σ).getLsbD i = _
        rw [BVExpr.eval_un, BVUnOp.eval_not, BitVec.getLsbD_not, BVExpr.eval_extract,
          BitVec.getLsbD_extractLsb']
        by_cases hi : i < len
        · rw [if_pos hi]; simp only [hi, decide_true, Bool.true_and, WBit.eval, Bool.xor_true]
        · rw [if_neg hi]; simp only [hi, decide_false, Bool.false_and, WBit.eval_c]

theorem runsExpr_len : ∀ rs : List Run, (runsExpr rs).1 = (runsBits rs).length
  | [] => rfl
  | [r] => by simp [runsExpr, runsBits, Run.expr_len]
  | r :: r' :: rs => by
      have ih := runsExpr_len (r' :: rs)
      simp only [runsExpr] at ih ⊢
      simp only [runsBits, List.map_cons, List.flatten_cons, List.length_append] at ih ⊢
      rw [ih, Run.expr_len]; omega

theorem runsExpr_bit (σ : Asg) : ∀ (rs : List Run) (i : Nat),
    ((runsExpr rs).2.eval σ).getLsbD i = ((runsBits rs).getD i (.c false)).eval σ
  | [], i => by simp [runsExpr, runsBits]
  | [r], i => by simp only [runsExpr, runsBits, List.map_cons, List.map_nil, List.flatten_cons,
      List.flatten_nil, List.append_nil]; exact Run.expr_bit σ r i
  | r :: r' :: rs, i => by
      have hsplit : runsExpr (r :: r' :: rs)
          = ⟨(runsExpr (r' :: rs)).1 + r.expr.1, .append (runsExpr (r' :: rs)).2 r.expr.2 rfl⟩ := rfl
      rw [hsplit]
      rw [eval_append' σ _ _ rfl, getLsbD_cast_eq rfl, BitVec.getLsbD_append]
      have hl := Run.expr_len r
      have ih := runsExpr_bit σ (r' :: rs)
      generalize r' :: rs = rs' at ih ⊢
      simp only [runsBits, List.map_cons, List.flatten_cons] at ih ⊢
      rw [List.getD_eq_getElem?_getD, List.getElem?_append]
      by_cases hi : i < r.expr.1
      · rw [if_pos hi, if_pos (hl ▸ hi), Run.expr_bit, List.getD_eq_getElem?_getD]
      · rw [if_neg hi, if_neg (hl ▸ hi), ih, hl, List.getD_eq_getElem?_getD]

/-! ## Normalization -/

/-- The wiring form of `e`, when `e` is wiring. -/
def norm {w : Nat} (e : BVExpr w) : Option (BVExpr w) := do
  let a ← wires e
  match runsExpr (runsOf a.toList) with
  | ⟨n, e'⟩ => if h : n = w then some (h ▸ e') else none

theorem eval_norm (σ : Asg) {w : Nat} {e e' : BVExpr w} (h : norm e = some e') :
    e'.eval σ = e.eval σ := by
  simp only [norm, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨a, ha, h⟩ := h
  split at h
  · rename_i hr
    cases h
    have hs := wires_spec σ e ha
    apply BitVec.eq_of_getLsbD_eq
    intro i _
    have key : ∀ (R : Σ n, BVExpr n) (q : R.1 = w),
        ((q ▸ R.2 : BVExpr w).eval σ).getLsbD i = (R.2.eval σ).getLsbD i := by
      intro R q; obtain ⟨n, E⟩ := R; simp only at q; subst q; rfl
    rw [key, runsExpr_bit, runsBits_runsOf, hs.2]
    unfold bitAt
    rw [Array.getD_eq_getD_getElem?, List.getD_eq_getElem?_getD, Array.getElem?_toList]
  · cases h

end HDLProof.Sym
