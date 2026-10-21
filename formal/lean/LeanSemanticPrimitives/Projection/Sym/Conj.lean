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

`runConjOf_sound_ref`: if `runConjOf` answers `true`, then on every run of the
reference semantics from `s0`, at every cycle whose assumptions up to it all
held, EVERY conjunct holds. k = 1. Mathlib-free.
-/
import LeanSemanticPrimitives.Projection.Sym.KInd
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection.Sym
open Std.Tactic.BVDecide Projection.MemProjection Projection.Hw Projection.HwMem Compiler

/-! ## Unrolling with the outputs kept -/

/-- One unrolled step: the assumption, the OUTPUTS, the next state. -/
abbrev StepO := BVLogicalExpr × SVal × SVal

def unrollO (D : DesignCert) (R : Program) (fuel : Nat) (asmOut : Option PropSpec)
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

theorem unrollO_extends {D : DesignCert} {R : Program} {fuel : Nat}
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
theorem unrollO_sound {D : DesignCert} {R : Program} {fuel : Nat}
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
              have hev := hres hA (hwf 0 (by omega))
              have rres := symStep_eval hev h1 hc x2 .conc
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

def baseRunC (D : DesignCert) (R : Program) (sp : ConjSpec) (s0 : StateM) :
    Except String (List StepO × SymSt) :=
  unrollO D R sp.fuel sp.asmOut sp.asmIn sp.inW 1 0 (.conc (StateM.encStateM s0)) ⟨sp.inW.length, #[]⟩

def stepRunC (D : DesignCert) (R : Program) (sp : ConjSpec) : Except String (List StepO × SymSt) :=
  unrollO D R sp.fuel sp.asmOut sp.asmIn sp.inW 2 (layoutSize sp.L) (stateVar sp.L 0)
    ⟨layoutSize sp.L + 2 * sp.inW.length, #[]⟩

/-- BASE, from the step `(a0, so0, _)` out of the initial state: the
assumption holds and some conjunct fails. -/
def baseOblOf (sp : ConjSpec) (st : SymSt) (s : StepO) : Option BVLogicalExpr :=
  (propsOf s.2.1 sp.conjs).map fun ps => coi st (.gate .and s.1 (.not (conjE ps)))

/-- STEP(t), from the two steps `s0 s1` out of an arbitrary state. -/
def stepOblOf (sp : ConjSpec) (st : SymSt) (s0 s1 : StepO) (t : Nat) : Option BVLogicalExpr := do
  let hp ← sp.select (sp.hypsOf t)
  let hs ← propsOf s0.2.1 hp
  let p ← sp.conjs[t]?
  let g ← propOf s1.2.1 p
  pure (coi st (conjE [s0.1, conjE hs, s1.1, .not g]))

def sideOKC (D : DesignCert) (sp : ConjSpec) (s0 : StateM) : Bool :=
  stMatchesB sp.L s0 && stateFullOKB D s0 && inputOKB D sp.inW.length
    && flopBoundOKB D sp.L.fws.length && edgesOKB D (edgesAll D) && inAsmRangeOK sp.inW sp.asmIn

/-- The check over the two symbolic runs. A driver that ran them once (to hand
the obligations to the solver) evaluates THIS on the same values, which is
`runConjOf` by definition. `pfS` holds one LRAT proof per conjunct, in order. -/
def checkConj (sp : ConjSpec) (rb rs : Except String (List StepO × SymSt)) (pfB : ByteArray)
    (pfS : List ByteArray) : Bool :=
  (match rb with
    | .ok ([s], st) =>
        scopedB st && (match baseOblOf sp st s with | some φ => checkUnsat φ pfB | none => false)
    | _ => false)
  && (match rs with
    | .ok ([s0, s1], st) =>
        scopedB st && decide (stateShapeOf s0.2.2 = some sp.L)
          && (List.range sp.conjs.length).all fun t =>
               match stepOblOf sp st s0 s1 t with
               | some φ => checkUnsat φ (pfS.getD t ByteArray.empty)
               | none => false
    | _ => false)

/-- The checker. -/
def runConjOf (D : DesignCert) (R : Program) (sp : ConjSpec) (s0 : StateM) (pfB : ByteArray)
    (pfS : List ByteArray) : Bool :=
  sideOKC D sp s0 && checkConj sp (baseRunC D R sp s0) (stepRunC D R sp) pfB pfS

/-! ## Soundness -/

section
variable {D : DesignCert} {R : Program} {sp : ConjSpec} {s0 : StateM} {pfB : ByteArray}
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

