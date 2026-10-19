/-
# Scalar operator preservation in a MIXED scalar/memory environment

## The gap this closes, stated exactly

`Hw.applyOp_agree` — and every per-operator lemma behind it — takes the GLOBAL
premise `SlotVals vals arg`, i.e. `∀ k < vals.length, vals[k] = encBV (arg k)`.
In a real memory-bearing design the slot environment contains `encMemImg`
values, and `encMemImg m = encBV b` is false for every `m` and `b`.  So the
global premise is UNSATISFIABLE in the environment the memory route actually
runs in, and `applyOpM_scalar` (§63.32) could not be instantiated there even
though the interpreter answers correctly.  That is a real proof gap, not a
runtime defect, and this module closes it.

## How, without weakening anything

`SlotVals` is UNCHANGED, and so is every theorem stated with it: `Hw.SlotVals`,
`Hw.slot_read` and the whole `Hw.*_agree` family keep their exact meanings and
are still the lemmas the memory-free route uses.  What is added is the LOCAL
condition — on the dependencies actually read, and nothing else:

```
SlotHere vals arg d  :=  ∃ h : d < vals.length, vals[d]'h = encBV (arg d)
```

`SlotHere` carries the bound AND the scalar reading together, so it replaces the
existing `d < vals.length` premises ONE FOR ONE: every `hb` in the mirrored
family has the same name, the same position and the same weakening
(`fun x hx => hb x (List.mem_cons_of_mem _ hx)`).  No premise was dropped: the
old bound is `(SlotHere vals arg d).1`, recovered by `SlotHere.bound`.

`SlotVals_SlotHere` proves the old global premise implies the new local one at
every dependency, so the mirrored lemmas SUBSUME the originals — the old
statements are corollaries, which is why no meaning is lost.

No scalar view of the environment is constructed, and **no memory image is
dropped or replaced to manufacture `SlotVals`**: the environment the mirrored
lemmas run in is the unmodified interpreter environment `objEnv vals`, images
and all.  `slot_agree` is kind-agnostic — it returns `vals[d]` for ANY `vals` —
so the only thing the scalar premise is ever used for is converting that slot's
value, which is exactly what `SlotHere` provides.  There is therefore no
"proof-only scalar view" here and nothing to bridge observationally.

## Coverage

All seventeen `OpSupported` operators, through the same helper chain the
original uses: the folds (`Op_And`/`Op_Or`/`Op_Xor`/`Op_SHL`), the reductions
(`Op_Ror`/`Op_EQ`), `Op_Sum`'s payload split, `Op_MuxN`'s direct pick, the
fixed-arity `Op_Not`/`Op_SRA`/`Op_GetMask`/`Op_MuxBool`/`Op_Sext`, and the four
comparisons.  SHL, SRA and Sext are covered with the rest.

The BV-level correctness is REUSED, not restated: the mirrored proofs are the
original proofs with the slot read localized, so `evalOp_Ror`, `muxAt`,
`cmpBool`, `cmpLt_agree`, `nz_agree`, `slot_agree` and the `OperatorBridge`
equations are the same facts in the same places.
-/
import LeanSemanticPrimitives.Projection.HardwareMemOp
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace HwLocal

open Surface Compiler Projection.Hw

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## The local scalar condition -/

/-- Slot `d` exists and holds a BIT VECTOR.  The bound and the scalar reading
travel together so that this premise replaces `d < vals.length` one for one. -/
def SlotHere (vals : List Val) (arg : Nat → BV) (d : Nat) : Prop :=
  ∃ h : d < vals.length, vals[d]'h = encBV (arg d)

theorem SlotHere.bound {vals : List Val} {arg : Nat → BV} {d : Nat}
    (h : SlotHere vals arg d) : d < vals.length := h.1

/-- **The old global premise implies the new local one**, so every mirrored
lemma below subsumes its original and no meaning is lost. -/
theorem SlotVals_SlotHere {vals : List Val} {arg : Nat → BV} (h : SlotVals vals arg)
    {d : Nat} (hd : d < vals.length) : SlotHere vals arg d := ⟨hd, h d hd⟩

/-- The localized slot read.  `slot_agree` is kind-agnostic — it answers
`vals[d]` whatever kind that slot holds — so this is the ONLY place the scalar
condition is consumed, exactly as `Hw.slot_read` consumed `SlotVals`. -/
theorem slot_readH {vals : List Val} {arg : Nat → BV} {d : Nat} {σ : SEnv}
    {ee en es : SExp} (h : SlotHere vals arg d)
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hes : SEval hwS σ es (.int (Int.ofNat d))) :
    SEval hwS σ (.call "slot" [ee, en, es]) (encBV (arg d)) := by
  obtain ⟨hd, hv⟩ := h
  rw [← hv]
  exact slot_agree hd henv hlen hes

/-! ## The mirrored operator family

Generated from `HardwareAdequacy`'s originals by exactly three changes: the
global `hsv` premise is deleted, each `d < vals.length` becomes
`SlotHere vals arg d`, and `slot_read hsv` becomes `slot_readH`.  Everything
else — every arithmetic step, every `eval_op` equation, every helper — is the
original proof. -/

