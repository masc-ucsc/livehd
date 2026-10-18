/-
`Sym.KInd` -- k-induction on the projection's residual, checked (M3 S3).

`runInvKOf D R spec s0 pfB pfS` is a closed Boolean. It builds two
obligations from the residual `R` of design `D`:

  * BASE: from the initial state `s0`, `k` symbolic steps; refute "some step
    `j < k` meets every assumption up to `j` and fails the property at `j`";
  * STEP: from an ARBITRARY state of layout `L` (variables), `k+1` steps;
    refute "every assumption holds, the property holds at the first `k`
    steps, and fails at the last".

Each is checked UNSAT by `checkUnsat` against an LRAT proof (`pfB`, `pfS`)
from an untrusted solver. Side checks: the definitions are scoped, `s0` has
layout `L`, one symbolic step from layout `L` lands in layout `L` again (so
the layout is an invariant), and the runtime-shape checks the memory route
needs.

`runInvKOf_sound`: if it answers `true`, then on EVERY run from `s0` with
inputs of the declared widths, the property holds at every cycle whose
assumptions up to it all held. Proved once; run per design.
-/
import LeanSemanticPrimitives.Projection.Sym.Oblig
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection.Sym
open Std.Tactic.BVDecide Projection.MemProjection Projection.Hw Projection.HwMem Compiler

/-! ## Shapes, executable -/

def imgMatchesB (m : MemImg) (p : Nat × Nat) : Bool :=
  m.words.length == 2 ^ p.1 && m.words.all (fun x => x.width == p.2) && m.dflt.width == p.2

def stMatchesB (L : Shape) (st : StateM) : Bool :=
  st.flops.toList.map BV.width == L.fws && st.mems.size == L.mds.length
    && (st.mems.toList.zip L.mds).all (fun q => imgMatchesB q.1 q.2)

theorem stMatchesB_sound {L : Shape} {st : StateM} (h : stMatchesB L st = true) : StMatches L st := by
  unfold stMatchesB at h
  simp only [Bool.and_eq_true, beq_iff_eq, List.all_eq_true] at h
  obtain ⟨⟨hf, hl⟩, hall⟩ := h
  refine ⟨hf, hl, fun j m aw dw h1 h2 => ?_⟩
  have hj1 : st.mems.toList[j]? = some m := by simpa using h1
  have hmem : (m, (aw, dw)) ∈ st.mems.toList.zip L.mds := by
    rw [List.mem_iff_getElem?]
    exact ⟨j, by rw [List.getElem?_zip_eq_some]; exact ⟨hj1, h2⟩⟩
  have := hall _ hmem
  simp only [imgMatchesB, Bool.and_eq_true, beq_iff_eq, List.all_eq_true] at this
  exact ⟨this.1.1, this.1.2, this.2⟩

/-- The number of primary variables a state of layout `L` takes. -/
def layoutSize (L : Shape) : Nat := L.fws.length + (L.mds.map fun p => 2 ^ p.1 + 1).sum

theorem stateVals_length {L : Shape} {st : StateM} (hm : StMatches L st) :
    (stateVals st).length = layoutSize L := by
  obtain ⟨hf, hl, hs⟩ := hm
  unfold stateVals layoutSize
  rw [List.length_append, List.length_map, ← hf, List.length_map]
  congr 1
  have key : ∀ (ms : List MemImg) (mds : List (Nat × Nat)), ms.length = mds.length →
      (∀ (j : Nat) (m : MemImg) (aw dw : Nat), ms[j]? = some m → mds[j]? = some (aw, dw) →
        m.words.length = 2 ^ aw) →
      (ms.flatMap imgVals).length = (mds.map fun p => 2 ^ p.1 + 1).sum := by
    intro ms
    induction ms with
    | nil => intro mds hl _; cases mds <;> simp_all
    | cons m ms ih =>
        intro mds hl hs
        cases mds with
        | nil => simp at hl
        | cons p mds =>
            rw [List.flatMap_cons, List.length_append, imgVals_length, List.map_cons, List.sum_cons,
              ih mds (by simpa using hl) (fun j m' aw dw h1 h2 => hs (j + 1) m' aw dw (by simpa using h1)
                (by simpa using h2)),
              hs 0 m p.1 p.2 rfl rfl]
  exact key st.mems.toList L.mds (by simpa using hl)
    (fun j m aw dw h1 h2 => (hs j m aw dw (by simpa using h1) h2).1)