theorem conj_parts (h : runConjOf D R sp s0 pfB pfS = true) :
    sideOKC D sp s0 = true
    ∧ (∃ s st, baseRunC D R sp s0 = .ok ([s], st) ∧ scopedB st = true
        ∧ ∃ φ, baseOblOf sp st s = some φ ∧ BVLogicalExpr.Unsat φ)
    ∧ (∃ a b st, stepRunC D R sp = .ok ([a, b], st) ∧ scopedB st = true
        ∧ stateShapeOf a.2.2 = some sp.L
        ∧ ∀ t, t < sp.conjs.length → ∃ φ, stepOblOf sp st a b t = some φ ∧ BVLogicalExpr.Unsat φ) := by
  unfold runConjOf checkConj at h
  simp only [Bool.and_eq_true] at h
  obtain ⟨hside, hb, hs⟩ := h
  refine ⟨hside, ?_, ?_⟩
  · split at hb
    · rename_i s st hrun
      simp only [Bool.and_eq_true] at hb
      refine ⟨s, st, hrun, hb.1, ?_⟩
      cases ho : baseOblOf sp st s with
      | none => rw [ho] at hb; exact absurd hb.2 (by simp)
      | some φ => rw [ho] at hb; exact ⟨φ, rfl, checkUnsat_sound hb.2⟩
    · cases hb
  · split at hs
    · rename_i a b st hrun
      simp only [Bool.and_eq_true, decide_eq_true_eq, List.all_eq_true, List.mem_range] at hs
      obtain ⟨⟨hsc, hsh⟩, hall⟩ := hs
      refine ⟨a, b, st, hrun, hsc, hsh, fun t ht => ?_⟩
      have := hall t ht
      cases ho : stepOblOf sp st a b t with
      | none => rw [ho] at this; cases this
      | some φ => rw [ho] at this; exact ⟨φ, rfl, checkUnsat_sound this⟩
    · cases hs

/-- The layout and the memory route's runtime shape hold at EVERY cycle. -/
theorem invC_all (hres : MemResidual D R) (hA : admitsWB D = true)
    (hchk : runConjOf D R sp s0 pfB pfS = true) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, StMatches sp.L (traceM D s0 ins n) ∧ stateFullOKB D (traceM D s0 ins n) = true := by
  obtain ⟨hside, -, ⟨a, b, stS, hrun, hsc, hshape, -⟩⟩ := conj_parts hchk
  intro n
  induction n with
  | zero =>
      obtain ⟨hm, hf, _⟩ := sideOKC_parts hside
      exact ⟨stMatchesB_sound hm, hf⟩
  | succ n ih =>
      have rt := runtime_of_invC hside ih.1 ih.2 (hins n)
      refine ⟨?_, (resM_spec hA rt).2.2⟩
      have hb := (unrollO_extends 2 _ _ _ _ _ hrun).1.1
      let xs := stateVals (traceM D s0 ins n) ++ insVals ins n 2
      have hxl : xs.length = stS.base := by
        rw [← hb, List.length_append, stateVals_length ih.1, insVals_length hins]
      obtain ⟨hc, hag⟩ := extendSt_agree hxl hsc
      rw [AgreeAt_append, stateVals_length ih.1, Nat.zero_add] at hag
      have hS := stateVar_rel ih.1 hag.1
      have facts := unrollO_sound (sideOKC_parts hside).2.2.2.2.2 hres hA hins hc 2 n _ _ _ _ _
        hrun (.refl _) (insVals_agree hins 2 hag.2) hS 0 (by simp)
        (fun i hi => by
          have : i = 0 := by omega
          subst this; simpa using rt)
      exact stateShapeOf_sound facts.2.2 hshape