theorem foldAnd_agreeOn {vals : List Val} {arg : Nat → BV}
    (w : Nat) :
    ∀ (deps : List Nat) (acc : BV) (σ : SEnv) (ed ee en ea ew : SExp),
      (∀ d ∈ deps, SlotHere vals arg d) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ ea (encBV acc) →
      SEval hwS σ ew (.int (Int.ofNat w)) →
      SEval hwS σ (.call "foldAnd" [ed, ee, en, ea, ew])
        (encBV ((deps.map arg).foldl (fun a b => bv_bitwise w (fun x y => x && y) a b) acc))
  | [],      _, _, _, _, _, _, _, _,  hdv, henv, hlen, hacc, hw => by
      refine SEval_call5 hdv henv hlen hacc hw rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (.ref rfl)
  | d :: ds, _, _, _, _, _, _, _, hb, hdv, henv, hlen, hacc, hw => by
      refine SEval_call5 hdv henv hlen hacc hw rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact foldAnd_agreeOn w ds _ _ _ _ _ _ _
        (fun x hx => hb x (List.mem_cons_of_mem _ hx))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_prim3 (.ref rfl) (.ref rfl)
          (slot_readH (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl)
        (.ref rfl)

theorem opAnd_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, SlotHere vals arg d)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opAnd" [ew, ed, ee, en])
      (encBV (eval_op .Op_And w (deps.map arg))) := by
  cases deps with
  | nil =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (SEval_prim2 (.ref rfl) .lit rfl)
  | cons d ds =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact foldAnd_agreeOn w ds _ _ _ _ _ _ _
        (fun x hx => hb x (List.mem_cons_of_mem _ hx))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_prim2 (.ref rfl)
          (slot_readH (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl)
        (.ref rfl)

theorem foldOr_agreeOn {vals : List Val} {arg : Nat → BV}
    (w : Nat) :
    ∀ (deps : List Nat) (acc : BV) (σ : SEnv) (ed ee en ea ew : SExp),
      (∀ d ∈ deps, SlotHere vals arg d) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ ea (encBV acc) →
      SEval hwS σ ew (.int (Int.ofNat w)) →
      SEval hwS σ (.call "foldOr" [ed, ee, en, ea, ew])
        (encBV ((deps.map arg).foldl (fun a b => bv_bitwise w (fun x y => x || y) a b) acc))
  | [],      _, _, _, _, _, _, _, _,  hdv, henv, hlen, hacc, hw => by
      refine SEval_call5 hdv henv hlen hacc hw rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (.ref rfl)
  | d :: ds, _, _, _, _, _, _, _, hb, hdv, henv, hlen, hacc, hw => by
      refine SEval_call5 hdv henv hlen hacc hw rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact foldOr_agreeOn w ds _ _ _ _ _ _ _
        (fun x hx => hb x (List.mem_cons_of_mem _ hx))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_prim3 (.ref rfl) (.ref rfl)
          (slot_readH (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl)
        (.ref rfl)

theorem opOr_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, SlotHere vals arg d)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opOr" [ew, ed, ee, en])
      (encBV (eval_op .Op_Or w (deps.map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  exact foldOr_agreeOn w deps _ _ _ _ _ _ _ hb (.ref rfl) (.ref rfl) (.ref rfl)
    (SEval_prim2 (.ref rfl) .lit rfl) (.ref rfl)

theorem foldXor_agreeOn {vals : List Val} {arg : Nat → BV}
    (w : Nat) :
    ∀ (deps : List Nat) (acc : BV) (σ : SEnv) (ed ee en ea ew : SExp),
      (∀ d ∈ deps, SlotHere vals arg d) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ ea (encBV acc) →
      SEval hwS σ ew (.int (Int.ofNat w)) →
      SEval hwS σ (.call "foldXor" [ed, ee, en, ea, ew])
        (encBV ((deps.map arg).foldl (fun a b => bv_bitwise w (fun x y => xor x y) a b) acc))
  | [],      _, _, _, _, _, _, _, _,  hdv, henv, hlen, hacc, hw => by
      refine SEval_call5 hdv henv hlen hacc hw rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (.ref rfl)
  | d :: ds, _, _, _, _, _, _, _, hb, hdv, henv, hlen, hacc, hw => by
      refine SEval_call5 hdv henv hlen hacc hw rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact foldXor_agreeOn w ds _ _ _ _ _ _ _
        (fun x hx => hb x (List.mem_cons_of_mem _ hx))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_prim3 (.ref rfl) (.ref rfl)
          (slot_readH (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl)
        (.ref rfl)

theorem opXor_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, SlotHere vals arg d)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opXor" [ew, ed, ee, en])
      (encBV (eval_op .Op_Xor w (deps.map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  exact foldXor_agreeOn w deps _ _ _ _ _ _ _ hb (.ref rfl) (.ref rfl) (.ref rfl)
    (SEval_prim2 (.ref rfl) .lit rfl) (.ref rfl)

theorem anyNz_agreeOn {vals : List Val} {arg : Nat → BV} :
    ∀ (deps : List Nat) (σ : SEnv) (ed ee en : SExp),
      (∀ d ∈ deps, SlotHere vals arg d) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ (.call "anyNz" [ed, ee, en])
        (.bool ((deps.map arg).any bv_nonzero))
  | [],      _, _, _, _, _,  hdv, henv, hlen => by
      refine SEval_call3 hdv henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) .lit
  | d :: ds, _, _, _, _, hb, hdv, henv, hlen => by
      refine SEval_call3 hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact SEval_prim2
        (nz_agree (slot_readH (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
          (SEval_hd (.ref rfl))))
        (anyNz_agreeOn ds _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl))
        rfl

theorem opRor_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, SlotHere vals arg d)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opRor" [ew, ed, ee, en])
      (encBV (eval_op .Op_Ror w (deps.map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  rw [evalOp_Ror]
  refine SEval_ite_of_bool (bb := (deps.map arg).any bv_nonzero)
    (anyNz_agreeOn deps _ _ _ _ hb (.ref rfl) (.ref rfl) (.ref rfl)) ?_ ?_
  · intro hT; simp only [hT, if_true]; exact SEval_prim2 (.ref rfl) .lit rfl
  · intro hF
    simp only [hF, Bool.false_eq_true, if_false]
    exact SEval_prim2 (.ref rfl) .lit rfl

theorem eqAll_agreeOn {vals : List Val} {arg : Nat → BV}
    (a : Int) :
    ∀ (deps : List Nat) (σ : SEnv) (ed ee en ea : SExp),
      (∀ d ∈ deps, SlotHere vals arg d) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ ea (.int a) →
      SEval hwS σ (.call "eqAll" [ed, ee, en, ea])
        (.bool ((deps.map arg).all fun b => bv_uint b = a))
  | [],      _, _, _, _, _, _,  hdv, henv, hlen, ha => by
      refine SEval_call4 hdv henv hlen ha rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) .lit
  | d :: ds, _, _, _, _, _, hb, hdv, henv, hlen, ha => by
      refine SEval_call4 hdv henv hlen ha rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact SEval_prim2
        (SEval_prim2
          (SEval_prim1 (slot_readH (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl)
          (.ref rfl) rfl)
        (eqAll_agreeOn a ds _ _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl) (.ref rfl))
        rfl

theorem opEq_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, SlotHere vals arg d)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opEq" [ew, ed, ee, en])
      (encBV (eval_op .Op_EQ w (deps.map arg))) := by
  cases deps with
  | nil =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (SEval_prim2 (.ref rfl) .lit rfl)
  | cons d ds =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      rw [List.map_cons, evalOp_EQ_cons]
      refine SEval_ite_of_bool
        (bb := (ds.map arg).all fun b => bv_uint b = bv_uint (arg d))
        (eqAll_agreeOn (bv_uint (arg d)) ds _ _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
          (SEval_prim1 (slot_readH (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl)) ?_ ?_
      · intro hT; simp only [hT, if_true]; exact SEval_prim2 (.ref rfl) .lit rfl
      · intro hF
        simp only [hF, Bool.false_eq_true, if_false]
        exact SEval_prim2 (.ref rfl) .lit rfl

theorem sumAdds_agreeOn {vals : List Val} {arg : Nat → BV} :
    ∀ (deps : List Nat) (k : Nat) (σ : SEnv) (ek ed ee en : SExp),
      (∀ d ∈ deps, SlotHere vals arg d) →
      SEval hwS σ ek (.int (Int.ofNat k)) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ (.call "sumAdds" [ek, ed, ee, en])
        (.int ((((deps.map arg).map bv_uint).take k).sum))
  | [],      _,     _, _, _, _, _, _,  hk, hdv, henv, hlen => by
      refine SEval_call4 hk hdv henv hlen rfl rfl ?_
      refine SEval.iteT (SEval_prim1 (.ref rfl) rfl) ?_
      simp only [List.map_nil, List.take_nil, List.sum_nil]
      exact .lit
  | _ :: _,  0,     _, _, _, _, _, _,  hk, hdv, henv, hlen => by
      refine SEval_call4 hk hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl) ?_
      simp only [List.take_zero, List.sum_nil]
      exact .lit
  | d :: ds, m + 1, _, _, _, _, _, hb, hk, hdv, henv, hlen => by
      refine SEval_call4 hk hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval.iteF (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim]; omega)) ?_
      exact SEval_prim2
        (SEval_prim1 (slot_readH (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
          (SEval_hd (.ref rfl))) rfl)
        (sumAdds_agreeOn ds m _ _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim]))
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl))
        rfl

theorem sumSubs_agreeOn {vals : List Val} {arg : Nat → BV} :
    ∀ (deps : List Nat) (k : Nat) (σ : SEnv) (ek ed ee en : SExp),
      (∀ d ∈ deps, SlotHere vals arg d) →
      SEval hwS σ ek (.int (Int.ofNat k)) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ (.call "sumSubs" [ek, ed, ee, en])
        (.int ((((deps.map arg).map bv_uint).drop k).sum))
  | [],      _,     _, _, _, _, _, _,  hk, hdv, henv, hlen => by
      refine SEval_call4 hk hdv henv hlen rfl rfl ?_
      refine SEval.iteT (SEval_prim1 (.ref rfl) rfl) ?_
      simp only [List.map_nil, List.drop_nil, List.sum_nil]
      exact .lit
  | d :: ds, 0,     _, _, _, _, _, hb, hk, hdv, henv, hlen => by
      refine SEval_call4 hk hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl) ?_
      exact SEval_prim2
        (SEval_prim1 (slot_readH (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
          (SEval_hd (.ref rfl))) rfl)
        (sumSubs_agreeOn ds 0 _ _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          .lit (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl))
        rfl
  | d :: ds, m + 1, _, _, _, _, _, hb, hk, hdv, henv, hlen => by
      refine SEval_call4 hk hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval.iteF (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim]; omega)) ?_
      exact sumSubs_agreeOn ds m _ _ _ _ _
        (fun x hx => hb x (List.mem_cons_of_mem _ hx))
        (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim]))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)

