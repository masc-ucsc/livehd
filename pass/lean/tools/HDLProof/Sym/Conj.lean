/-
`Sym.Conj` -- induction over a CONJUNCTION of properties, one STEP obligation
per conjunct (M8 A2), each restricted to its cone of influence (M8 A3).

`KInd` proves ONE output property with one monolithic step obligation. Here
the invariant is a list of conjuncts (each an output, nonzero or zero), and
conjunct `t`'s step obligation assumes only the conjuncts its hypothesis list
names (all of them by default):

  BASE      from the initial state, one step: the assumption holds and some
            conjunct fails;
  STEP(t)   from an ARBITRARY state of layout `L`, two steps: the assumption
            holds at both, the hypotheses of `t` hold at the first, and `t`
            fails at the second.

Fewer hypotheses only make an obligation stronger, so all the STEP(t)
together give the step for the whole conjunction (weakening). Each obligation
keeps only the cut definitions reachable from its own formula: any subset of
true definitions is implied by all of them, so this too only weakens what the
solver is asked to refute. The reachability itself is untrusted.

The checker on these obligations is `Sym.ConjT`'s `runConjTOf` (cases by template,
`runConjTOf_sound_ref`: on every run of the reference semantics from `s0`, at every cycle
whose assumptions up to it all held, every conjunct holds; k = 1). This module keeps what it
reuses: the unrolling with the outputs (`unrollO`), the cone of influence, the specification,
the two symbolic runs (`baseRunC`, `stepRunC`) and the side conditions (`sideOKC`). The first,
untemplated checker (`runConjOf`) was removed on 2026-11-27: nothing ran it. Mathlib-free.
-/
import HDLProof.Sym.KInd
import LeanSemanticPrimitives.Projection.AxiomGate

open Projection
namespace HDLProof.Sym
open Std.Tactic.BVDecide Projection.MemProjection Projection.Hw Projection.HwMem Compiler

/-! ## Unrolling with the outputs kept -/

/-- One unrolled step: the assumption, the OUTPUTS, the next state. -/
abbrev StepO := BVLogicalExpr × SVal × SVal

def unrollO (D : DesignCert) (R : Stepper) (fuel : Nat) (asmOut : Option PropSpec)
    (asmIn : List (Nat × Int)) (inW : List Nat) :
    Nat → Nat → SVal → SymSt → Except String (List StepO × SymSt)
  | 0, _, _, st => .ok ([], st)
  | j + 1, inB, sS, st =>
    match symStep R fuel st (.conc (encEdges (edgesAll D))) (inputVar inW inB) sS with
    | .error m => .error m
    | .ok (sres, st1) =>
      match splitResult sres with
      | none => .error "the residual's answer is not a result record"
      | some (sn, so) =>
        match asmOf so asmOut inW inB asmIn with
        | none => .error "the assumption output is not a bit vector"
        | some asm =>
          match unrollO D R fuel asmOut asmIn inW j (inB + inW.length) sn st1 with
          | .error m => .error m
          | .ok (rest, st2) => .ok ((asm, so, sn) :: rest, st2)