/-- **Conjunctive induction is sound.** -/
theorem runConjOf_sound (hres : MemResidual D R) (hA : admitsWB D = true)
    (hchk : runConjOf D R sp s0 pfB pfS = true) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, (∀ j, j ≤ n → asmBit (outM D s0 ins j) (ins j) sp.asmOut sp.asmIn = true) →
      sp.conjs.all (propBit (outM D s0 ins n)) = true := by
  obtain ⟨hside, ⟨sB, stB, hrunB, hscB, φB, hφB, hunsB⟩, ⟨a, b, stS, hrunS, hscS, -, hstep⟩⟩ :=
    conj_parts hchk
  have hinv := invC_all hres hA hchk hins
  have rt : ∀ n, runtimeFullOKB D (ins n) (traceM D s0 ins n) (edgesAll D) = true :=
    fun n => runtime_of_invC hside (hinv n).1 (hinv n).2 (hins n)
  have hrange := (sideOKC_parts hside).2.2.2.2.2
  intro n
  induction n with
  | zero =>
    intro hasm
    cases hall : sp.conjs.all (propBit (outM D s0 ins 0))
    case true => rfl
    case false =>
    exfalso
    have hbB := (unrollO_extends 1 _ _ _ _ _ hrunB).1.1
    let xs := insVals ins 0 1
    have hxl : xs.length = stB.base := by rw [← hbB, insVals_length hins]; simp
    obtain ⟨hc, hag⟩ := extendSt_agree hxl hscB
    have facts := unrollO_sound hrange hres hA hins hc 1 0 0 _ _ _ _ hrunB (.refl _)
      (fun j hj => by simpa using insVals_agree hins 1 hag j hj) .conc 0 (by simp)
      (fun i _ => by simpa using rt i)
    simp only [List.getElem_cons_zero, Nat.zero_add] at facts
    unfold baseOblOf at hφB
    simp only [Option.map_eq_some_iff] at hφB
    obtain ⟨ps, hps, rfl⟩ := hφB
    have hev := eval_coi hc (body := .gate .and sB.1 (.not (conjE ps))) (by
      rw [eval_and, eval_not, facts.1, eval_conjE_propsOf facts.2.1 _ _ hps, hall]
      simpa using hasm 0 (Nat.le_refl 0))
    rw [hunsB] at hev; cases hev
  | succ n ih =>
    intro hasm
    have hprev := ih (fun j hj => hasm j (by omega))
    rw [List.all_eq_true]
    intro p hp
    obtain ⟨t, ht, rfl⟩ := List.getElem_of_mem hp
    cases hgt : propBit (outM D s0 ins (n + 1)) sp.conjs[t]
    case true => rfl
    case false =>
    exfalso
    obtain ⟨φ, hφ, huns⟩ := hstep t ht
    have hbS := (unrollO_extends 2 _ _ _ _ _ hrunS).1.1
    let xs := stateVals (traceM D s0 ins n) ++ insVals ins n 2
    have hxl : xs.length = stS.base := by
      rw [← hbS, List.length_append, stateVals_length (hinv n).1, insVals_length hins]
    obtain ⟨hc, hag⟩ := extendSt_agree hxl hscS
    rw [AgreeAt_append, stateVals_length (hinv n).1, Nat.zero_add] at hag
    have hS := stateVar_rel (hinv n).1 hag.1
    have facts := unrollO_sound hrange hres hA hins hc 2 n _ _ _ _ _ hrunS (.refl _)
      (insVals_agree hins 2 hag.2) hS
    have f0 := facts 0 (by simp) (fun i hi => by
      have : i = 0 := by omega
      subst this; simpa using rt n)
    have f1 := facts 1 (by simp) (fun i hi => by
      rcases Nat.lt_or_ge i 1 with h | h
      · have : i = 0 := by omega
        subst this; simpa using rt n
      · have : i = 1 := by omega
        subst this; simpa using rt (n + 1))
    simp only [List.getElem_cons_zero, List.getElem_cons_succ, Nat.add_zero] at f0 f1
    unfold stepOblOf at hφ
    simp only [Option.bind_eq_bind, Option.bind_eq_some_iff, Option.pure_def, Option.some.injEq] at hφ
    obtain ⟨hp', hsel, hs, hhs, p', hp'', g, hg, rfl⟩ := hφ
    have hp'eq : p' = sp.conjs[t] := by
      rw [List.getElem?_eq_getElem ht] at hp''; exact (Option.some.inj hp'').symm
    subst hp'eq
    -- every hypothesis is a conjunct, and every conjunct held at cycle n
    have hyps_true : hp'.all (propBit (outM D s0 ins n)) = true := by
      rw [List.all_eq_true]
      intro q hq
      unfold ConjSpec.select at hsel
      exact (List.all_eq_true.mp hprev) q (mem_of_mapM_getElem? hsel q hq)
    have hev := eval_coi hc (body := conjE [a.1, conjE hs, b.1, .not g]) (by
      rw [eval_conjE]
      simp only [List.all_cons, List.all_nil, Bool.and_true, eval_not]
      rw [f0.1, f1.1, eval_conjE_propsOf f0.2.1 _ _ hhs, hyps_true, propOf_sound f1.2.1 hg, hgt]
      have h0 := hasm n (by omega)
      have h1 := hasm (n + 1) (Nat.le_refl _)
      simp [h0, h1])
    rw [huns] at hev; cases hev

end

/-! ## Against the reference semantics -/

/-- **The conjunctive invariant, in the shared reference semantics**, for a
residual `R` from the specializer run: at every cycle whose assumptions up to
it all held, every conjunct of `interpretDesign`'s outputs holds. -/
theorem runConjOf_sound_ref {D : DesignCert} {R : Program} {sp : ConjSpec}
    {s0 : StateM} {pfB : ByteArray} {pfS : List ByteArray} (hres : MemResidual D R)
    (hA : admitsWB D = true) (hr : runConjOf D R sp s0 pfB pfS = true)
    (ins : Nat → RuntimeInput) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, (∀ j, j ≤ n → asmBit (refOut D (StateM.denote s0) ins j) (ins j) sp.asmOut sp.asmIn = true) →
      sp.conjs.all (propBit (refOut D (StateM.denote s0) ins n)) = true := by
  have hside := (conj_parts hr).1
  have hinv := invC_all hres hA hr hins
  have hR : ∀ n, runtimeFullOKB D (ins n) (traceM D s0 ins n) (edgesAll D) = true :=
    fun n => runtime_of_invC hside (hinv n).1 (hinv n).2 (hins n)
  have hden := traceM_denote hA hR
  intro n hasm
  rw [← (hden n).2]
  exact runConjOf_sound hres hA hr hins n (fun j hj => by rw [(hden j).2]; exact hasm j hj)

end Projection.Sym