theorem opSum_agreeOn {vals : List Val} {arg : Nat → BV}
    {w nAdd : Nat} {deps : List Nat} {σ : SEnv} {ew ep ed ee en : SExp}
    (hb : ∀ d ∈ deps, SlotHere vals arg d)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hp : SEval hwS σ ep (.int (Int.ofNat nAdd)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opSum" [ew, ep, ed, ee, en])
      (encBV (eval_op (.Op_Sum nAdd) w (deps.map arg))) := by
  refine SEval_call5 hw hp hdv henv hlen rfl rfl ?_
  exact SEval_prim2 (.ref rfl)
    (SEval_prim2
      (sumAdds_agreeOn deps nAdd _ _ _ _ _ hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))
      (sumSubs_agreeOn deps nAdd _ _ _ _ _ hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))
      rfl)
    rfl

theorem foldShl_agreeOn {vals : List Val} {arg : Nat → BV}
    (w : Nat) (x : BV) :
    ∀ (bs : List Nat) (acc : BV) (σ : SEnv) (eb ee en ea ex ew : SExp),
      (∀ d ∈ bs, SlotHere vals arg d) →
      SEval hwS σ eb (encListG encNat bs) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ ea (encBV acc) →
      SEval hwS σ ex (encBV x) →
      SEval hwS σ ew (.int (Int.ofNat w)) →
      SEval hwS σ (.call "foldShl" [eb, ee, en, ea, ex, ew])
        (encBV ((bs.map arg).foldl
          (fun a b => bv_bitwise w (fun p q => xor p q) a (bv_shl_step w x b)) acc))
  | [],      _, _, _, _, _, _, _, _, _,  hbv, henv, hlen, hacc, hx, hw => by
      refine SEval_call6 hbv henv hlen hacc hx hw rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (.ref rfl)
  | d :: ds, _, _, _, _, _, _, _, _, hb, hbv, henv, hlen, hacc, hx, hw => by
      refine SEval_call6 hbv henv hlen hacc hx hw rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact foldShl_agreeOn w x ds _ _ _ _ _ _ _ _
        (fun y hy => hb y (List.mem_cons_of_mem _ hy))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_prim3 (.ref rfl) (.ref rfl)
          (SEval_prim3 (.ref rfl) (.ref rfl)
            (slot_readH (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
              (SEval_hd (.ref rfl))) rfl) rfl)
        (.ref rfl) (.ref rfl)