/-! ## Input segments -/

def insVals (ins : Nat → RuntimeInput) (m : Nat) : Nat → List PBV
  | 0     => []
  | c + 1 => insVals ins m c ++ inputVals (ins (m + c))

theorem inputVals_length {i : RuntimeInput} {inW : List Nat} (h : i.toList.map BV.width = inW) :
    (inputVals i).length = inW.length := by
  rw [← h]; simp [inputVals]

theorem insVals_length {ins : Nat → RuntimeInput} {inW : List Nat}
    (hins : ∀ n, (ins n).toList.map BV.width = inW) (m : Nat) :
    ∀ c, (insVals ins m c).length = c * inW.length
  | 0 => by simp [insVals]
  | c + 1 => by
      rw [insVals, List.length_append, insVals_length hins m c, inputVals_length (hins _), Nat.succ_mul]

theorem insVals_agree {σ : Asg} {ins : Nat → RuntimeInput} {inW : List Nat}
    (hins : ∀ n, (ins n).toList.map BV.width = inW) {m start : Nat} :
    ∀ c, AgreeAt σ start (insVals ins m c) →
      ∀ j, j < c → AgreeAt σ (start + j * inW.length) (inputVals (ins (m + j)))
  | 0, _, _, hj => by simp at hj
  | c + 1, h, j, hj => by
      rw [insVals, AgreeAt_append] at h
      rcases Nat.lt_or_ge j c with hl | hl
      · exact insVals_agree hins c h.1 j hl
      · have : j = c := by omega
        subst this
        rw [insVals_length hins] at h
        exact h.2

/-! ## The assignment a concrete window gets -/

theorem extendSt_agree {xs : List PBV} {st : SymSt} (hb : xs.length = st.base) (hs : scopedB st = true) :
    st.Consistent (extendSt xs.toArray st) ∧ AgreeAt (extendSt xs.toArray st) 0 xs := by
  obtain ⟨hc, hg⟩ := extendSt_consistent (xs := xs.toArray) (st := st) (by simpa using hb) hs
  exact ⟨hc, fun j x hj => by rw [Nat.zero_add]; exact hg j x (by simpa using hj)⟩

/-! ## The checker -/

