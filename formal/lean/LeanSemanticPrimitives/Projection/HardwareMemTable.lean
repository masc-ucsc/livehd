/-
# A memory's commit table, on the ACTUAL `hwMemS` (DCERT3, decision #9)

A memory written on more than one clock carries a commit table
(`MemoryDesc.commits`). `Compiler.srcMemNext` then takes `nextImg` when ALL the
memory's clocks fired (`allFire`), the entry whose clock set is EXACTLY the
clocks that fired (`find? firedIs`) when only some did, and holds otherwise.

`hwMemS` computes this with eight small functions (`HardwareMem`), each a
mirror of one definition. This module proves each one:

* §1 restates `MemDomains` in the shape the object functions walk: `domSpan` as
  a list fold (`spanL`), `inDom` with its `any` over the table's list,
  `allFire` / `firedIs` as a walk over `[k, sp)` (`allFrom`).
* §2 proves, per function, that `SEval` returns exactly that value: `bitAt` is
  `Nat.testBit`, `log2O` is `Nat.log2`, `spanO` is `spanL`, `anyBit` / `inDomO`
  are `inDom`, `allFireO` / `firedIsO` are `allFire` / `firedIs`. `pickO`
  reads slot images, so its proof is in `HardwareMemCommit`, next to
  `slot_readImg`.

Every clock the walks read is below `domSpan`, and `firesAt` needs it below
`e.size`: that is the one runtime premise (`domSpan ≤ e.size`, checked by
`edgesOKB`).
-/
import LeanSemanticPrimitives.Projection.HardwareMemSource
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw

/-! ## 1. `MemDomains`, in the shape the object walks -/

/-- `List.range sp`, checked from `k` on. -/
def allFrom (p : Nat → Bool) (k sp : Nat) : Bool :=
  if k < sp then p k && allFrom p (k + 1) sp else true
termination_by sp - k