theorem opShl_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, SlotHere vals arg d)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opShl" [ew, ed, ee, en])
      (encBV (eval_op .Op_SHL w (deps.map arg))) := by
  cases deps with
  | nil =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (SEval_prim2 (.ref rfl) .lit rfl)
  | cons d ds =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      exact foldShl_agreeOn w (arg d) ds _ _ _ _ _ _ _ _
        (fun y hy => hb y (List.mem_cons_of_mem _ hy))
        (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (SEval_prim2 (.ref rfl) .lit rfl)
        (slot_readH (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
          (SEval_hd (.ref rfl)))
        (.ref rfl)

theorem muxPick_agreeOn {vals : List Val} {arg : Nat → BV}
    (w : Nat) (sel : Nat) :
    ∀ (args : List Nat) (k : Nat) (σ : SEnv) (ew ea ee en es ek : SExp),
      (∀ d ∈ args, SlotHere vals arg d) →
      SEval hwS σ ew (.int (Int.ofNat w)) →
      SEval hwS σ ea (encListG encNat args) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ es (.int (Int.ofNat sel)) →
      SEval hwS σ ek (.int (Int.ofNat k)) →
      SEval hwS σ (.call "muxPick" [ew, ea, ee, en, es, ek])
        (encBV (if k ≤ sel then muxAt w arg args (sel - k) else mk_bv w 0))
  | [],      k, _, _, _, _, _, _, _, _,  hw, hav, henv, hlen, hs, hk => by
      have hz : (if k ≤ sel then muxAt w arg [] (sel - k) else mk_bv w 0) = mk_bv w 0 := by
        simp [muxAt]
      rw [hz]
      refine SEval_call6 hw hav henv hlen hs hk rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (SEval_prim2 (.ref rfl) .lit rfl)
  | a :: rest, k, _, _, _, _, _, _, _, hb, hw, hav, henv, hlen, hs, hk => by
      by_cases hsk : sel = k
      · subst hsk
        have hz : (if sel ≤ sel then muxAt w arg (a :: rest) (sel - sel) else mk_bv w 0)
            = bv_resize w (arg a) := by simp [muxAt]
        rw [hz]
        refine SEval_call6 hw hav henv hlen hs hk rfl rfl ?_
        refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
        refine SEval.iteT (SEval_prim2 (.ref rfl) (.ref rfl) (by simp [evalPrim])) ?_
        exact SEval_prim2 (.ref rfl)
          (slot_readH (hb a List.mem_cons_self) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl))) rfl
      · have hstep : (if k ≤ sel then muxAt w arg (a :: rest) (sel - k) else mk_bv w 0)
            = (if k + 1 ≤ sel then muxAt w arg rest (sel - (k + 1)) else mk_bv w 0) := by
          by_cases h1 : k ≤ sel
          · have h2 : k + 1 ≤ sel := by omega
            have h3 : sel - k = (sel - (k + 1)) + 1 := by omega
            simp [h1, h2, h3, muxAt]
          · have h2 : ¬ (k + 1 ≤ sel) := by omega
            simp [h1, h2]
        rw [hstep]
        refine SEval_call6 hw hav henv hlen hs hk rfl rfl ?_
        refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
        refine SEval.iteF (SEval_prim2 (.ref rfl) (.ref rfl)
          (by simp [evalPrim] <;> omega)) ?_
        exact muxPick_agreeOn w sel rest (k + 1) _ _ _ _ _ _ _
          (fun y hy => hb y (List.mem_cons_of_mem _ hy))
          (.ref rfl) (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl) (.ref rfl)
          (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim] <;> omega))

theorem opMuxN_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, SlotHere vals arg d)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opMuxN" [ew, ed, ee, en])
      (encBV (eval_op .Op_MuxN w (deps.map arg))) := by
  cases deps with
  | nil =>
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) (SEval_prim2 (.ref rfl) .lit rfl)
  | cons d ds =>
      have hval : eval_op .Op_MuxN w ((d :: ds).map arg)
          = muxAt w arg ds (bv_uint (arg d)).toNat := by
        rw [List.map_cons, evalOp_MuxN_cons]
        simp only [muxAt, List.length_map, List.getElem?_map]
        by_cases hlt : (bv_uint (arg d)).toNat < ds.length
        · rw [if_pos hlt, List.getElem?_eq_getElem hlt]; simp
        · rw [if_neg hlt, List.getElem?_eq_none (Nat.le_of_not_lt hlt)]; simp
      rw [hval]
      refine SEval_call4 hw hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      have hz : muxAt w arg ds (bv_uint (arg d)).toNat
          = (if 0 ≤ (bv_uint (arg d)).toNat
             then muxAt w arg ds ((bv_uint (arg d)).toNat - 0) else mk_bv w 0) := by
        simp
      rw [hz]
      exact muxPick_agreeOn w (bv_uint (arg d)).toNat ds 0 _ _ _ _ _ _ _
        (fun y hy => hb y (List.mem_cons_of_mem _ hy))
        (.ref rfl) (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)
        (by rw [bv_uint_ofNat_toNat]
            exact SEval_prim1 (slot_readH (hb d List.mem_cons_self) (.ref rfl)
              (.ref rfl) (SEval_hd (.ref rfl))) rfl)
        .lit