structure InvSpec where
  /-- the property: an output, nonzero or zero -/
  prop   : PropSpec
  /-- an assumption on an output, if any -/
  asmOut : Option PropSpec
  /-- assumptions on inputs: input `k` has value `c` at every cycle -/
  asmIn  : List (Nat × Int)
  /-- the induction depth, `≥ 1` -/
  k      : Nat
  /-- symbolic-evaluation fuel (the residual checker's bound suffices) -/
  fuel   : Nat
  /-- the input widths, by ordinal -/
  inW    : List Nat
  /-- the state layout -/
  L      : Shape

def baseRun (D : DesignCert) (R : Program) (sp : InvSpec) (s0 : StateM) :
    Except String (List StepS × SymSt) :=
  unroll D R sp.fuel sp.prop sp.asmOut sp.asmIn sp.inW sp.k 0 (.conc (StateM.encStateM s0))
    ⟨sp.k * sp.inW.length, #[]⟩

def stepRun (D : DesignCert) (R : Program) (sp : InvSpec) : Except String (List StepS × SymSt) :=
  unroll D R sp.fuel sp.prop sp.asmOut sp.asmIn sp.inW (sp.k + 1) (layoutSize sp.L) (stateVar sp.L 0)
    ⟨layoutSize sp.L + (sp.k + 1) * sp.inW.length, #[]⟩

/-- The base obligation, when the run succeeds and its definitions are scoped. -/
def baseObl (D : DesignCert) (R : Program) (sp : InvSpec) (s0 : StateM) : Option BVLogicalExpr :=
  match baseRun D R sp s0 with
  | .ok (steps, st) => if scopedB st then some (.gate .and (defsE st) (baseBad steps)) else none
  | .error _ => none

/-- The step obligation, when the run succeeds, its definitions are scoped and
its first step lands in the layout again. -/
def stepObl (D : DesignCert) (R : Program) (sp : InvSpec) : Option BVLogicalExpr :=
  match stepRun D R sp with
  | .ok (s :: steps, st) =>
      if scopedB st && decide (stateShapeOf s.2.2 = some sp.L) then
        some (.gate .and (defsE st) (conjE (stepConj (s :: steps))))
      else none
  | _ => none

def sideOK (D : DesignCert) (sp : InvSpec) (s0 : StateM) : Bool :=
  decide (0 < sp.k) && stMatchesB sp.L s0 && stateFullOKB D s0 && inputOKB D sp.inW.length
    && flopBoundOKB D sp.L.fws.length && edgesOKB D (edgesAll D) && inAsmRangeOK sp.inW sp.asmIn

def runInvKOf (D : DesignCert) (R : Program) (sp : InvSpec) (s0 : StateM) (pfB pfS : ByteArray) : Bool :=
  sideOK D sp s0
    && (match baseObl D R sp s0 with | some φ => checkUnsat φ pfB | none => false)
    && (match stepObl D R sp with | some φ => checkUnsat φ pfS | none => false)

/-! ## Soundness -/

section
variable {sf wf : Nat} {D : DesignCert} {R : Program} {sp : InvSpec} {s0 : StateM}
  {pfB pfS : ByteArray} {ins : Nat → RuntimeInput}

theorem sideOK_parts (h : sideOK D sp s0 = true) :
    0 < sp.k ∧ stMatchesB sp.L s0 = true ∧ stateFullOKB D s0 = true ∧ inputOKB D sp.inW.length = true
    ∧ flopBoundOKB D sp.L.fws.length = true ∧ edgesOKB D (edgesAll D) = true
    ∧ inAsmRangeOK sp.inW sp.asmIn = true := by
  unfold sideOK at h
  simp only [Bool.and_eq_true, decide_eq_true_eq] at h
  exact ⟨h.1.1.1.1.1.1, h.1.1.1.1.1.2, h.1.1.1.1.2, h.1.1.1.2, h.1.1.2, h.1.2, h.2⟩

theorem runtime_of_inv (hside : sideOK D sp s0 = true) {st : StateM} {i : RuntimeInput}
    (hm : StMatches sp.L st) (hf : stateFullOKB D st = true) (hi : i.toList.map BV.width = sp.inW) :
    runtimeFullOKB D i st (edgesAll D) = true := by
  obtain ⟨_, _, _, hin, hfb, hed, _⟩ := sideOK_parts hside
  unfold stateFullOKB at hf
  simp only [Bool.and_eq_true] at hf
  have hsz : i.size = sp.inW.length := by rw [← hi]; simp
  have hfs : st.flops.size = sp.L.fws.length := by rw [← hm.1]; simp
  unfold runtimeFullOKB runtimeOKB
  simp only [Bool.and_eq_true]
  exact ⟨⟨⟨⟨hf.1, by rw [hsz]; exact hin⟩, by rw [hfs]; exact hfb⟩, hed⟩, hf.2⟩

theorem obl_parts (hchk : runInvKOf D R sp s0 pfB pfS = true) :
    sideOK D sp s0 = true
    ∧ (∃ steps st, baseRun D R sp s0 = .ok (steps, st) ∧ scopedB st = true
        ∧ BVLogicalExpr.Unsat (BoolExpr.gate .and (defsE st) (baseBad steps)))
    ∧ (∃ s steps st, stepRun D R sp = .ok (s :: steps, st) ∧ scopedB st = true
        ∧ stateShapeOf s.2.2 = some sp.L
        ∧ BVLogicalExpr.Unsat (BoolExpr.gate .and (defsE st) (conjE (stepConj (s :: steps))))) := by
  unfold runInvKOf at hchk
  simp only [Bool.and_eq_true] at hchk
  obtain ⟨⟨hside, hb⟩, hs⟩ := hchk
  refine ⟨hside, ?_, ?_⟩
  · cases hob : baseObl D R sp s0 with
    | none => rw [hob] at hb; cases hb
    | some φ =>
      rw [hob] at hb
      unfold baseObl at hob
      split at hob
      · rename_i steps st hrun
        split at hob
        · rename_i hsc
          simp only [Option.some.injEq] at hob; subst hob
          exact ⟨steps, st, hrun, hsc, checkUnsat_sound hb⟩
        · cases hob
      · cases hob
  · cases hos : stepObl D R sp with
    | none => rw [hos] at hs; cases hs
    | some φ =>
      rw [hos] at hs
      unfold stepObl at hos
      split at hos
      · rename_i s steps st hrun
        split at hos
        · rename_i hc
          simp only [Option.some.injEq] at hos; subst hos
          simp only [Bool.and_eq_true, decide_eq_true_eq] at hc
          exact ⟨s, steps, st, hrun, hc.1, hc.2, checkUnsat_sound hs⟩
        · cases hos
      · cases hos

theorem run_base {steps : List StepS} {st st0 : SymSt} {k inB : Nat} {sS : SVal}
    (h : unroll D R sp.fuel sp.prop sp.asmOut sp.asmIn sp.inW k inB sS st0 = .ok (steps, st)) :
    st.base = st0.base ∧ steps.length = k := by
  have ⟨x, l⟩ := unroll_extends k inB sS st0 steps st h
  exact ⟨x.1.symm, l⟩

/-- The layout and the memory route's runtime shape hold at EVERY cycle. -/
theorem inv_all (hproj : projectMemDesignAt sf wf D = .ok R) (hA : admitsB D = true)
    (hchk : runInvKOf D R sp s0 pfB pfS = true) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, StMatches sp.L (traceM D s0 ins n) ∧ stateFullOKB D (traceM D s0 ins n) = true := by
  obtain ⟨hside, -, ⟨s, steps, stS, hrun, hsc, hshape, -⟩⟩ := obl_parts hchk
  intro n
  induction n with
  | zero =>
      obtain ⟨_, hm, hf, _⟩ := sideOK_parts hside
      exact ⟨stMatchesB_sound hm, hf⟩
  | succ n ih =>
      have rt := runtime_of_inv hside ih.1 ih.2 (hins n)
      refine ⟨?_, (resM_spec hA rt).2.2⟩
      -- one symbolic step from the layout lands in the layout
      have hb := (run_base hrun).1
      let xs := stateVals (traceM D s0 ins n) ++ insVals ins n (sp.k + 1)
      have hxl : xs.length = stS.base := by
        rw [hb, List.length_append, stateVals_length ih.1, insVals_length hins]
      obtain ⟨hc, hag⟩ := extendSt_agree hxl hsc
      rw [AgreeAt_append, stateVals_length ih.1, Nat.zero_add] at hag
      have hS := stateVar_rel ih.1 hag.1
      have facts := unroll_sound (sideOK_parts hside).2.2.2.2.2.2 hproj hA hins hc (sp.k + 1) n _ _ _ _ _
        hrun (.refl _)
        (insVals_agree hins (sp.k + 1) hag.2) hS 0 (by simp)
        (fun i hi => by
          have : i = 0 := by omega
          subst this; simpa using rt)
      exact stateShapeOf_sound facts.2.2 hshape

/-- **k-induction is sound.** -/
theorem runInvKOf_sound (hproj : projectMemDesignAt sf wf D = .ok R) (hA : admitsB D = true)
    (hchk : runInvKOf D R sp s0 pfB pfS = true) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, (∀ j, j ≤ n → asmBit (outM D s0 ins j) (ins j) sp.asmOut sp.asmIn = true) →
      propBit (outM D s0 ins n) sp.prop = true := by
  obtain ⟨hside, ⟨stepsB, stB, hrunB, hscB, hunsB⟩, ⟨s, steps, stS, hrunS, hscS, -, hunsS⟩⟩ :=
    obl_parts hchk
  have hinv := inv_all hproj hA hchk hins
  have rt : ∀ n, runtimeFullOKB D (ins n) (traceM D s0 ins n) (edgesAll D) = true :=
    fun n => runtime_of_inv hside (hinv n).1 (hinv n).2 (hins n)
  have hk : 0 < sp.k := by
    exact (sideOK_parts hside).1
  intro n
  induction n using Nat.strongRecOn with
  | ind n ih =>
  intro hasm
  cases hokf : propBit (outM D s0 ins n) sp.prop
  case true => rfl
  case false =>
  exfalso
  rcases Nat.lt_or_ge n sp.k with hn | hn
  · -- the base window, from cycle 0
    obtain ⟨hbB, hlB⟩ := run_base hrunB
    let xs := insVals ins 0 sp.k
    have hxl : xs.length = stB.base := by rw [hbB, insVals_length hins]
    obtain ⟨hc, hag⟩ := extendSt_agree hxl hscB
    have facts := unroll_sound (sideOK_parts hside).2.2.2.2.2.2 hproj hA hins hc sp.k 0 0 _ _ _ _ hrunB
      (.refl _)
      (fun j hj => by simpa using insVals_agree hins sp.k hag j hj) .conc
    have hev : BVLogicalExpr.eval (extendSt xs.toArray stB) (BoolExpr.gate .and (defsE stB) (baseBad stepsB)) = true := by
      rw [eval_and, eval_defsE hc, Bool.true_and]
      refine baseBad_true [] stepsB n (by omega) rfl (fun i hi hin => ?_) ?_
      · have f := (facts i hi (fun i' _ => by simpa using rt i')).2.1
        rw [f]; simpa using hasm i hin
      · have f := (facts n (by omega) (fun i' _ => by simpa using rt i')).1
        rw [f]; simpa using hokf
    rw [hunsB] at hev; cases hev
  · -- the step window, from cycle n - k
    obtain ⟨hbS, hlS⟩ := run_base hrunS
    let m := n - sp.k
    have hmn : m + sp.k = n := by omega
    let xs := stateVals (traceM D s0 ins m) ++ insVals ins m (sp.k + 1)
    have hxl : xs.length = stS.base := by
      rw [hbS, List.length_append, stateVals_length (hinv m).1, insVals_length hins]
    obtain ⟨hc, hag⟩ := extendSt_agree hxl hscS
    rw [AgreeAt_append, stateVals_length (hinv m).1, Nat.zero_add] at hag
    have hS := stateVar_rel (hinv m).1 hag.1
    have facts := unroll_sound (sideOK_parts hside).2.2.2.2.2.2 hproj hA hins hc (sp.k + 1) m _ _ _ _ _
      hrunS (.refl _)
      (insVals_agree hins (sp.k + 1) hag.2) hS
    have hev : BVLogicalExpr.eval (extendSt xs.toArray stS)
        (BoolExpr.gate .and (defsE stS) (conjE (stepConj (s :: steps)))) = true := by
      rw [eval_and, eval_defsE hc, Bool.true_and]
      have hlen : (s :: steps).length = sp.k + 1 := hlS
      refine stepConj_true (s :: steps) (by simp) (fun i hi => ?_) (fun i hi hi' => ?_) ?_
      · have f := (facts i hi (fun i' _ => rt (m + i'))).2.1
        rw [f]; exact hasm (m + i) (by omega)
      · have f := (facts i hi (fun i' _ => rt (m + i'))).1
        rw [f]; exact ih (m + i) (by omega) (fun j hj => hasm j (by omega))
      · have f := (facts ((s :: steps).length - 1) (by simp) (fun i' _ => rt (m + i'))).1
        refine f.trans ?_
        have : m + ((s :: steps).length - 1) = n := by omega
        rw [this]; exact hokf
    rw [hunsS] at hev; cases hev