theorem allFrom_range' (p : Nat → Bool) :
    ∀ (n k : Nat), allFrom p k (k + n) = (List.range' k n).all p
  | 0, k => by unfold allFrom; simp
  | n + 1, k => by
      unfold allFrom
      rw [if_pos (by omega), List.range'_succ, List.all_cons,
          show k + (n + 1) = (k + 1) + n by omega, allFrom_range' p n (k + 1)]

theorem range_all_eq (p : Nat → Bool) (sp : Nat) : (List.range sp).all p = allFrom p 0 sp := by
  rw [List.range_eq_range', ← allFrom_range' p sp 0, Nat.zero_add]

/-- `domSpan` as the list fold `spanO` walks. -/
def spanL (a : Nat) : List MemCommit → Nat
  | []      => a
  | c :: cs => spanL (if a ≤ c.mask.log2 + 1 then c.mask.log2 + 1 else a) cs

theorem spanL_eq_foldl : ∀ (cs : List MemCommit) (a : Nat),
    spanL a cs = cs.foldl (fun a c => max a (c.mask.log2 + 1)) a
  | [],      _ => rfl
  | c :: cs, a => by
      simp only [spanL, List.foldl_cons, Nat.max_def]
      exact spanL_eq_foldl cs _

theorem domSpan_eq (m : MemoryDesc) : m.domSpan = spanL (m.clock + 1) m.commits.toList := by
  unfold MemoryDesc.domSpan
  rw [spanL_eq_foldl, Array.foldl_toList]

/-- The clock span stays within `n` clocks when the clock and every mask do. -/
theorem spanL_le {n : Nat} (hn : 0 < n) :
    ∀ (cs : List MemCommit) (a : Nat), a ≤ n → (∀ c ∈ cs, c.mask < 2 ^ n) → spanL a cs ≤ n
  | [],      _, ha, _  => ha
  | c :: cs, a, ha, hc => by
      simp only [spanL]
      have hl : c.mask.log2 + 1 ≤ n := by
        by_cases h0 : c.mask = 0
        · rw [h0, Nat.log2_zero]; omega
        · have := (Nat.log2_lt h0).mpr (hc c List.mem_cons_self); omega
      refine spanL_le hn cs _ ?_ (fun x hx => hc x (List.mem_cons_of_mem _ hx))
      split <;> omega

theorem domSpan_le {m : MemoryDesc} {n : Nat} (hck : m.clock < n)
    (hc : ∀ c ∈ m.commits, c.mask < 2 ^ n) : m.domSpan ≤ n := by
  rw [domSpan_eq]
  exact spanL_le (by omega) _ _ (by omega) (fun c hcm => hc c (Array.mem_def.mpr hcm))

/-- `inDom`, with the table's `any` over its list. -/
theorem inDom_eq (m : MemoryDesc) (k : Nat) :
    m.inDom k = (k == m.clock || m.commits.toList.any (fun c => c.mask.testBit k)) := by
  unfold MemoryDesc.inDom
  rw [Array.any_toList]

/-- The predicate `allFireO` checks at clock `k`. -/
def allFireAt (m : MemoryDesc) (e : ClockEdges) (k : Nat) : Bool :=
  if m.inDom k then fires e k else true

theorem allFire_eq (m : MemoryDesc) (e : ClockEdges) :
    m.allFire e = allFrom (allFireAt m e) 0 m.domSpan := by
  unfold MemoryDesc.allFire
  rw [range_all_eq]
  congr 1
  funext k
  unfold allFireAt
  cases m.inDom k <;> rfl

/-- The predicate `firedIsO` checks at clock `k`. -/
def firedAt (m : MemoryDesc) (e : ClockEdges) (mask k : Nat) : Bool :=
  if mask.testBit k then (if m.inDom k then fires e k else false)
  else !(if m.inDom k then fires e k else false)

theorem firedIs_eq (m : MemoryDesc) (e : ClockEdges) (c : MemCommit) :
    m.firedIs e c = allFrom (firedAt m e c.mask) 0 m.domSpan := by
  unfold MemoryDesc.firedIs
  rw [range_all_eq]
  congr 1
  funext k
  unfold firedAt
  cases c.mask.testBit k <;> cases m.inDom k <;> cases fires e k <;> rfl

/-! ## 2. Each object function computes its mirror -/

theorem bitAt_agree : ∀ (k m : Nat) {σ : SEnv} {em ek : SExp},
    SEval hwMemS σ em (.int (Int.ofNat m)) → SEval hwMemS σ ek (.int (Int.ofNat k)) →
    SEval hwMemS σ (.call "bitAt" [em, ek]) (.bool (m.testBit k))
  | 0, m, _, _, _, hm, hk => by
      refine SEval_call2 hm hk rfl rfl ?_
      refine SEval.iteT (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim])) ?_
      refine SEval_prim2 (SEval_prim2 (.ref rfl) .lit rfl) .lit ?_
      simp only [evalPrim, Nat.testBit_zero]
      congr 2
      simp only [Int.ofNat_eq_natCast, decide_eq_decide]
      omega
  | k + 1, m, _, _, _, hm, hk => by
      refine SEval_call2 hm hk rfl rfl ?_
      refine SEval.iteF (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim] <;> omega)) ?_
      rw [Nat.testBit_add_one]
      exact bitAt_agree k (m / 2)
        (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim] <;> omega))
        (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim]))

theorem log2_agree : ∀ (n : Nat) {σ : SEnv} {en : SExp},
    SEval hwMemS σ en (.int (Int.ofNat n)) →
    SEval hwMemS σ (.call "log2O" [en]) (.int (Int.ofNat n.log2))
  | n, σ, en, hn => by
    refine SEval_call1 hn rfl rfl ?_
    rw [Nat.log2_def]
    by_cases h2 : 2 ≤ n
    · rw [if_pos h2]
      refine SEval.iteF (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim] <;> omega)) ?_
      refine SEval_prim2 (log2_agree (n / 2)
        (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim] <;> omega))) .lit ?_
      simp [evalPrim]
    · rw [if_neg h2]
      exact SEval.iteT (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim] <;> omega)) .lit
termination_by n => n
decreasing_by omega

theorem spanO_agree : ∀ (cs : List MemCommit) (a : Nat) {σ : SEnv} {ea ec : SExp},
    SEval hwMemS σ ea (.int (Int.ofNat a)) → SEval hwMemS σ ec (encListG encCommit cs) →
    SEval hwMemS σ (.call "spanO" [ea, ec]) (.int (Int.ofNat (spanL a cs)))
  | [], a, _, _, _, ha, hc => by
      refine SEval_call2 ha hc rfl rfl ?_
      exact SEval.iteT (SEval_isNil_nil (.ref rfl)) (.ref rfl)
  | c :: cs, a, _, _, _, ha, hc => by
      refine SEval_call2 ha hc rfl rfl ?_
      refine SEval.iteF (SEval_isNil_cons (.ref rfl)) ?_
      refine SEval_switch_of_tag (SEval_hd (.ref rfl)) rfl rfl ?_
      refine SEval.letN (d := .int (Int.ofNat (c.mask.log2 + 1))) ?_ ?_
      · exact SEval_prim2 (log2_agree c.mask (.ref rfl)) .lit (by simp [evalPrim])
      refine spanO_agree cs (if a ≤ c.mask.log2 + 1 then c.mask.log2 + 1 else a) ?_
        (SEval_tl (.ref rfl))
      by_cases hle : a ≤ c.mask.log2 + 1
      · rw [if_pos hle]
        exact SEval.iteT (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim] <;> omega))
          (.ref rfl)
      · rw [if_neg hle]
        exact SEval.iteF (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim] <;> omega))
          (.ref rfl)

