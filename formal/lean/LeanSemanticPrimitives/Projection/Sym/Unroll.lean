/-
`Sym.Unroll` -- `k` clock steps, symbolically, and what they denote (M3 S3).

A PROPERTY is a design output read as a bit: it holds when the output is
nonzero (`okBit`). An optional second output is an ASSUMPTION on the inputs
(`asmBit`). Both are written as ordinary RTL in a monitor wrapped around the
design, so they reach the certificate through the same exporter.

`unroll` runs `k` symbolic steps; step `j`'s inputs are fresh variables. For
each step it returns the property and assumption as `BVLogicalExpr`s and the
symbolic next state. `unroll_sound`: under an assignment satisfying the cut
definitions and holding a concrete run's state and inputs, each returned
expression evaluates to that run's bit, and each next state denotes its state.
-/
import LeanSemanticPrimitives.Projection.Sym.Shape
import LeanSemanticPrimitives.Projection.Sym.Sat

namespace Projection.Sym
open Std.Tactic.BVDecide Projection.MemProjection Projection.Hw Projection.HwMem Compiler

/-! ## Property bits -/

/-- A bit vector read as a Boolean: nonzero. -/
def nzE (p : Σ w, BVExpr w) : BVLogicalExpr :=
  .not (.literal (.bin p.2 .eq (.const 0#p.1)))

theorem eval_nzE {σ : Asg} {w : Nat} {e : BVExpr w} {b : BV}
    (hu : bv_uint b = ((e.eval σ).toNat : Int)) :
    (nzE ⟨w, e⟩).eval σ = bv_nonzero b := by
  simp only [nzE, BVLogicalExpr.eval, BoolExpr.eval, BVPred.eval, BVBinPred.eval, BVExpr.eval_const]
  unfold bv_nonzero
  rw [hu]
  by_cases h : e.eval σ = 0#w
  · simp [h]
  · have : (e.eval σ).toNat ≠ 0 := fun h0 => h (BitVec.eq_of_toNat_eq (by simpa using h0))
    simp [h, this]

def conjE : List BVLogicalExpr → BVLogicalExpr
  | []      => .const true
  | a :: as => .gate .and a (conjE as)

def disjE : List BVLogicalExpr → BVLogicalExpr
  | []      => .const false
  | a :: as => .gate .or a (disjE as)

theorem eval_conjE (σ : Asg) : ∀ as : List BVLogicalExpr, (conjE as).eval σ = as.all (·.eval σ)
  | [] => rfl
  | a :: as => by
      show (a.eval σ && (conjE as).eval σ) = _
      rw [eval_conjE σ as, List.all_cons]

theorem eval_disjE (σ : Asg) : ∀ as : List BVLogicalExpr, (disjE as).eval σ = as.any (·.eval σ)
  | [] => rfl
  | a :: as => by
      show (a.eval σ || (disjE as).eval σ) = _
      rw [eval_disjE σ as, List.any_cons]

theorem eval_and (σ : Asg) (a b : BVLogicalExpr) :
    BVLogicalExpr.eval σ (.gate .and a b) = (a.eval σ && b.eval σ) := rfl

theorem eval_or (σ : Asg) (a b : BVLogicalExpr) :
    BVLogicalExpr.eval σ (.gate .or a b) = (a.eval σ || b.eval σ) := rfl

theorem eval_not (σ : Asg) (a : BVLogicalExpr) :
    BVLogicalExpr.eval σ (.not a) = !a.eval σ := rfl

/-- A property: output `out` nonzero (`nz`) or zero (`!nz`). -/
structure PropSpec where
  out : Nat
  nz  : Bool := true
  deriving Repr, Inhabited

def propBit (outs : Array BV) (p : PropSpec) : Bool :=
  match outs[p.out]? with
  | some b => bv_nonzero b == p.nz
  | none   => false

def propE (p : PropSpec) (e : Σ w, BVExpr w) : BVLogicalExpr :=
  if p.nz then nzE e else .not (nzE e)

def propOf (so : SVal) (p : PropSpec) : Option BVLogicalExpr :=
  (sNth so p.out >>= bvOfS).map (propE p)

/-- Input assumptions: input `k` has (normalized) value `c` at every cycle. -/
def inAsmOK (i : RuntimeInput) (l : List (Nat × Int)) : Bool :=
  l.all fun q => match i[q.1]? with
    | some b => decide (bv_uint b = q.2)
    | none   => false

def inAsmLit (inW : List Nat) (inB : Nat) (q : Nat × Int) : BVLogicalExpr :=
  .literal (.bin (.var (inB + q.1) : BVExpr (inW.getD q.1 0)) .eq (.const (BitVec.ofInt (inW.getD q.1 0) q.2)))

def inAsmE (inW : List Nat) (inB : Nat) (l : List (Nat × Int)) : BVLogicalExpr :=
  conjE (l.map (inAsmLit inW inB))

/-- Every input assumption names a declared input and a value of its width. -/
def inAsmRangeOK (inW : List Nat) (l : List (Nat × Int)) : Bool :=
  l.all fun q => decide (q.1 < inW.length) && decide (0 ≤ q.2) && decide (q.2 < 2 ^ (inW.getD q.1 0))

/-- The assumption of a cycle: the output assumption, if any, and the input ones. -/
def asmBit (outs : Array BV) (i : RuntimeInput) (asmOut : Option PropSpec) (asmIn : List (Nat × Int)) :
    Bool :=
  (match asmOut with | none => true | some p => propBit outs p) && inAsmOK i asmIn

def asmOf (so : SVal) (asmOut : Option PropSpec) (inW : List Nat) (inB : Nat) (asmIn : List (Nat × Int)) :
    Option BVLogicalExpr :=
  let outE : Option BVLogicalExpr := match asmOut with
    | none   => some (.const true)
    | some p => propOf so p
  outE.map fun a => .gate .and a (inAsmE inW inB asmIn)

/-! ## The unroll -/

/-- One unrolled step: the property output, the assumption, the next state. -/
abbrev StepS := BVLogicalExpr × BVLogicalExpr × SVal

def unroll (D : DesignCert) (R : Program) (fuel : Nat) (prop : PropSpec) (asmOut : Option PropSpec)
    (asmIn : List (Nat × Int)) (inW : List Nat) :
    Nat → Nat → SVal → SymSt → Except String (List StepS × SymSt)
  | 0, _, _, st => .ok ([], st)
  | j + 1, inB, sS, st =>
    match symStep R fuel st (.conc (encEdges (edgesAll D))) (inputVar inW inB) sS with
    | .error m => .error m
    | .ok (sres, st1) =>
      match splitResult sres with
      | none => .error "the residual's answer is not a result record"
      | some (sn, so) =>
        match propOf so prop with
        | none => .error "the property output is not a bit vector"
        | some ok =>
          match asmOf so asmOut inW inB asmIn with
          | none => .error "the assumption output is not a bit vector"
          | some asm =>
            match unroll D R fuel prop asmOut asmIn inW j (inB + inW.length) sn st1 with
            | .error m => .error m
            | .ok (rest, st2) => .ok ((ok, asm, sn) :: rest, st2)

theorem unroll_extends {D : DesignCert} {R : Program} {fuel : Nat} {prop : PropSpec}
    {asmOut : Option PropSpec} {asmIn : List (Nat × Int)} {inW : List Nat} :
    ∀ (k inB : Nat) (sS : SVal) (st : SymSt) (steps : List StepS) (st' : SymSt),
      unroll D R fuel prop asmOut asmIn inW k inB sS st = .ok (steps, st') → st.Extends st' ∧ steps.length = k
  | 0, _, _, st, steps, st', h => by
      simp only [unroll, Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h; exact ⟨.refl _, rfl⟩
  | j + 1, inB, sS, st, steps, st', h => by
      unfold unroll at h
      split at h
      · cases h
      · rename_i sres st1 h1
        split at h
        · cases h
        · split at h
          · cases h
          · split at h
            · cases h
            · split at h
              · cases h
              · rename_i rest st2 h2
                simp only [Except.ok.injEq, Prod.mk.injEq] at h
                obtain ⟨rfl, rfl⟩ := h
                have ⟨x2, l2⟩ := unroll_extends j _ _ _ _ _ h2
                exact ⟨(symStep_extends h1).trans x2, by simp [l2]⟩

/-- The concrete facts a step of the unroll denotes. -/
def StepFacts (σ : Asg) (prop : PropSpec) (asmOut : Option PropSpec) (asmIn : List (Nat × Int))
    (outs : Array BV) (i : RuntimeInput) (stNext : StateM) (s : StepS) : Prop :=
  s.1.eval σ = propBit outs prop ∧ s.2.1.eval σ = asmBit outs i asmOut asmIn
    ∧ SRel σ s.2.2 (StateM.encStateM stNext)

theorem propOf_sound {σ : Asg} {so : SVal} {outs : Array BV} {p : PropSpec} {ok : BVLogicalExpr}
    (hr : SRel σ so (encBVs outs)) (h : propOf so p = some ok) :
    ok.eval σ = propBit outs p := by
  simp only [propOf, Option.map_eq_some_iff, Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨⟨w, e⟩, ⟨sk, hk, hb⟩, rfl⟩ := h
  obtain ⟨b, hbk, rb⟩ := sNth_sound so outs.toList p.out sk hr hk
  obtain ⟨_, hu⟩ := bvOfS_sound rb hb
  unfold propBit
  rw [show outs[p.out]? = outs.toList[p.out]? by simp, hbk]
  unfold propE
  cases hnz : p.nz
  · simp only [Bool.false_eq_true, if_false]
    show (!BVLogicalExpr.eval σ (nzE ⟨w, e⟩)) = _
    rw [eval_nzE hu]; cases bv_nonzero b <;> rfl
  · simp only [if_true]
    rw [eval_nzE hu]; cases bv_nonzero b <;> rfl

theorem eval_inAsmLit {σ : Asg} {inW : List Nat} {inB : Nat} {i : RuntimeInput}
    (hw : i.toList.map BV.width = inW) (ha : AgreeAt σ inB (inputVals i)) {q : Nat × Int}
    (hr : decide (q.1 < inW.length) && decide (0 ≤ q.2) && decide (q.2 < 2 ^ (inW.getD q.1 0)) = true) :
    (inAsmLit inW inB q).eval σ = (match i[q.1]? with
      | some b => decide (bv_uint b = q.2) | none => false) := by
  simp only [Bool.and_eq_true, decide_eq_true_eq] at hr
  obtain ⟨⟨hk, h0⟩, hc⟩ := hr
  have hlen : i.toList.length = inW.length := by rw [← hw]; simp
  have hki : q.1 < i.size := by simp at hlen; omega
  have hgi : i[q.1]? = some (i[q.1]'hki) := Array.getElem?_eq_getElem hki
  rw [hgi]
  generalize hb : i[q.1]'hki = b
  have hbw : b.width = inW.getD q.1 0 := by
    rw [← hw, ← hb]; simp [List.getD_eq_getElem?_getD, hgi]
  have hget : σ.get (inB + q.1) = pbvOf b := by
    apply ha q.1 (pbvOf b)
    simp only [inputVals, List.getElem?_map, Array.getElem?_toList, hgi, hb, Option.map_some]
  simp only [inAsmLit, BVLogicalExpr.eval, BoolExpr.eval, BVPred.eval, BVBinPred.eval,
    BVExpr.eval_const]
  rw [← hbw, eval_var_of hget]
  have hu0 := bv_uint_nonneg' b
  have hu1 := bv_uint_lt' b
  rw [two_pow_cast] at hu1
  rw [← hbw] at hc
  have hc' : q.2 < ((2 ^ b.width : Nat) : Int) := by rw [← two_pow_cast]; exact hc
  apply Bool.eq_iff_iff.mpr
  simp only [beq_iff_eq, decide_eq_true_eq]
  constructor
  · intro h
    have := congrArg BitVec.toNat h
    rw [BitVec.toNat_ofNat, BitVec.toNat_ofInt, Nat.mod_eq_of_lt (by omega)] at this
    have hm : q.2 % ((2 ^ b.width : Nat) : Int) = q.2 := Int.emod_eq_of_lt h0 hc'
    rw [hm] at this
    omega
  · intro h
    apply BitVec.eq_of_toNat_eq
    rw [BitVec.toNat_ofNat, BitVec.toNat_ofInt, Nat.mod_eq_of_lt (by omega)]
    have hm : q.2 % ((2 ^ b.width : Nat) : Int) = q.2 := Int.emod_eq_of_lt h0 hc'
    rw [hm]; omega

theorem eval_inAsmE {σ : Asg} {inW : List Nat} {inB : Nat} {i : RuntimeInput}
    (hw : i.toList.map BV.width = inW) (ha : AgreeAt σ inB (inputVals i)) :
    ∀ (l : List (Nat × Int)), inAsmRangeOK inW l = true → (inAsmE inW inB l).eval σ = inAsmOK i l
  | [], _ => rfl
  | q :: l, hr => by
      simp only [inAsmRangeOK, List.all_cons, Bool.and_eq_true] at hr
      simp only [inAsmE, List.map_cons, conjE, inAsmOK, List.all_cons]
      show (BVLogicalExpr.eval σ (inAsmLit inW inB q) && BVLogicalExpr.eval σ (conjE (l.map (inAsmLit inW inB)))) = _
      rw [eval_inAsmLit hw ha (by simpa using hr.1)]
      have := eval_inAsmE hw ha l (by simpa [inAsmRangeOK] using hr.2)
      simp only [inAsmE] at this
      rw [this]; rfl

theorem asmOf_sound {σ : Asg} {so : SVal} {outs : Array BV} {asmOut : Option PropSpec}
    {inW : List Nat} {inB : Nat} {asmIn : List (Nat × Int)} {i : RuntimeInput} {asm : BVLogicalExpr}
    (hr : SRel σ so (encBVs outs)) (hw : i.toList.map BV.width = inW) (ha : AgreeAt σ inB (inputVals i))
    (hrange : inAsmRangeOK inW asmIn = true) (h : asmOf so asmOut inW inB asmIn = some asm) :
    asm.eval σ = asmBit outs i asmOut asmIn := by
  simp only [asmOf, Option.map_eq_some_iff] at h
  obtain ⟨a, ha', rfl⟩ := h
  show (BVLogicalExpr.eval σ a && BVLogicalExpr.eval σ (inAsmE inW inB asmIn)) = _
  rw [eval_inAsmE hw ha asmIn hrange]
  unfold asmBit
  congr 1
  cases asmOut with
  | none => simp at ha'; subst ha'; rfl
  | some p => exact propOf_sound hr ha'

/-- **The unroll is sound.** -/
theorem unroll_sound {sf wf : Nat} {D : DesignCert} {R : Program} {fuel : Nat}
    {prop : PropSpec} {asmOut : Option PropSpec} {asmIn : List (Nat × Int)} {inW : List Nat}
    (hrange : inAsmRangeOK inW asmIn = true)
    (hproj : projectMemDesignAt sf wf D = .ok R) (hA : admitsB D = true)
    {s0 : StateM} {ins : Nat → RuntimeInput} (hins : ∀ n, (ins n).toList.map BV.width = inW)
    {σ : Asg} {stF : SymSt} (hc : stF.Consistent σ) :
    ∀ (k m inB : Nat) (sS : SVal) (st : SymSt) (steps : List StepS) (st' : SymSt),
      unroll D R fuel prop asmOut asmIn inW k inB sS st = .ok (steps, st') → st'.Extends stF →
      (∀ j, j < k → AgreeAt σ (inB + j * inW.length) (inputVals (ins (m + j)))) →
      SRel σ sS (StateM.encStateM (traceM D s0 ins m)) →
      ∀ j (hj : j < steps.length),
        (∀ i, i ≤ j → runtimeFullOKB D (ins (m + i)) (traceM D s0 ins (m + i)) (edgesAll D) = true) →
        StepFacts σ prop asmOut asmIn (outM D s0 ins (m + j)) (ins (m + j)) (traceM D s0 ins (m + j + 1))
          (steps[j]'hj)
  | 0, _, _, _, _, steps, _, h, _, _, _ => by
      simp only [unroll, Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h
      intro j hj; simp at hj
  | k + 1, m, inB, sS, st, steps, st', h, hx, hin, hS => by
      unfold unroll at h
      split at h
      · cases h
      · rename_i sres st1 h1
        split at h
        · cases h
        · rename_i sn so hsplit
          split at h
          · cases h
          · rename_i ok hok
            split at h
            · cases h
            · rename_i asm hasm
              split at h
              · cases h
              · rename_i rest st2 h2
                simp only [Except.ok.injEq, Prod.mk.injEq] at h
                obtain ⟨rfl, rfl⟩ := h
                have x2 := (unroll_extends k _ _ _ _ _ h2).1.trans hx
                intro j hj hwf
                -- the first step
                have hev := resM_residual hproj hA (hwf 0 (by omega))
                have rres := symStep_eval hev h1 hc x2 .conc
                  (inputVar_rel (hins (m + 0)) (by simpa using hin 0 (by omega))) hS
                obtain ⟨rn, ro⟩ := splitResult_sound rres hsplit
                have f0 : StepFacts σ prop asmOut asmIn (outM D s0 ins (m + 0)) (ins (m + 0))
                    (traceM D s0 ins (m + 0 + 1)) (ok, asm, sn) :=
                  ⟨propOf_sound ro hok,
                   asmOf_sound ro (hins (m + 0)) (by simpa using hin 0 (by omega)) hrange hasm, rn⟩
                cases j with
                | zero => simpa using f0
                | succ j =>
                    -- the rest, from cycle m + 1
                    have ih := unroll_sound hrange hproj hA hins hc k (m + 1) (inB + inW.length) sn st1 rest st2 h2 hx
                      (fun j hj => by
                        have := hin (j + 1) (by omega)
                        rwa [show inB + inW.length + j * inW.length = inB + (j + 1) * inW.length by
                          rw [Nat.succ_mul]; omega,
                          show m + 1 + j = m + (j + 1) by omega])
                      rn j (by simpa using hj)
                      (fun i hi => by
                        have := hwf (i + 1) (by omega); rwa [show m + 1 + i = m + (i + 1) by omega])
                    simp only [List.getElem_cons_succ]
                    rwa [show m + 1 + j = m + (j + 1) by omega, show m + (j + 1) + 1 = m + 1 + j + 1 by omega]
                      at *

end Projection.Sym