theorem unrollO_extends {D : DesignCert} {R : Stepper} {fuel : Nat}
    {asmOut : Option PropSpec} {asmIn : List (Nat × Int)} {inW : List Nat} :
    ∀ (k inB : Nat) (sS : SVal) (st : SymSt) (steps : List StepO) (st' : SymSt),
      unrollO D R fuel asmOut asmIn inW k inB sS st = .ok (steps, st') → st.Extends st' ∧ steps.length = k
  | 0, _, _, st, steps, st', h => by
      simp only [unrollO, Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h; exact ⟨.refl _, rfl⟩
  | j + 1, inB, sS, st, steps, st', h => by
      unfold unrollO at h
      split at h
      · cases h
      · rename_i sres st1 h1
        split at h
        · cases h
        · split at h
          · cases h
          · split at h
            · cases h
            · rename_i rest st2 h2
              simp only [Except.ok.injEq, Prod.mk.injEq] at h
              obtain ⟨rfl, rfl⟩ := h
              have ⟨x2, l2⟩ := unrollO_extends j _ _ _ _ _ h2
              exact ⟨(symStep_extends h1).trans x2, by simp [l2]⟩

/-- The concrete facts a step of `unrollO` denotes. -/
def StepFactsO (σ : Asg) (asmOut : Option PropSpec) (asmIn : List (Nat × Int))
    (outs : Array BV) (i : RuntimeInput) (stNext : StateM) (s : StepO) : Prop :=
  s.1.eval σ = asmBit outs i asmOut asmIn ∧ SRel σ s.2.1 (encBVs outs)
    ∧ SRel σ s.2.2 (StateM.encStateM stNext)

/-- **`unrollO` is sound** (as `unroll_sound`, the outputs kept). -/
theorem unrollO_sound {D : DesignCert} {R : Stepper} {fuel : Nat}
    {asmOut : Option PropSpec} {asmIn : List (Nat × Int)} {inW : List Nat}
    (hrange : inAsmRangeOK inW asmIn = true)
    (hres : MemResidual D R) (hA : admitsWB D = true)
    {s0 : StateM} {ins : Nat → RuntimeInput} (hins : ∀ n, (ins n).toList.map BV.width = inW)
    {σ : Asg} {stF : SymSt} (hc : stF.Consistent σ) :
    ∀ (k m inB : Nat) (sS : SVal) (st : SymSt) (steps : List StepO) (st' : SymSt),
      unrollO D R fuel asmOut asmIn inW k inB sS st = .ok (steps, st') → st'.Extends stF →
      (∀ j, j < k → AgreeAt σ (inB + j * inW.length) (inputVals (ins (m + j)))) →
      SRel σ sS (StateM.encStateM (traceM D s0 ins m)) →
      ∀ j (hj : j < steps.length),
        (∀ i, i ≤ j → runtimeFullOKB D (ins (m + i)) (traceM D s0 ins (m + i)) (edgesAll D) = true) →
        StepFactsO σ asmOut asmIn (outM D s0 ins (m + j)) (ins (m + j)) (traceM D s0 ins (m + j + 1))
          (steps[j]'hj)
  | 0, _, _, _, _, steps, _, h, _, _, _ => by
      simp only [unrollO, Except.ok.injEq, Prod.mk.injEq] at h
      obtain ⟨rfl, rfl⟩ := h
      intro j hj; simp at hj
  | k + 1, m, inB, sS, st, steps, st', h, hx, hin, hS => by
      unfold unrollO at h
      split at h
      · cases h
      · rename_i sres st1 h1
        split at h
        · cases h
        · rename_i sn so hsplit
          split at h
          · cases h
          · rename_i asm hasm
            split at h
            · cases h
            · rename_i rest st2 h2
              simp only [Except.ok.injEq, Prod.mk.injEq] at h
              obtain ⟨rfl, rfl⟩ := h
              have x2 := (unrollO_extends k _ _ _ _ _ h2).1.trans hx
              intro j hj hwf
              have rres := symStep_eval (hres hA (hwf 0 (by omega))) h1 hc x2 .conc
                (inputVar_rel (hins (m + 0)) (by simpa using hin 0 (by omega))) hS
              obtain ⟨rn, ro⟩ := splitResult_sound rres hsplit
              have f0 : StepFactsO σ asmOut asmIn (outM D s0 ins (m + 0)) (ins (m + 0))
                  (traceM D s0 ins (m + 0 + 1)) (asm, so, sn) :=
                ⟨asmOf_sound ro (hins (m + 0)) (by simpa using hin 0 (by omega)) hrange hasm, ro, rn⟩
              cases j with
              | zero => simpa using f0
              | succ j =>
                  have ih := unrollO_sound hrange hres hA hins hc k (m + 1) (inB + inW.length) sn st1 rest st2
                    h2 hx
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

/-! ## Several properties of one output vector -/

/-- The properties `ps` of the outputs `so`, in order. -/
def propsOf (so : SVal) (ps : List PropSpec) : Option (List BVLogicalExpr) := ps.mapM (propOf so)

theorem eval_conjE_propsOf {σ : Asg} {so : SVal} {outs : Array BV} (hr : SRel σ so (encBVs outs)) :
    ∀ (ps : List PropSpec) (es : List BVLogicalExpr), propsOf so ps = some es →
      (conjE es).eval σ = ps.all (propBit outs)
  | [], es, h => by simp [propsOf] at h; subst h; rfl
  | p :: ps, es, h => by
      simp only [propsOf, List.mapM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff,
        Option.pure_def, Option.some.injEq] at h
      obtain ⟨e, he, es', hes', rfl⟩ := h
      rw [conjE, eval_and, propOf_sound hr he, eval_conjE_propsOf hr ps es' hes', List.all_cons]

/-! ## Cone of influence -/

mutual
/-- The variables an expression reads. -/
def varsE : {w : Nat} → BVExpr w → List Nat → List Nat
  | _, .var i, acc => i :: acc
  | _, .const _, acc => acc
  | _, .extract _ _ e, acc => varsE e acc
  | _, .bin a _ b, acc => varsE a (varsE b acc)
  | _, .un _ e, acc => varsE e acc
  | _, .append a b _, acc => varsE a (varsE b acc)
  | _, .replicate _ e _, acc => varsE e acc
  | _, .shiftLeft a b, acc => varsE a (varsE b acc)
  | _, .shiftRight a b, acc => varsE a (varsE b acc)
  | _, .arithShiftRight a b, acc => varsE a (varsE b acc)
end

def varsP : BVPred → List Nat → List Nat
  | .bin a _ b, acc => varsE a (varsE b acc)
  | .getLsbD e _, acc => varsE e acc

def varsL : BVLogicalExpr → List Nat → List Nat
  | .literal p, acc => varsP p acc
  | .const _, acc => acc
  | .not a, acc => varsL a acc
  | .gate _ a b, acc => varsL a (varsL b acc)
  | .ite c a b, acc => varsL c (varsL a (varsL b acc))

/-- The definitions (by index) reachable from `start`: cut variable `base + k`
is defined by `defs[k]`. UNTRUSTED: any answer is sound (see `defsWhere`). -/
def reachDefs (st : SymSt) (start : List Nat) : Array Bool := Id.run do
  let n := st.defs.size
  let mut keep := Array.replicate n false
  let mut todo := start
  -- every definition is entered at most once, so `n + 1` rounds of the
  -- worklist suffice; the bound only makes the loop total
  for _ in [0:n + 1] do
    match todo with
    | [] => break
    | _ =>
      let mut next : List Nat := []
      for v in todo do
        if st.base ≤ v then
          let k := v - st.base
          if h : k < keep.size then
            if !keep[k] then
              keep := keep.set k true
              if h' : k < st.defs.size then
                next := varsE st.defs[k].2 next
      todo := next
  return keep

/-- The definitions `keep` selects, as one conjunction. -/
def defsWhere (st : SymSt) (keep : Nat → Bool) : BVLogicalExpr :=
  conjE ((st.defs.toList.zipIdx.filter (fun p => keep p.2)).map (defLit st.base))

theorem eval_defsWhere {σ : Asg} {st : SymSt} (hc : st.Consistent σ) (keep : Nat → Bool) :
    (defsWhere st keep).eval σ = true := by
  rw [defsWhere, eval_conjE, List.all_eq_true]
  intro x hx
  rw [List.mem_map] at hx
  obtain ⟨p, hp, rfl⟩ := hx
  have hp' := (List.mem_filter.mp hp).1
  have : (defsE st).eval σ = true := eval_defsE hc
  rw [defsE, eval_conjE, List.all_eq_true] at this
  exact this _ (List.mem_map.mpr ⟨p, hp', rfl⟩)

/-- `body` with only the definitions reachable from it. -/
def coi (st : SymSt) (body : BVLogicalExpr) : BVLogicalExpr :=
  let keep := reachDefs st (varsL body [])
  .gate .and (defsWhere st (fun k => keep.getD k false)) body

theorem eval_coi {σ : Asg} {st : SymSt} (hc : st.Consistent σ) {body : BVLogicalExpr}
    (hb : body.eval σ = true) : (coi st body).eval σ = true := by
  unfold coi; simp only; rw [eval_and, eval_defsWhere hc, hb]; rfl

/-! ## The specification and the obligations -/

structure ConjSpec where
  /-- the invariant: every one of these holds -/
  conjs  : List PropSpec
  /-- per conjunct (same position): the conjuncts its STEP assumes; `none`
  (or a missing entry) assumes all of them -/
  hyps   : List (Option (List Nat)) := []
  asmOut : Option PropSpec := none
  asmIn  : List (Nat × Int) := []
  fuel   : Nat
  inW    : List Nat
  L      : Shape

def ConjSpec.hypsOf (sp : ConjSpec) (t : Nat) : List Nat :=
  match sp.hyps[t]? with
  | some (some l) => l
  | _ => List.range sp.conjs.length

def ConjSpec.select (sp : ConjSpec) (idx : List Nat) : Option (List PropSpec) :=
  idx.mapM (sp.conjs[·]?)

theorem mem_of_mapM_getElem? {α : Type} {xs : List α} :
    ∀ {idx : List Nat} {ys : List α}, idx.mapM (xs[·]?) = some ys → ∀ y ∈ ys, y ∈ xs
  | [], ys, h => by simp at h; subst h; simp
  | i :: idx, ys, h => by
      simp only [List.mapM_cons, Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def,
        Option.some.injEq] at h
      obtain ⟨y0, hy0, ys', hys', rfl⟩ := h
      intro y hy
      rcases List.mem_cons.mp hy with rfl | hy
      · exact List.mem_of_getElem? hy0
      · exact mem_of_mapM_getElem? hys' y hy

def baseRunC (D : DesignCert) (R : Stepper) (sp : ConjSpec) (s0 : StateM) :
    Except String (List StepO × SymSt) :=
  unrollO D R sp.fuel sp.asmOut sp.asmIn sp.inW 1 0 (.conc (StateM.encStateM s0)) ⟨sp.inW.length, #[]⟩

def stepRunC (D : DesignCert) (R : Stepper) (sp : ConjSpec) : Except String (List StepO × SymSt) :=
  unrollO D R sp.fuel sp.asmOut sp.asmIn sp.inW 2 (layoutSize sp.L) (stateVar sp.L 0)
    ⟨layoutSize sp.L + 2 * sp.inW.length, #[]⟩

def sideOKC (D : DesignCert) (sp : ConjSpec) (s0 : StateM) : Bool :=
  stMatchesB sp.L s0 && stateFullOKB D s0 && inputOKB D sp.inW.length
    && flopBoundOKB D sp.L.fws.length && edgesOKB D (edgesAll D) && inAsmRangeOK sp.inW sp.asmIn

/-! ## Soundness -/

section
variable {D : DesignCert} {R : Stepper} {sp : ConjSpec} {s0 : StateM} {pfB : ByteArray}
  {pfS : List ByteArray} {ins : Nat → RuntimeInput}

theorem sideOKC_parts (h : sideOKC D sp s0 = true) :
    stMatchesB sp.L s0 = true ∧ stateFullOKB D s0 = true ∧ inputOKB D sp.inW.length = true
    ∧ flopBoundOKB D sp.L.fws.length = true ∧ edgesOKB D (edgesAll D) = true
    ∧ inAsmRangeOK sp.inW sp.asmIn = true := by
  unfold sideOKC at h
  simp only [Bool.and_eq_true] at h
  exact ⟨h.1.1.1.1.1, h.1.1.1.1.2, h.1.1.1.2, h.1.1.2, h.1.2, h.2⟩

theorem runtime_of_invC (hside : sideOKC D sp s0 = true) {st : StateM} {i : RuntimeInput}
    (hm : StMatches sp.L st) (hf : stateFullOKB D st = true) (hi : i.toList.map BV.width = sp.inW) :
    runtimeFullOKB D i st (edgesAll D) = true := by
  obtain ⟨_, _, hin, hfb, hed, _⟩ := sideOKC_parts hside
  unfold stateFullOKB at hf
  simp only [Bool.and_eq_true] at hf
  have hsz : i.size = sp.inW.length := by rw [← hi]; simp
  have hfs : st.flops.size = sp.L.fws.length := by rw [← hm.1]; simp
  unfold runtimeFullOKB runtimeOKB
  simp only [Bool.and_eq_true]
  exact ⟨⟨⟨⟨hf.1, by rw [hsz]; exact hin⟩, by rw [hfs]; exact hfb⟩, hed⟩, hf.2⟩

end

end HDLProof.Sym