theorem anyBit_agree : ∀ (cs : List MemCommit) (k : Nat) {σ : SEnv} {ec ek : SExp},
    SEval hwMemS σ ec (encListG encCommit cs) → SEval hwMemS σ ek (.int (Int.ofNat k)) →
    SEval hwMemS σ (.call "anyBit" [ec, ek]) (.bool (cs.any (fun c => c.mask.testBit k)))
  | [], k, _, _, _, hc, hk => by
      refine SEval_call2 hc hk rfl rfl ?_
      exact SEval.iteT (SEval_isNil_nil (.ref rfl)) .lit
  | c :: cs, k, _, _, _, hc, hk => by
      refine SEval_call2 hc hk rfl rfl ?_
      refine SEval.iteF (SEval_isNil_cons (.ref rfl)) ?_
      refine SEval_switch_of_tag (SEval_hd (.ref rfl)) rfl rfl ?_
      simp only [List.any_cons]
      refine SEval_ite_of_bool (bitAt_agree k c.mask (.ref rfl) (.ref rfl)) ?_ ?_
      · intro hb; rw [hb]; exact .lit
      · intro hb; rw [hb, Bool.false_or]
        exact anyBit_agree cs k (SEval_tl (.ref rfl)) (.ref rfl)

theorem inDom_agree (m : MemoryDesc) (k : Nat) {σ : SEnv} {eck ec ek : SExp}
    (hck : SEval hwMemS σ eck (.int (Int.ofNat m.clock)))
    (hc : SEval hwMemS σ ec (encListG encCommit m.commits.toList))
    (hk : SEval hwMemS σ ek (.int (Int.ofNat k))) :
    SEval hwMemS σ (.call "inDomO" [eck, ec, ek]) (.bool (m.inDom k)) := by
  refine SEval_call3 hck hc hk rfl rfl ?_
  rw [inDom_eq]
  by_cases h : k = m.clock
  · refine SEval.iteT (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim, h])) ?_
    simp [h]; exact .lit
  · refine SEval.iteF (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim] <;> omega)) ?_
    have : (k == m.clock) = false := by simp [h]
    rw [this, Bool.false_or]
    exact anyBit_agree _ k (.ref rfl) (.ref rfl)

/-- `allFireO` from `k`: `n` more clocks to go. -/
theorem allFireO_agree (m : MemoryDesc) (e : ClockEdges) (sp : Nat) (hsp : sp ≤ e.size) :
    ∀ (n k : Nat), sp = k + n → ∀ {σ : SEnv} {ee eck ec ek esp : SExp},
    SEval hwMemS σ ee (encEdges e) →
    SEval hwMemS σ eck (.int (Int.ofNat m.clock)) →
    SEval hwMemS σ ec (encListG encCommit m.commits.toList) →
    SEval hwMemS σ ek (.int (Int.ofNat k)) →
    SEval hwMemS σ esp (.int (Int.ofNat sp)) →
    SEval hwMemS σ (.call "allFireO" [ee, eck, ec, ek, esp]) (.bool (allFrom (allFireAt m e) k sp))
  | 0, k, hn, _, _, _, _, _, _, he, hck, hc, hk, hs => by
      refine SEval_call5 he hck hc hk hs rfl rfl ?_
      rw [show allFrom (allFireAt m e) k sp = true by unfold allFrom; rw [if_neg (by omega)]]
      exact SEval.iteF (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim] <;> omega)) .lit
  | n + 1, k, hn, _, _, _, _, _, _, he, hck, hc, hk, hs => by
      refine SEval_call5 he hck hc hk hs rfl rfl ?_
      have hlt : k < sp := by omega
      rw [show allFrom (allFireAt m e) k sp
            = (allFireAt m e k && allFrom (allFireAt m e) (k + 1) sp) by
            conv => lhs; unfold allFrom
            rw [if_pos hlt]]
      refine SEval.iteT (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim] <;> omega)) ?_
      refine SEval_ite_of_bool (bb := allFireAt m e k) ?hat ?_ ?_
      case hat =>
        unfold allFireAt
        refine SEval_ite_of_bool (inDom_agree m k (.ref rfl) (.ref rfl) (.ref rfl)) ?_ ?_
        · intro h; rw [if_pos h]
          exact lift (firesAt_agree (by omega) (.ref rfl) (.ref rfl))
        · intro h; rw [if_neg (by simp [h])]; exact .lit
      · intro h; rw [h, Bool.true_and]
        exact allFireO_agree m e sp hsp n (k + 1) (by omega) (.ref rfl) (.ref rfl) (.ref rfl)
          (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim])) (.ref rfl)
      · intro h; rw [h, Bool.false_and]; exact .lit