end

/-! ## The closed checker, against the reference semantics -/

/-- Specialize the memory interpreter to `D`, then check. -/
def runInvK (sf wf : Nat) (D : DesignCert) (sp : InvSpec) (s0 : StateM) (pfB pfS : ByteArray) : Bool :=
  admitsB D &&
    match projectMemDesignAt sf wf D with
    | .ok R    => runInvKOf D R sp s0 pfB pfS
    | .error _ => false

/-- **The unbounded invariant, in the shared reference semantics**, for a
residual `R` obtained from the specializer run. If the checker accepts, then for
EVERY input sequence of the declared widths, at every cycle `n` whose
assumptions up to `n` all held, the property output of `interpretDesign`
(iterated from `denote s0`, every clock firing) is nonzero. -/
theorem runInvKOf_sound_ref {sf wf : Nat} {D : DesignCert} {R : Program} {sp : InvSpec}
    {s0 : StateM} {pfB pfS : ByteArray} (hproj : projectMemDesignAt sf wf D = .ok R)
    (hA : admitsB D = true) (hr : runInvKOf D R sp s0 pfB pfS = true)
    (ins : Nat → RuntimeInput) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, (∀ j, j ≤ n → asmBit (refOut D (StateM.denote s0) ins j) (ins j) sp.asmOut sp.asmIn = true) →
      propBit (refOut D (StateM.denote s0) ins n) sp.prop = true := by
  have hside := (obl_parts hr).1
  have hinv := inv_all hproj hA hr hins
  have hR : ∀ n, runtimeFullOKB D (ins n) (traceM D s0 ins n) (edgesAll D) = true :=
    fun n => runtime_of_inv hside (hinv n).1 (hinv n).2 (hins n)
  have hden := traceM_denote hA hR
  intro n hasm
  rw [← (hden n).2]
  exact runInvKOf_sound hproj hA hr hins n (fun j hj => by rw [(hden j).2]; exact hasm j hj)

