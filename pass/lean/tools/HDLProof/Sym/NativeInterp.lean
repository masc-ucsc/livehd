import LeanSemanticPrimitives.Projection.HardwareMemMain
import LeanSemanticPrimitives.Projection.HardwareMemWriteAdmit
import HDLProof.Sym.Trace

/-! The interpreter's result, made explicit. `resM` is `Classical.choose` of the memory
interpreter's evaluation; `resM_explicit` shows it is `resOf` of the interpreter's own slot
list, which comes with every node's step (`NodesOK`). Used by `NativeRoute` to tie the IR and
VC symbolic steps to `resM` exactly (memory word lists are not determined by their denotation).
Copies of `evalNodesM_agree_gen`, `sourcesThenNodes_gen` and `mainM_adequate_gen` that keep
those facts; `SEval_det` for the named syntax. Mathlib-free. -/
namespace Projection
open Surface

mutual

/-- The named-syntax semantics is deterministic, as `Eval_det` is for the resolved one. -/
theorem SEval_det {P : SProgram} :
    ∀ {σ : SEnv} {e : SExp} {v₁ v₂ : Val}, SEval P σ e v₁ → SEval P σ e v₂ → v₁ = v₂
  | _, _, _, _, .lit, h2 => by cases h2; rfl
  | _, _, _, _, .ref h1, h2 => by
      cases h2 with
      | ref h2' => rw [h1] at h2'; exact Option.some.inj h2'
  | _, _, _, _, .letN he hb, h2 => by
      cases h2 with
      | letN he' hb' =>
          cases SEval_det he he'
          exact SEval_det hb hb'
  | _, _, _, _, .iteT hc ha, h2 => by
      cases h2 with
      | iteT hc' ha' => exact SEval_det ha ha'
      | iteF hc' hb' => exact absurd (SEval_det hc hc') (by simp)
  | _, _, _, _, .iteF hc hb, h2 => by
      cases h2 with
      | iteT hc' ha' => exact absurd (SEval_det hc hc') (by simp)
      | iteF hc' hb' => exact SEval_det hb hb'
  | _, _, _, _, .prim hs hp, h2 => by
      cases h2 with
      | prim hs' hp' =>
          cases SEvalList_det hs hs'
          rw [hp] at hp'
          exact Except.ok.inj hp'
  | _, _, _, _, .mk hs, h2 => by
      cases h2 with
      | mk hs' => cases SEvalList_det hs hs'; rfl
  | _, _, _, _, .switch hs hf hl hb, h2 => by
      cases h2 with
      | switch hs' hf' hl' hb' =>
          have hc := SEval_det hs hs'
          cases hc
          rw [hf] at hf'
          cases Option.some.inj hf'
          exact SEval_det hb hb'
  | _, _, _, _, .call hs hfn hl hb, h2 => by
      cases h2 with
      | call hs' hfn' hl' hb' =>
          cases SEvalList_det hs hs'
          rw [hfn] at hfn'
          cases Option.some.inj hfn'
          exact SEval_det hb hb'

theorem SEvalList_det {P : SProgram} :
    ∀ {σ : SEnv} {es : List SExp} {vs₁ vs₂ : List Val},
      SEvalList P σ es vs₁ → SEvalList P σ es vs₂ → vs₁ = vs₂
  | _, _, _, _, .nil, h2 => by cases h2; rfl
  | _, _, _, _, .cons h hs, h2 => by
      cases h2 with
      | cons h' hs' =>
          cases SEval_det h h'
          cases SEvalList_det hs hs'
          rfl

end


end Projection

namespace Projection
namespace HwMem
open Surface Compiler Projection.Hw Projection.HwLocal

/-- Every node from `j` on stepped by `NodeStepOK`, read off the final slot list. -/
def NodesOK (D : DesignCert) (i : RuntimeInput) (st : StateM) (G : Nat → Option Nat)
    (j : Nat) (ss : List SlotV) : Prop :=
  ∀ (j' : Nat) (c : DenseNodeCert), j ≤ j' → D.nodes[j']? = some c →
    ∃ h : D.sources.size + j' < ss.length,
      NodeStepOK D i st G j' c (ss.take (D.sources.size + j')) (ss[D.sources.size + j'])

/-- `evalNodesM_agree_gen`, keeping the per-node facts and the prefix. -/
theorem evalNodesM_agree_keep {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hstep : ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c → j < D.nodes.size →
               c ∈ D.nodes.toList →
               ∀ ss : List SlotV, PrefixRelM D i st G (D.sources.size + j) ss →
                 ∃ v : SlotV, NodeStepOK D i st G j c ss v) :
    ∀ (r j : Nat), j + r = D.nodes.size →
      ∀ (ss : List SlotV), PrefixRelM D i st G (D.sources.size + j) ss →
        ∃ ss' : List SlotV,
          PrefixRelM D i st G (D.sources.size + D.nodes.size) ss'
          ∧ ss'.take (D.sources.size + j) = ss
          ∧ NodesOK D i st G j ss'
          ∧ ∀ (σ : SEnv) (eno eenv en : SExp),
              SEval hwMemS σ eno (encListG encNode (D.nodes.toList.drop j)) →
              SEval hwMemS σ eenv (objEnv (ss.map SlotV.enc)) →
              SEval hwMemS σ en (.int (Int.ofNat (D.sources.size + j))) →
              SEval hwMemS σ (.call "evalNodesM" [eno, eenv, en])
                (objEnv (ss'.map SlotV.enc))
  | 0,     j, hr, ss, hpre => by
      have hj : j = D.nodes.size := by omega
      subst hj
      refine ⟨ss, hpre, ?_, ?_, ?_⟩
      · rw [← hpre.len]; exact List.take_length
      · intro j' c hj' hc
        have := (Array.getElem?_eq_some_iff.mp hc).1
        omega
      · intro σ eno eenv en hno henv hlen
        refine SEval_call3 hno henv hlen rfl rfl ?_
        refine SEval.iteT (SEval_prim1 (.ref rfl) ?_) (.ref rfl)
        have hnil : D.nodes.toList.drop D.nodes.size = [] := by simp
        rw [hnil]
        rfl
  | r + 1, j, hr, ss, hpre => by
      have hj : j < D.nodes.size := by omega
      have hjl : j < D.nodes.toList.length := by simpa using hj
      have hdrop : D.nodes.toList.drop j
          = D.nodes.toList[j] :: D.nodes.toList.drop (j + 1) :=
        List.drop_eq_getElem_cons hjl
      have hcj : D.nodes[j]? = some D.nodes.toList[j] := by
        rw [Array.getElem?_eq_getElem hj]; simp
      have hmemc : D.nodes.toList[j] ∈ D.nodes.toList := List.getElem_mem hjl
      obtain ⟨v, hvden, hvkind, hvgeom, hv⟩ := hstep j _ hcj hj hmemc ss hpre
      have hpre' : PrefixRelM D i st G (D.sources.size + (j + 1)) (ss ++ [v]) := by
        have hx := prefixRelM_snoc hpre hvden hvkind hvgeom
        simpa [Nat.add_assoc] using hx
      obtain ⟨ss', hss', htake, hok, hrec⟩ :=
        evalNodesM_agree_keep hstep r (j + 1) (by omega) (ss ++ [v]) hpre'
      have hlen0 := hpre.len
      have htake0 : ss'.take (D.sources.size + j) = ss := by
        have := congrArg (List.take (D.sources.size + j)) htake
        rw [List.take_take, Nat.min_eq_left (by omega)] at this
        rw [this, List.take_append_of_le_length (by omega), ← hlen0, List.take_length]
      refine ⟨ss', hss', htake0, ?_, ?_⟩
      · intro j' c hj' hc
        rcases Nat.lt_or_ge j j' with hlt | hge
        · exact hok j' c hlt hc
        · have hjj : j' = j := by omega
          subst hjj
          have hlt' : D.sources.size + j' < ss'.length := by rw [hss'.len]; omega
          refine ⟨hlt', ?_⟩
          have hcc : c = D.nodes.toList[j'] := by
            rw [hcj] at hc; exact (Option.some.inj hc).symm
          subst hcc
          have hget : ss'[D.sources.size + j'] = v := by
            have h1 : (ss'.take (D.sources.size + (j' + 1)))[D.sources.size + j']'(by
                simp; omega) = ss'[D.sources.size + j'] := List.getElem_take
            rw [← h1]
            simp only [htake]
            simp [hlen0]
          rw [htake0, hget]
          exact ⟨hvden, hvkind, hvgeom, hv⟩
      · intro σ eno eenv en hno henv hlen
        rw [hdrop] at hno
        refine SEval_call3 hno henv hlen rfl rfl ?_
        refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
        refine hrec _ _ _ _ (SEval_tl (.ref rfl)) ?_
          (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim] <;> omega))
        rw [objEnv_snoc]
        exact SEval_consP
          (hv _ _ _ _ (SEval_hd (.ref rfl)) (.ref rfl)
            (by rw [hpre.len]; exact .ref rfl)) (.ref rfl)


theorem sourcesThenNodes_keep {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat}
    (hsem : Direct.DesignSemWF D)
    (hstate : StateShapeOK D st)
    (hGsrc : ∀ k, k < D.sources.size → G k = srcGeomOf D k)
    (hstep : ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c → j < D.nodes.size →
               c ∈ D.nodes.toList →
               ∀ ss : List SlotV, PrefixRelM D i st G (D.sources.size + j) ss →
                 ∃ v : SlotV, NodeStepOK D i st G j c ss v) :
    ∃ ss' : List SlotV,
      PrefixRelM D i st G (D.sources.size + D.nodes.size) ss'
      ∧ ss'.take D.sources.size = D.sources.toList.map (srcSlotV i st)
      ∧ NodesOK D i st G 0 ss'
      ∧ ∀ (σ : SEnv) (eno eenv en : SExp),
          SEval hwMemS σ eno (encListG encNode D.nodes.toList) →
          SEval hwMemS σ eenv
            (objEnv ((D.sources.toList.map (srcSlotV i st)).map SlotV.enc)) →
          SEval hwMemS σ en (.int (Int.ofNat D.sources.size)) →
          SEval hwMemS σ (.call "evalNodesM" [eno, eenv, en])
            (objEnv (ss'.map SlotV.enc)) := by
  obtain ⟨ss', hss', htake, hok, hrec⟩ :=
    evalNodesM_agree_keep hstep D.nodes.size 0 (by omega)
      (D.sources.toList.map (srcSlotV i st))
      (by simpa using prefixRelM_sources (G := G) hsem hstate hGsrc)
  refine ⟨ss', hss', by simpa using htake, hok, ?_⟩
  intro σ eno eenv en hno henv hlen
  exact hrec σ eno eenv en (by simpa using hno) henv (by simpa using hlen)

/-- The interpreter's result, as a function of its slot list. -/
def resOf (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (st : StateM)
    (ss : List SlotV) : StateM.ResultM :=
  { nextState :=
      { flops := (flopNextsFrom (slotValM D i st) e (StateM.denote st) 0 D.flops.toList).toArray
      , mems  := (memNextsOf e st ss 0 D.memories.toList).toArray }
  , outputs :=
      (D.outputs.toList.map (fun o => bv_resize o.width ((slotValM D i st) o.slot).asBV)).toArray }

theorem mainM_explicit {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hstate : StateShapeOK D st)
    (hGsrc : ∀ k, k < D.sources.size → G k = srcGeomOf D k)
    (hstep : ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c → j < D.nodes.size →
               c ∈ D.nodes.toList →
               ∀ ss : List SlotV, PrefixRelM D i st G (D.sources.size + j) ss →
                 ∃ v : SlotV, NodeStepOK D i st G j c ss v)
    (hin : ∀ sd ∈ D.sources.toList, SourceInputBound sd i.size)
    (hstb : ∀ sd ∈ D.sources.toList, SourceFlopBound sd st.flops.size)
    (hfclk : ∀ f ∈ D.flops.toList, f.clock < e.size)
    (hmclk : ∀ md ∈ D.memories.toList, md.clock < e.size)
    (hmspan : ∀ md ∈ D.memories.toList, md.domSpan ≤ e.size)
    (hfsize : D.flops.size ≤ st.flops.size) :
    ∃ ss : List SlotV,
      PrefixRelM D i st G (D.sources.size + D.nodes.size) ss
      ∧ ss.take D.sources.size = D.sources.toList.map (srcSlotV i st)
      ∧ NodesOK D i st G 0 ss
      ∧ SEval hwMemS []
          (.call "mainM" [.lit (encDesign D), .lit (encEdges e), .lit (encInput i),
                          .lit (StateM.encStateM st)])
          (StateM.encResultM (resOf D e i st ss)) := by
  obtain ⟨ss, hss, htake, hok, hfold⟩ :=
    sourcesThenNodes_keep hsem hstate hGsrc hstep
  have hlen : ss.length = D.sources.size + D.nodes.size := hss.len
  have hlenMap : (ss.map SlotV.enc).length = D.sources.size + D.nodes.size := by
    simpa using hlen
  -- the memory premises, from the checker
  have hmnext : ∀ md ∈ D.memories.toList, md.nextImg < ss.length := by
    intro md hmd; rw [hlen]; exact hwf.slotsInRange.2.2 md hmd
  have hmkind : ∀ md ∈ D.memories.toList,
      Direct.slotIsMem D md.nextImg = some true := by
    intro md hmd
    obtain ⟨k, hk⟩ := array_mem_getElem? hmd
    exact hsem.memKinds k md hk
  -- a commit table's images (decision #9), from the checker
  have hmckind : ∀ md ∈ D.memories.toList, ∀ c ∈ md.commits,
      Direct.slotIsMem D c.nextImg = some true := by
    intro md hmd c hc
    obtain ⟨k, hk⟩ := array_mem_getElem? hmd
    exact hsem.memCommitKinds k md hk c hc
  have hmcnext : ∀ md ∈ D.memories.toList, ∀ c ∈ md.commits, c.nextImg < ss.length := by
    intro md hmd c hc
    rw [hlen]
    simpa [DesignCert.numSlots] using Direct.slotIsMem_lt (hmckind md hmd c hc)
  -- an asynchronous reset's image (decision #2), from the checker
  have hmakind : ∀ md ∈ D.memories.toList, ∀ a, md.asyncImg = some a →
      Direct.slotIsMem D a = some true := by
    intro md hmd a ha
    obtain ⟨k, hk⟩ := array_mem_getElem? hmd
    exact hsem.memAsyncKinds k md hk a ha
  have hmanext : ∀ md ∈ D.memories.toList, ∀ a, md.asyncImg = some a → a < ss.length := by
    intro md hmd a ha
    rw [hlen]
    simpa [DesignCert.numSlots] using Direct.slotIsMem_lt (hmakind md hmd a ha)
  -- the reference, piece by piece
  refine ⟨ss, hss, htake, hok, ?_⟩
  · -- THE OBJECT
    refine SEval_call4 .lit .lit .lit .lit rfl rfl ?_
    refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
    refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
    refine SEval.letN (lift (lenL_agree D.sources.toList _ _ (.ref rfl))) ?_
    refine SEval.letN (lift (lenL_agree D.nodes.toList _ _ (.ref rfl))) ?_
    have hcast : Int.ofNat D.sources.size + Int.ofNat D.nodes.size
        = Int.ofNat (D.sources.size + D.nodes.size) := by
      simp only [Int.ofNat_eq_natCast]; omega
    refine SEval.letN
      (SEval_prim2 (a := Val.int (Int.ofNat D.sources.size))
        (b := Val.int (Int.ofNat D.nodes.size))
        (v := Val.int (Int.ofNat (D.sources.size + D.nodes.size)))
        (.ref rfl) (.ref rfl) (by simp only [evalPrim]; rw [hcast])) ?_
    refine SEval.letN (d := objEnv ((D.sources.toList.map (srcSlotV i st)).map SlotV.enc))
      (by
        rw [List.map_map]
        exact mkSourcesM_objEnv D.sources.toList hin hstb (.ref rfl) (.ref rfl)
          (.ref rfl) (.ref rfl) .lit) ?_
    refine SEval.letN (hfold _ _ _ _ (.ref rfl) (.ref rfl) (.ref rfl)) ?_
    refine SEval.letN (lift (mkOutputs_agreeOn D.outputs.toList _ _ _ _
      (outputs_scalar hwf hsem hss)
      (.ref rfl) (.ref rfl) (by rw [hlenMap]; exact .ref rfl))) ?_
    refine SEval.letN (lift (flopNexts_agreeOn (st := StateM.denote st)
      D.flops.toList 0 _ _ _ _ _ _ _
      (flops_scalar hwf hsem hss hfclk)
      (by simpa using hfsize)
      (.ref rfl) (.ref rfl) (.ref rfl) (by rw [hlenMap]; exact .ref rfl)
      (.ref rfl) .lit)) ?_
    refine SEval.letN (memNexts_agree hss D.memories.toList 0 _ _ _ _ _ _ _
      hmclk hmspan hmnext hmkind hmcnext hmckind hmanext hmakind (.ref rfl) (.ref rfl) (.ref rfl)
      (by rw [hlen]; exact .ref rfl) (.ref rfl) .lit) ?_
    exact SEval.mk (.cons (SEval.mk (.cons (.ref rfl) (.cons (.ref rfl) .nil)))
      (.cons (.ref rfl) .nil))

theorem mainM_explicit_resolved {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hstate : StateShapeOK D st)
    (hGsrc : ∀ k, k < D.sources.size → G k = srcGeomOf D k)
    (hstep : ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c → j < D.nodes.size →
               c ∈ D.nodes.toList →
               ∀ ss : List SlotV, PrefixRelM D i st G (D.sources.size + j) ss →
                 ∃ v : SlotV, NodeStepOK D i st G j c ss v)
    (hin : ∀ sd ∈ D.sources.toList, SourceInputBound sd i.size)
    (hstb : ∀ sd ∈ D.sources.toList, SourceFlopBound sd st.flops.size)
    (hfclk : ∀ f ∈ D.flops.toList, f.clock < e.size)
    (hmclk : ∀ md ∈ D.memories.toList, md.clock < e.size)
    (hmspan : ∀ md ∈ D.memories.toList, md.domSpan ≤ e.size)
    (hfsize : D.flops.size ≤ st.flops.size) :
    ∃ ss : List SlotV,
      PrefixRelM D i st G (D.sources.size + D.nodes.size) ss
      ∧ ss.take D.sources.size = D.sources.toList.map (srcSlotV i st)
      ∧ NodesOK D i st G 0 ss
      ∧ Eval hwMemP []
          (.call hwMemP.entry
            ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
          (StateM.encResultM (resOf D e i st ss)) := by
  obtain ⟨ss, hss, htake, hok, hev⟩ :=
    mainM_explicit hwf hsem hstate hGsrc hstep hin hstb hfclk hmclk hmspan hfsize
  refine ⟨ss, hss, htake, hok, ?_⟩
  refine SEval_entry hwMemP_resolves ?_
  simpa [hwMemS_entry] using hev


end HwMem
end Projection

open Projection Projection.HwMem Compiler
namespace HDLProof.Sym

/-- **`resM` is the interpreter's result on its own slot list.** The slot list comes with the
prefix relation, the sources and every node's step; the result is `resOf` of it. -/
theorem resM_explicit {D : DesignCert} {e : ClockEdges} {i : RuntimeInput} {st : StateM}
    (hA : admitsWB D = true) (hR : runtimeFullOKB D i st e = true) :
    ∃ ss : List SlotV,
      PrefixRelM D i st (geomAtW D) (D.sources.size + D.nodes.size) ss
      ∧ ss.take D.sources.size = D.sources.toList.map (srcSlotV i st)
      ∧ NodesOK D i st (geomAtW D) 0 ss
      ∧ resM D e i st = resOf D e i st ss := by
  have hR' := hR
  unfold runtimeFullOKB at hR'
  simp only [Bool.and_eq_true] at hR'
  obtain ⟨hR0, -⟩ := hR'
  unfold runtimeOKB at hR0
  simp only [Bool.and_eq_true] at hR0
  obtain ⟨⟨⟨hst, hin⟩, hfb⟩, hed⟩ := hR0
  have hwf   := admitsW_wf hA
  have hsem  := admitsW_semwf hA
  have hgsrc := admitsW_geom_src hA
  have hadm  := admitsW_nodeAdmitted hA
  have harit := admitsW_arity hA
  have hprop := admitsW_writePropagates hA
  have hfits := admits_writeAddrFitsW hA i st
  obtain ⟨ss, hss, htake, hok, hev⟩ :=
    mainM_explicit_resolved (e := e) (G := geomAtW D) hwf hsem (stateOKB_shape hst) hgsrc
      (fun j c hcj hj hmemc ss hpre =>
        node_stepW hwf hsem hcj hj (hadm _ hmemc) (harit _ hmemc)
          (hprop j _ hcj) (hfits _ hmemc) hpre)
      (inputOKB_sound hin) (flopBoundOKB_sound hfb) (edgesOKB_flops hed) (edgesOKB_mems hed)
      (edgesOKB_span hed) (stateOKB_flopSize hst)
  refine ⟨ss, hss, htake, hok, ?_⟩
  have h1 := (resM_spec hA hR).1
  have h2 := Surface.Eval_det h1 hev
  have h3 := congrArg StateM.decResultM h2
  simp only [StateM.decResultM_encResultM, Option.some.injEq] at h3
  exact h3

#audit_axioms resM_explicit

end HDLProof.Sym