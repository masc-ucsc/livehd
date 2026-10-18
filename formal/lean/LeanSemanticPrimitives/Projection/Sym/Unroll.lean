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

/-- The property: output `k` is present and nonzero. -/
def okBit (outs : Array BV) (k : Nat) : Bool :=
  match outs[k]? with
  | some b => bv_nonzero b
  | none   => false

/-- The assumption: none, or an output that must be nonzero. -/
def asmBit (outs : Array BV) : Option Nat → Bool
  | none   => true
  | some k => okBit outs k

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

def asmOf (so : SVal) : Option Nat → Option BVLogicalExpr
  | none    => some (.const true)
  | some ai => (sNth so ai >>= bvOfS).map nzE

/-! ## The unroll -/

/-- One unrolled step: the property output, the assumption, the next state. -/
abbrev StepS := (Σ w, BVExpr w) × BVLogicalExpr × SVal

def unroll (D : DesignCert) (R : Program) (fuel okIdx : Nat) (asmIdx : Option Nat) (inW : List Nat) :
    Nat → Nat → SVal → SymSt → Except String (List StepS × SymSt)
  | 0, _, _, st => .ok ([], st)
  | j + 1, inB, sS, st =>
    match symStep R fuel st (.conc (encEdges (edgesAll D))) (inputVar inW inB) sS with
    | .error m => .error m
    | .ok (sres, st1) =>
      match splitResult sres with
      | none => .error "the residual's answer is not a result record"
      | some (sn, so) =>
        match sNth so okIdx >>= bvOfS with
        | none => .error "the property output is not a bit vector"
        | some ok =>
          match asmOf so asmIdx with
          | none => .error "the assumption output is not a bit vector"
          | some asm =>
            match unroll D R fuel okIdx asmIdx inW j (inB + inW.length) sn st1 with
            | .error m => .error m
            | .ok (rest, st2) => .ok ((ok, asm, sn) :: rest, st2)

theorem unroll_extends {D : DesignCert} {R : Program} {fuel okIdx : Nat} {asmIdx : Option Nat}
    {inW : List Nat} :
    ∀ (k inB : Nat) (sS : SVal) (st : SymSt) (steps : List StepS) (st' : SymSt),
      unroll D R fuel okIdx asmIdx inW k inB sS st = .ok (steps, st') → st.Extends st' ∧ steps.length = k
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
def StepFacts (σ : Asg) (okIdx : Nat) (asmIdx : Option Nat) (outs : Array BV) (stNext : StateM)
    (s : StepS) : Prop :=
  (nzE s.1).eval σ = okBit outs okIdx ∧ s.2.1.eval σ = asmBit outs asmIdx
    ∧ SRel σ s.2.2 (StateM.encStateM stNext)

theorem okOf_sound {σ : Asg} {so : SVal} {outs : Array BV} {okIdx : Nat} {ok : Σ w, BVExpr w}
    (hr : SRel σ so (encBVs outs)) (h : (sNth so okIdx >>= bvOfS) = some ok) :
    (nzE ok).eval σ = okBit outs okIdx := by
  simp only [Option.bind_eq_bind, Option.bind_eq_some_iff] at h
  obtain ⟨sk, hk, hb⟩ := h
  obtain ⟨b, hbk, rb⟩ := sNth_sound so outs.toList okIdx sk hr hk
  obtain ⟨w, e⟩ := ok
  obtain ⟨_, hu⟩ := bvOfS_sound rb hb
  rw [eval_nzE hu]
  unfold okBit
  rw [show outs[okIdx]? = outs.toList[okIdx]? by simp, hbk]

theorem asmOf_sound {σ : Asg} {so : SVal} {outs : Array BV} {asmIdx : Option Nat} {asm : BVLogicalExpr}
    (hr : SRel σ so (encBVs outs)) (h : asmOf so asmIdx = some asm) :
    asm.eval σ = asmBit outs asmIdx := by
  cases asmIdx with
  | none => simp [asmOf] at h; subst h; rfl
  | some ai =>
      simp only [asmOf, Option.map_eq_some_iff] at h
      obtain ⟨ok, hok, rfl⟩ := h
      exact okOf_sound hr hok

/-- **The unroll is sound.** -/
theorem unroll_sound {sf wf : Nat} {D : DesignCert} {R : Program} {fuel okIdx : Nat}
    {asmIdx : Option Nat} {inW : List Nat}
    (hproj : projectMemDesignAt sf wf D = .ok R) (hA : admitsB D = true)
    {s0 : StateM} {ins : Nat → RuntimeInput} (hins : ∀ n, (ins n).toList.map BV.width = inW)
    {σ : Asg} {stF : SymSt} (hc : stF.Consistent σ) :
    ∀ (k m inB : Nat) (sS : SVal) (st : SymSt) (steps : List StepS) (st' : SymSt),
      unroll D R fuel okIdx asmIdx inW k inB sS st = .ok (steps, st') → st'.Extends stF →
      (∀ j, j < k → AgreeAt σ (inB + j * inW.length) (inputVals (ins (m + j)))) →
      SRel σ sS (StateM.encStateM (traceM D s0 ins m)) →
      ∀ j (hj : j < steps.length),
        (∀ i, i ≤ j → runtimeFullOKB D (ins (m + i)) (traceM D s0 ins (m + i)) (edgesAll D) = true) →
        StepFacts σ okIdx asmIdx (outM D s0 ins (m + j)) (traceM D s0 ins (m + j + 1)) (steps[j]'hj)
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
                have f0 : StepFacts σ okIdx asmIdx (outM D s0 ins (m + 0)) (traceM D s0 ins (m + 0 + 1))
                    (ok, asm, sn) :=
                  ⟨okOf_sound ro hok, asmOf_sound ro hasm, rn⟩
                cases j with
                | zero => simpa using f0
                | succ j =>
                    -- the rest, from cycle m + 1
                    have ih := unroll_sound hproj hA hins hc k (m + 1) (inB + inW.length) sn st1 rest st2 h2 hx
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