/-- The same, closed: the check specializes `D` itself. -/
theorem runInvK_sound {sf wf : Nat} {D : DesignCert} {sp : InvSpec} {s0 : StateM}
    {pfB pfS : ByteArray} (h : runInvK sf wf D sp s0 pfB pfS = true)
    (ins : Nat → RuntimeInput) (hins : ∀ n, (ins n).toList.map BV.width = sp.inW) :
    ∀ n, (∀ j, j ≤ n → asmBit (refOut D (StateM.denote s0) ins j) (ins j) sp.asmOut sp.asmIn = true) →
      propBit (refOut D (StateM.denote s0) ins n) sp.prop = true := by
  unfold runInvK at h
  simp only [Bool.and_eq_true] at h
  obtain ⟨hA, hr⟩ := h
  split at hr
  · rename_i R hproj
    exact runInvKOf_sound_ref hproj hA hr ins hins
  · cases hr

#audit_axioms extendSt_consistent
#audit_axioms checkUnsat_sound
#audit_axioms symStep_sound
#audit_axioms resM_residual
#audit_axioms traceM_denote
#audit_axioms stateShapeOf_sound
#audit_axioms unroll_sound
#audit_axioms eval_defsE
#audit_axioms inv_all
#audit_axioms runInvKOf_sound
#audit_axioms runInvKOf_sound_ref
#audit_axioms runInvK_sound

end Projection.Sym