theorem opNot_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : SlotHere vals arg d)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opNot" [ew, ed, ee, en])
      (encBV (eval_op .Op_Not w ([d].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  exact SEval_prim2 (.ref rfl)
    (slot_readH hb (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl))) rfl

theorem opSra_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb0 : SlotHere vals arg d0) (hb1 : SlotHere vals arg d1)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opSra" [ew, ed, ee, en])
      (encBV (eval_op .Op_SRA w ([d0, d1].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  exact SEval_prim3 (.ref rfl)
    (slot_readH hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
    (slot_readH hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl)))) rfl

theorem opGetMask_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb0 : SlotHere vals arg d0) (hb1 : SlotHere vals arg d1)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opGetMask" [ew, ed, ee, en])
      (encBV (eval_op .Op_GetMask w ([d0, d1].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  exact SEval_prim3 (.ref rfl)
    (slot_readH hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
    (slot_readH hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl)))) rfl

/-- Operands are `[sel, false_v, true_v]` IN THAT ORDER, so the nonzero branch
takes the THIRD.  A polarity slip here is a silent swap. -/

theorem opMuxBool_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d0 d1 d2 : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb0 : SlotHere vals arg d0) (hb1 : SlotHere vals arg d1) (hb2 : SlotHere vals arg d2)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1, d2]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opMuxBool" [ew, ed, ee, en])
      (encBV (eval_op .Op_MuxBool w ([d0, d1, d2].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  rw [List.map_cons, List.map_cons, List.map_cons, List.map_nil, evalOp_MuxBool]
  refine SEval_ite_of_bool (bb := bv_nonzero (arg d0))
    (nz_agree (slot_readH hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))) ?_ ?_
  · intro hT
    simp only [hT, if_true]
    exact SEval_prim2 (.ref rfl)
      (slot_readH hb2 (.ref rfl) (.ref rfl)
        (SEval_hd (SEval_tl (SEval_tl (.ref rfl))))) rfl
  · intro hF
    simp only [hF, Bool.false_eq_true, if_false]
    exact SEval_prim2 (.ref rfl)
      (slot_readH hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl)))) rfl

/-- Sign extension IS "truncate to the low `n` bits, then read them SIGNED".
The pinned body spells that out as a power/mod/sign formula; nothing here
re-derives it -- `OperatorBridge.evalOp_Sext` carries that obligation. -/

theorem opSext_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb0 : SlotHere vals arg d0) (hb1 : SlotHere vals arg d1)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opSext" [ew, ed, ee, en])
      (encBV (eval_op .Op_Sext w ([d0, d1].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  rw [List.map_cons, List.map_cons, List.map_nil, evalOp_Sext]
  exact SEval_prim2 (.ref rfl)
    (SEval_prim1
      (SEval_prim2
        (SEval_prim1 (slot_readH hb1 (.ref rfl) (.ref rfl)
          (SEval_hd (SEval_tl (.ref rfl)))) rfl)
        (slot_readH hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
        (prim_bvResize_int _ _))
      rfl)
    rfl

theorem opCmp_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d0 d1 : Nat} {signed swap : Bool} {σ : SEnv}
    {ew ed ee en es esw : SExp}
    (hb0 : SlotHere vals arg d0) (hb1 : SlotHere vals arg d1)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hs : SEval hwS σ es (.bool signed)) (hsw : SEval hwS σ esw (.bool swap)) :
    SEval hwS σ (.call "opCmp" [ew, ed, ee, en, es, esw])
      (encBV (mk_bv w (if cmpBool signed swap (arg d0) (arg d1) then 1 else 0))) := by
  refine SEval_call6 hw hdv henv hlen hs hsw rfl rfl ?_
  refine SEval_ite_of_bool (bb := cmpBool signed swap (arg d0) (arg d1))
    (cmpLt_agree (.ref rfl) (.ref rfl)
      (slot_readH hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
      (slot_readH hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl))))) ?_ ?_
  · intro hT; rw [hT]; exact SEval_prim2 (.ref rfl) .lit rfl
  · intro hF; rw [hF]; exact SEval_prim2 (.ref rfl) .lit rfl

theorem opULT_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en es esw : SExp}
    (hb0 : SlotHere vals arg d0) (hb1 : SlotHere vals arg d1)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hs : SEval hwS σ es (.bool false)) (hsw : SEval hwS σ esw (.bool false)) :
    SEval hwS σ (.call "opCmp" [ew, ed, ee, en, es, esw])
      (encBV (eval_op .Op_ULT w ([d0, d1].map arg))) := by
  have h := opCmp_agreeOn hb0 hb1 hw hdv henv hlen hs hsw
  simpa [cmpBool, evalOp_ULT] using h

theorem opUGT_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en es esw : SExp}
    (hb0 : SlotHere vals arg d0) (hb1 : SlotHere vals arg d1)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hs : SEval hwS σ es (.bool false)) (hsw : SEval hwS σ esw (.bool true)) :
    SEval hwS σ (.call "opCmp" [ew, ed, ee, en, es, esw])
      (encBV (eval_op .Op_UGT w ([d0, d1].map arg))) := by
  have h := opCmp_agreeOn hb0 hb1 hw hdv henv hlen hs hsw
  simpa [cmpBool, evalOp_UGT] using h