/-- `firedIsO` from `k`: `n` more clocks to go. -/
theorem firedIsO_agree (m : MemoryDesc) (e : ClockEdges) (mask sp : Nat) (hsp : sp ≤ e.size) :
    ∀ (n k : Nat), sp = k + n → ∀ {σ : SEnv} {ee eck ec emask ek esp : SExp},
    SEval hwMemS σ ee (encEdges e) →
    SEval hwMemS σ eck (.int (Int.ofNat m.clock)) →
    SEval hwMemS σ ec (encListG encCommit m.commits.toList) →
    SEval hwMemS σ emask (.int (Int.ofNat mask)) →
    SEval hwMemS σ ek (.int (Int.ofNat k)) →
    SEval hwMemS σ esp (.int (Int.ofNat sp)) →
    SEval hwMemS σ (.call "firedIsO" [ee, eck, ec, emask, ek, esp])
      (.bool (allFrom (firedAt m e mask) k sp))
  | 0, k, hn, _, _, _, _, _, _, _, he, hck, hc, hm, hk, hs => by
      refine SEval_call6 he hck hc hm hk hs rfl rfl ?_
      rw [show allFrom (firedAt m e mask) k sp = true by unfold allFrom; rw [if_neg (by omega)]]
      exact SEval.iteF (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim] <;> omega)) .lit
  | n + 1, k, hn, _, _, _, _, _, _, _, he, hck, hc, hm, hk, hs => by
      refine SEval_call6 he hck hc hm hk hs rfl rfl ?_
      have hlt : k < sp := by omega
      rw [show allFrom (firedAt m e mask) k sp
            = (firedAt m e mask k && allFrom (firedAt m e mask) (k + 1) sp) by
            conv => lhs; unfold allFrom
            rw [if_pos hlt]]
      refine SEval.iteT (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim] <;> omega)) ?_
      refine SEval.letN (d := .bool (if m.inDom k then fires e k else false)) ?_ ?_
      · refine SEval_ite_of_bool (inDom_agree m k (.ref rfl) (.ref rfl) (.ref rfl)) ?_ ?_
        · intro h; rw [if_pos h]
          exact lift (firesAt_agree (by omega) (.ref rfl) (.ref rfl))
        · intro h; rw [if_neg (by simp [h])]; exact .lit
      refine SEval_ite_of_bool (bb := firedAt m e mask k) ?hat ?_ ?_
      case hat =>
        unfold firedAt
        refine SEval_ite_of_bool (bitAt_agree k mask (.ref rfl) (.ref rfl)) ?_ ?_
        · intro h; simp only [h, ite_true]; exact .ref rfl
        · intro h; simp only [h, Bool.false_eq_true, ite_false]
          exact SEval_prim1 (.ref rfl) rfl
      · intro h; rw [h, Bool.true_and]
        exact firedIsO_agree m e mask sp hsp n (k + 1) (by omega) (.ref rfl) (.ref rfl) (.ref rfl)
          (.ref rfl) (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim])) (.ref rfl)
      · intro h; rw [h, Bool.false_and]; exact .lit

#audit_axioms allFrom_range'
#audit_axioms domSpan_eq
#audit_axioms allFire_eq
#audit_axioms firedIs_eq
#audit_axioms bitAt_agree
#audit_axioms log2_agree
#audit_axioms spanO_agree
#audit_axioms anyBit_agree
#audit_axioms inDom_agree
#audit_axioms allFireO_agree
#audit_axioms firedIsO_agree

end HwMem
end Projection