theorem opSLT_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en es esw : SExp}
    (hb0 : SlotHere vals arg d0) (hb1 : SlotHere vals arg d1)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hs : SEval hwS σ es (.bool true)) (hsw : SEval hwS σ esw (.bool false)) :
    SEval hwS σ (.call "opCmp" [ew, ed, ee, en, es, esw])
      (encBV (eval_op .Op_SLT w ([d0, d1].map arg))) := by
  have h := opCmp_agreeOn hb0 hb1 hw hdv henv hlen hs hsw
  simpa [cmpBool, evalOp_SLT] using h

theorem opSGT_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en es esw : SExp}
    (hb0 : SlotHere vals arg d0) (hb1 : SlotHere vals arg d1)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length)))
    (hs : SEval hwS σ es (.bool true)) (hsw : SEval hwS σ esw (.bool true)) :
    SEval hwS σ (.call "opCmp" [ew, ed, ee, en, es, esw])
      (encBV (eval_op .Op_SGT w ([d0, d1].map arg))) := by
  have h := opCmp_agreeOn hb0 hb1 hw hdv henv hlen hs hsw
  simpa [cmpBool, evalOp_SGT] using h


/-! ### D6 Tier 1, mirrored (the same three changes) -/

theorem multAll_agreeOn {vals : List Val} {arg : Nat → BV} :
    ∀ (deps : List Nat) (σ : SEnv) (ed ee en : SExp),
      (∀ d ∈ deps, SlotHere vals arg d) →
      SEval hwS σ ed (encListG encNat deps) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ (.call "multAll" [ed, ee, en])
        (.int (((deps.map arg).map bv_uint).prod))
  | [],      _, _, _, _, _,  hdv, henv, hlen => by
      refine SEval_call3 hdv henv hlen rfl rfl ?_
      refine SEval.iteT (SEval_prim1 (.ref rfl) rfl) ?_
      simp only [List.map_nil, List.prod_nil]
      exact .lit
  | d :: ds, _, _, _, _, hb, hdv, henv, hlen => by
      refine SEval_call3 hdv henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval_prim2
        (SEval_prim1 (slot_readH (hb d List.mem_cons_self) (.ref rfl) (.ref rfl)
          (SEval_hd (.ref rfl))) rfl)
        (multAll_agreeOn ds _ _ _ _ (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl)) ?_
      simp [evalPrim, List.prod_cons]

theorem opMult_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {deps : List Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb : ∀ d ∈ deps, SlotHere vals arg d)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opMult" [ew, ed, ee, en])
      (encBV (eval_op .Op_Mult w (deps.map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  exact SEval_prim2 (.ref rfl)
    (multAll_agreeOn deps _ _ _ _ hb (.ref rfl) (.ref rfl) (.ref rfl)) rfl

theorem opUDiv_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb0 : SlotHere vals arg d0) (hb1 : SlotHere vals arg d1)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opUDiv" [ew, ed, ee, en])
      (encBV (eval_op .Op_UDiv w ([d0, d1].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  exact SEval_prim2 (.ref rfl)
    (SEval_prim2
      (SEval_prim1 (slot_readH hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl))) rfl)
      (SEval_prim1 (slot_readH hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl)))) rfl)
      rfl)
    rfl

theorem opSDiv_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d0 d1 : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb0 : SlotHere vals arg d0) (hb1 : SlotHere vals arg d1)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opSDiv" [ew, ed, ee, en])
      (encBV (eval_op .Op_SDiv w ([d0, d1].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  refine SEval_call4 (.ref rfl)
    (SEval_prim1 (slot_readH hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl)))) rfl)
    (SEval_prim1 (slot_readH hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl))) rfl)
    (SEval_prim1 (slot_readH hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl)))) rfl)
    rfl rfl ?_
  show SEval hwS _ _ (encBV (bv_sdiv w (arg d0) (arg d1)))
  unfold bv_sdiv
  by_cases h0 : bv_uint (arg d1) = 0
  · refine SEval.iteT (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim, h0])) ?_
    simp only [h0, if_true]
    exact SEval_prim2 (.ref rfl) .lit rfl
  · refine SEval.iteF (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim, h0])) ?_
    simp only [h0, if_false]
    refine SEval_prim2 (.ref rfl)
      (signFix_agree (.ref rfl) (.ref rfl)
        (SEval_prim2 (absI_agree (.ref rfl)) (absI_agree (.ref rfl)) rfl)) ?_
    rw [truncDiv_obj]
    rfl

/-- `Op_SetMask`: `smLoop_agree`/`setBitV_agree` do not read the slot
environment, so only the three operand reads change. -/
theorem opSetMask_agreeOn {vals : List Val} {arg : Nat → BV}
    {w : Nat} {d0 d1 d2 : Nat} {σ : SEnv} {ew ed ee en : SExp}
    (hb0 : SlotHere vals arg d0) (hb1 : SlotHere vals arg d1) (hb2 : SlotHere vals arg d2)
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat [d0, d1, d2]))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "opSetMask" [ew, ed, ee, en])
      (encBV (eval_op .Op_SetMask w ([d0, d1, d2].map arg))) := by
  refine SEval_call4 hw hdv henv hlen rfl rfl ?_
  show SEval hwS _ _ (encBV (bv_set_mask w (arg d0) (arg d1) (arg d2)))
  rw [SetMaskLoop.set_mask_eq_smRun]
  exact SEval_prim2 (.ref rfl)
    (smLoop_agree w 0 (arg d0) 0 (by omega)
      (slot_readH hb0 (.ref rfl) (.ref rfl) (SEval_hd (.ref rfl)))
      (slot_readH hb1 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (.ref rfl))))
      (slot_readH hb2 (.ref rfl) (.ref rfl) (SEval_hd (SEval_tl (SEval_tl (.ref rfl)))))
      .lit .lit .lit (.ref rfl))
    rfl

theorem applyOp_agreeOn {vals : List Val} {arg : Nat → BV}
    {op : LGraphOp} {w : Nat} {deps : List Nat} {σ : SEnv} {eo ew ed ee en : SExp}
    (hsup : OpSupported op = true) (harity : ArityOK op deps)
    (hb : ∀ d ∈ deps, SlotHere vals arg d)
    (hop : SEval hwS σ eo (encOp op))
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "applyOp" [eo, ew, ed, ee, en])
      (encBV (eval_op op w (deps.map arg))) := by
  refine SEval_call5 hop hw hdv henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  cases op with
  | Op_And =>
      exact SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opAnd_agreeOn hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))
  | Op_Or =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opOr_agreeOn hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))
  | Op_SRA =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opSra_agreeOn (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))
  | Op_GetMask =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opGetMask_agreeOn (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))))
  | Op_Xor =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opXor_agreeOn hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))))
  | Op_Not =>
      obtain ⟨d0, rfl⟩ := list_len_one harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opNot_agreeOn (hb d0 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))))))
  | Op_Sum nAdd =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opSum_agreeOn hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))))))
  | Op_EQ =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opEq_agreeOn hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))))))))
  | Op_Ror =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opRor_agreeOn hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))))))))
  | Op_MuxBool =>
      obtain ⟨d0, d1, d2, rfl⟩ := list_len_three harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opMuxBool_agreeOn (hb d0 (by simp)) (hb d1 (by simp)) (hb d2 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))))))))))
  | Op_MuxN =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opMuxN_agreeOn hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))))))))))
  | Op_ULT =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opULT_agreeOn (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) .lit .lit))))))))))))
  | Op_UGT =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opUGT_agreeOn (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) .lit .lit)))))))))))))
  | Op_SLT =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opSLT_agreeOn (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) .lit .lit))))))))))))))
  | Op_SGT =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opSGT_agreeOn (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) .lit .lit)))))))))))))))
  | Op_Sext =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opSext_agreeOn (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))))))))))))))))
  | Op_SHL =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opShl_agreeOn hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))))))))))))))))
  | Op_Const c => exact absurd hsup (by simp [OpSupported])
  | Op_Sub => exact absurd hsup (by simp [OpSupported])
  | Op_Mult =>
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opMult_agreeOn hb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))))))))))))))))))
  | Op_Div => exact absurd hsup (by simp [OpSupported])
  | Op_UDiv =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opUDiv_agreeOn (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))))))))))))))))))
  | Op_SDiv =>
      obtain ⟨d0, d1, rfl⟩ := list_len_two harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opSDiv_agreeOn (hb d0 (by simp)) (hb d1 (by simp)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)))))))))))))))))))))
  | Op_LT => exact absurd hsup (by simp [OpSupported])
  | Op_GT => exact absurd hsup (by simp [OpSupported])
  | Op_SetMask =>
      obtain ⟨d0, d1, d2, rfl⟩ := list_len_three harity
      exact SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteF (SEval_prim2 (.ref rfl) .lit rfl)
              (SEval.iteT (SEval_prim2 (.ref rfl) .lit rfl)
              (opSetMask_agreeOn (hb d0 (by simp)) (hb d1 (by simp)) (hb d2 (by simp))
                (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))))))))))))))))))))))
  | Op_MemRead => exact absurd hsup (by simp [OpSupported])
  | Op_MemWrite => exact absurd hsup (by simp [OpSupported])
  | Op_MemWriteBE b => exact absurd hsup (by simp [OpSupported])

/-! ## The originals are corollaries

Stated to make the subsumption checkable rather than asserted: the global
premise gives the local one at every dependency, so `applyOp_agree`'s exact
statement follows from `applyOp_agreeOn`. -/

theorem applyOp_agree_of_On {vals : List Val} {arg : Nat → BV} (hsv : SlotVals vals arg)
    {op : LGraphOp} {w : Nat} {deps : List Nat} {σ : SEnv} {eo ew ed ee en : SExp}
    (hsup : OpSupported op = true) (harity : ArityOK op deps)
    (hb : ∀ d ∈ deps, d < vals.length)
    (hop : SEval hwS σ eo (encOp op))
    (hw : SEval hwS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwS σ ed (encListG encNat deps))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ en (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "applyOp" [eo, ew, ed, ee, en])
      (encBV (eval_op op w (deps.map arg))) :=
  applyOp_agreeOn hsup harity (fun d hd => SlotVals_SlotHere hsv (hb d hd))
    hop hw hdv henv hlen

#audit_axioms applyOp_agreeOn
#audit_axioms applyOp_agree_of_On
#audit_axioms slot_readH
#audit_axioms SlotVals_SlotHere


end HwLocal

/-! # The mixed-environment scalar operator bridge

`applyOpM` on the ACTUAL mixed environment — memory images still in it, nothing
dropped or replaced — with the scalar condition on the dependencies the node
actually reads and on nothing else. -/

namespace HwMem
open Surface Compiler Projection.Hw Projection.HwLocal

/-- **The replacement for `applyOpM_scalar` in a mixed environment.**  Premises:
`OpSupported`, the arity, and `SlotHere` at each dependency.  `vals` is
unrestricted elsewhere, so it may — and in every memory-bearing design does —
contain `encMemImg` slots. -/
theorem applyOpM_scalar_mixed {vals : List Val} {arg : Nat → BV}
    {op : LGraphOp} {w : Nat} {deps : List Nat} {σ : SEnv} {eo ew ed ee en : SExp}
    (hsup : OpSupported op = true) (harity : ArityOK op deps)
    (hb : ∀ d ∈ deps, SlotHere vals arg d)
    (hop : SEval hwMemS σ eo (encOp op))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat deps))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en])
      (encBV (eval_op op w (deps.map arg))) := by
  obtain ⟨h26, h28⟩ := OpSupported_code_ne hsup
  have h27 := OpSupported_code_ne27 hsup
  refine SEval_call5 hop hw hdv henv hlen rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit ?_) ?_
  · simp [evalPrim, encNat]
    omega
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit ?_) ?_
  · simp [evalPrim, encNat]
    omega
  refine SEval.iteF (SEval_prim2 (.ref rfl) .lit ?_) ?_
  · simp [evalPrim, encNat]
    omega
  exact lift (applyOp_agreeOn hsup harity hb (.ref rfl) (.ref rfl) (.ref rfl)
                (.ref rfl) (.ref rfl))

/-- …and against the reference's single `eval_op_cert`, with `SrcRel` carrying
the kind. -/
theorem applyOpM_scalar_mixed_cert {vals : List Val} {arg : Nat → BV}
    {op : LGraphOp} {w : Nat} {deps : List Nat} {σ : SEnv} {eo ew ed ee en : SExp}
    (hsup : OpSupported op = true) (harity : ArityOK op deps)
    (hb : ∀ d ∈ deps, SlotHere vals arg d)
    (hop : SEval hwMemS σ eo (encOp op))
    (hw : SEval hwMemS σ ew (.int (Int.ofNat w)))
    (hdv : SEval hwMemS σ ed (encListG encNat deps))
    (henv : SEval hwMemS σ ee (objEnv vals))
    (hlen : SEval hwMemS σ en (.int (Int.ofNat vals.length))) :
    ∃ v, SEval hwMemS σ (.call "applyOpM" [eo, ew, ed, ee, en]) v
       ∧ SrcRel v (eval_op_cert op w ((deps.map arg).map CertVal.bv)) := by
  refine ⟨_, applyOpM_scalar_mixed hsup harity hb hop hw hdv henv hlen, ?_⟩
  rw [eval_op_cert_bv op w (deps.map arg) (OpSupported_not_mem hsup)]
  rfl

/-! ## Non-vacuity: a genuinely mixed environment

The slot list below holds an `encMemImg` at slot 0 — a Pico-shaped 32-word,
32-bit image — and bit vectors at slots 1 and 2.  `SlotVals` is FALSE for it
(slot 0 is not an `encBV`), so the old global lemma cannot be used; `SlotHere`
holds at the two dependencies that are actually read, so the new one can. -/

private def mixedVals : List Val :=
  [ MemImg.encMemImg (MemImg.zeroImg 5 32)
  , encBV (mk_bv 8 0x0F)
  , encBV (mk_bv 8 0x3C) ]

private def mixedArg : Nat → BV
  | 1 => mk_bv 8 0x0F
  | 2 => mk_bv 8 0x3C
  | _ => mk_bv 8 0

/-- The two scalar dependencies satisfy `SlotHere` in the mixed list. -/
theorem mixed_SlotHere : ∀ d ∈ [1, 2], SlotHere mixedVals mixedArg d := by
  intro d hd
  have hd' : d = 1 ∨ d = 2 := by simpa using hd
  rcases hd' with rfl | rfl
  · exact ⟨by decide, rfl⟩
  · exact ⟨by decide, rfl⟩

/-- **`SlotVals` is false for the same list** — the premise the old lemma needs
cannot be supplied here, which is exactly the gap this module closes. -/
theorem mixed_not_SlotVals : ¬ SlotVals mixedVals mixedArg := by
  intro h
  have := h 0 (by decide)
  simp [mixedVals, mixedArg, MemImg.encMemImg, encBV, ofBV, mkBV,
        MemImg.zeroImg, MemImg.tagMemImg, bvTag] at this

/-- `Op_And` over the two scalar slots, in the environment that STILL holds the
image.  Nothing was dropped to make this provable. -/
example : SEval hwMemS []
    (.call "applyOpM"
       [ .lit (encOp .Op_And), .lit (.int 8), .lit (encListG encNat [1, 2])
       , .lit (objEnv mixedVals), .lit (.int 3) ])
    (encBV (eval_op .Op_And 8 ([1, 2].map mixedArg))) :=
  applyOpM_scalar_mixed (by decide) (by decide) mixed_SlotHere .lit .lit .lit .lit .lit

/-- `Op_SRA` in the same mixed environment — the operator whose correctness
repair must not be lost. -/
example : SEval hwMemS []
    (.call "applyOpM"
       [ .lit (encOp .Op_SRA), .lit (.int 8), .lit (encListG encNat [1, 2])
       , .lit (objEnv mixedVals), .lit (.int 3) ])
    (encBV (eval_op .Op_SRA 8 ([1, 2].map mixedArg))) :=
  applyOpM_scalar_mixed (by decide) (by decide) mixed_SlotHere .lit .lit .lit .lit .lit

private def applyOpMId' : Nat := (idOf "applyOpM").getD 999

/- …and the resolved program agrees, on the same mixed environment -/
#guard (match evalFuel 40000 hwMemP []
          (.call applyOpMId'
             [ .lit (encOp .Op_And), .lit (.int 8), .lit (encListG encNat [1, 2])
             , .lit (objEnv mixedVals), .lit (.int 3) ]) with
        | .value v => v == encBV (eval_op .Op_And 8 ([1, 2].map mixedArg))
        | _ => false)

#guard (match evalFuel 40000 hwMemP []
          (.call applyOpMId'
             [ .lit (encOp .Op_SRA), .lit (.int 8), .lit (encListG encNat [1, 2])
             , .lit (objEnv mixedVals), .lit (.int 3) ]) with
        | .value v => v == encBV (eval_op .Op_SRA 8 ([1, 2].map mixedArg))
        | _ => false)

#audit_axioms applyOpM_scalar_mixed
#audit_axioms applyOpM_scalar_mixed_cert
#audit_axioms mixed_SlotHere
#audit_axioms mixed_not_SlotVals

end HwMem
end Projection
