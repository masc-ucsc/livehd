/-
# Outputs, flops and the whole cycle, in a mixed environment

Three things:

1. the scalar premises the mirrored output/flop family needs, taken from
   `Direct.DesignSemWF.outputKinds` and `flopKinds` plus
   `DesignCertWF.slotsInRange` — the CHECKER's own fields, not assumptions;
2. the array-length/index bridge from the pointwise `srcMemNext` lemmas to the
   complete `RuntimeState` memory array;
3. `mainM_adequate`: `mkSourcesM`, the corrected node fold, `mkOutputs`,
   `flopNexts` and `memNexts` composed into one cycle, whose result DENOTES
   `Compiler.interpretDesign`.

The node fold's EVALUATION witness is retained and used — `mainM` is proved by
running the actual chain, not by assuming an environment appears.
-/
import LeanSemanticPrimitives.Projection.HardwareMemOutFlop

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw Projection.HwLocal

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. Scalar premises, from the checker -/

theorem array_mem_getElem? {α : Type} {a : Array α} {x : α} (h : x ∈ a.toList) :
    ∃ k : Nat, a[k]? = some x := by
  obtain ⟨k, hk, hx⟩ := List.mem_iff_getElem.mp h
  refine ⟨k, ?_⟩
  have hks : k < a.size := by simpa using hk
  rw [Array.getElem?_eq_getElem hks]
  simp [← hx]

/-- **Every output slot is a scalar**, by `outputKinds`, and in range by
`slotsInRange`. -/
theorem outputs_scalar {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {ss : List SlotV}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hpre : PrefixRelM D i st G D.numSlots ss) :
    ∀ o ∈ D.outputs.toList,
      SlotHere (ss.map SlotV.enc) (fun k => (slotValM D i st k).asBV) o.slot := by
  intro o ho
  obtain ⟨k, hk⟩ := array_mem_getElem? ho
  have hkind := hsem.outputKinds k o hk
  have hb : o.slot < ss.length := by
    rw [hpre.len]; exact hwf.slotsInRange.1 o ho
  exact ⟨by simpa using hb, dep_scalar hpre hb hkind⟩

/-- **Every flop pin is a scalar**, by `flopKinds`, in range by `slotsInRange`,
and on a declared clock by `memClocks`'s flop counterpart. -/
theorem flops_scalar {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {ss : List SlotV} {e : ClockEdges}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hpre : PrefixRelM D i st G D.numSlots ss)
    (hfclk : ∀ f ∈ D.flops.toList, f.clock < e.size) :
    ∀ f ∈ D.flops.toList,
      f.clock < e.size
      ∧ SlotHere (ss.map SlotV.enc) (fun k => (slotValM D i st k).asBV) f.din
      ∧ (∀ x, f.enable = some x →
            SlotHere (ss.map SlotV.enc) (fun k => (slotValM D i st k).asBV) x)
      ∧ (∀ r, f.resetPin = some r →
            SlotHere (ss.map SlotV.enc) (fun k => (slotValM D i st k).asBV) r) := by
  intro f hf
  obtain ⟨k, hk⟩ := array_mem_getElem? hf
  obtain ⟨hkd, hke, hkr⟩ := hsem.flopKinds k f hk
  obtain ⟨hbd, hbe, hbr⟩ := hwf.slotsInRange.2.1 f hf
  have hbd' : f.din < ss.length := by rw [hpre.len]; exact hbd
  refine ⟨hfclk f hf, ⟨by simpa using hbd', dep_scalar hpre hbd' hkd⟩, ?_, ?_⟩
  · intro x hx
    have hbx : x < ss.length := by rw [hpre.len]; exact hbe x hx
    exact ⟨by simpa using hbx, dep_scalar hpre hbx (hke x hx)⟩
  · intro r hr
    have hbr' : r < ss.length := by rw [hpre.len]; exact hbr r hr
    exact ⟨by simpa using hbr', dep_scalar hpre hbr' (hkr r hr)⟩

/-! ## 2. The array bridge

The pointwise `srcMemNext` lemmas become a statement about the COMPLETE
`RuntimeState` memory array: same length, and the same function at every index
— hence the same `BV` at every address of every memory. -/

theorem memNextsOf_denote_array {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {m : Nat} {ss : List SlotV} {e : ClockEdges}
    (hpre : PrefixRelM D i st G m ss)
    (hnext : ∀ md ∈ D.memories.toList, md.nextImg < ss.length)
    (hkind : ∀ md ∈ D.memories.toList,
               Direct.slotIsMem D md.nextImg = some true) :
    ((memNextsOf e st ss 0 D.memories.toList).toArray).map MemImg.denote
      = D.memories.mapIdx
          (fun idx md => Compiler.srcMemNext (slotValM D i st) e (StateM.denote st) idx md) := by
  apply Array.ext
  · simp
  · intro k h1 h2
    have hk : k < D.memories.size := by simpa using h2
    have hlist : D.memories.toList[k]? = some D.memories[k] := by
      rw [← Array.getElem?_eq_getElem hk]; simp
    have hmem : D.memories[k] ∈ D.memories.toList := by
      simpa using Array.getElem_mem hk
    have hget := memNextsOf_get (e := e) (st := st) (ss := ss)
      D.memories.toList 0 k D.memories[k] hlist
    have hval : (memNextsOf e st ss 0 D.memories.toList)[k]'(by simpa using h1)
        = memNextOf e st ss k D.memories[k] := by
      have := hget
      rw [List.getElem?_eq_getElem (by simpa using h1)] at this
      simpa using this
    simp only [Array.getElem_map, List.getElem_toArray]
    rw [hval]
    simp only [Array.getElem_mapIdx]
    exact memNextOf_denote hpre (hnext _ hmem) (hkind _ hmem)

/-! ## 3. The whole cycle

`mainM` binds the six certificate fields and the memory-bearing state, builds
the source environment with `mkSourcesM`, folds the nodes over it, reads the
outputs, computes the flop vector and commits each memory on its own clock.
Every piece is a lemma above; this is where they compose.

The node fold's EVALUATION witness is used here — the environment `mainM` hands
to `mkOutputs`, `flopNexts` and `memNexts` is the one `evalNodesM` actually
produced, not one assumed to exist. -/

theorem mainM_adequate {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hstate : StateShapeOK D st)
    (hGsrc : ∀ k, k < D.sources.size → G k = srcGeomOf D k)
    (hadm : ∀ c ∈ D.nodes.toList, NodeAdmitted c)
    (harity : ∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList)
    (hprop : ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c →
               WritePropagates D G j c)
    (hfits : ∀ c ∈ D.nodes.toList, WriteAddrFits D i st G c)
    (hin : ∀ sd ∈ D.sources.toList, SourceInputBound sd i.size)
    (hstb : ∀ sd ∈ D.sources.toList, SourceFlopBound sd st.flops.size)
    (hfclk : ∀ f ∈ D.flops.toList, f.clock < e.size)
    (hmclk : ∀ md ∈ D.memories.toList, md.clock < e.size)
    (hfsize : D.flops.size ≤ st.flops.size) :
    ∃ r : StateM.ResultM,
      SEval hwMemS []
        (.call "mainM" [.lit (encDesign D), .lit (encEdges e), .lit (encInput i),
                        .lit (StateM.encStateM st)])
        (StateM.encResultM r)
      ∧ r.denote = Compiler.interpretDesign D e i (StateM.denote st) := by
  obtain ⟨ss, hss, hfold⟩ :=
    sourcesThenNodes hwf hsem hstate hGsrc hadm harity hprop hfits
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
  -- the reference, piece by piece
  refine ⟨{ nextState :=
              { flops := (flopNextsFrom (slotValM D i st) e (StateM.denote st) 0
                            D.flops.toList).toArray
              , mems  := (memNextsOf e st ss 0 D.memories.toList).toArray }
          , outputs :=
              (D.outputs.toList.map
                 (fun o => bv_resize o.width ((slotValM D i st) o.slot).asBV)).toArray },
          ?_, ?_⟩
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
      hmclk hmnext hmkind (.ref rfl) (.ref rfl) (.ref rfl)
      (by rw [hlen]; exact .ref rfl) (.ref rfl) .lit) ?_
    exact SEval.mk (.cons (SEval.mk (.cons (.ref rfl) (.cons (.ref rfl) .nil)))
      (.cons (.ref rfl) .nil))
  · -- THE REFERENCE
    have houts : ((D.outputs.toList.map
          (fun o => bv_resize o.width ((slotValM D i st) o.slot).asBV)).toArray)
        = D.outputs.map (fun o => bv_resize o.width ((slotValM D i st) o.slot).asBV) := by
      apply Array.ext <;> simp
    have hflops : ((flopNextsFrom (slotValM D i st) e (StateM.denote st) 0
          D.flops.toList).toArray)
        = D.flops.mapIdx
            (fun idx f => srcFlopNext (slotValM D i st) e (StateM.denote st) idx f) := by
      rw [flopNextsFrom_mapIdx]
    have hmems := memNextsOf_denote_array (e := e) hss hmnext hmkind
    have href : Compiler.interpretDesign D e i (StateM.denote st)
        = { outputs := D.outputs.map
              (fun o => bv_resize o.width ((slotValM D i st) o.slot).asBV)
          , nextState :=
              { flops := D.flops.mapIdx
                  (fun idx f => srcFlopNext (slotValM D i st) e (StateM.denote st) idx f)
              , mems := D.memories.mapIdx
                  (fun idx md => srcMemNext (slotValM D i st) e (StateM.denote st) idx md) } } :=
      rfl
    have hns : StateM.denote
          { flops := (flopNextsFrom (slotValM D i st) e (StateM.denote st) 0
                        D.flops.toList).toArray
          , mems  := (memNextsOf e st ss 0 D.memories.toList).toArray }
        = { flops := D.flops.mapIdx
              (fun idx f => srcFlopNext (slotValM D i st) e (StateM.denote st) idx f)
          , mems  := D.memories.mapIdx
              (fun idx md => srcMemNext (slotValM D i st) e (StateM.denote st) idx md) } := by
      show ({ flops := _, mems := Array.map MemImg.denote _ } : RuntimeState) = _
      rw [hflops, hmems]
    rw [href]
    simp only [StateM.ResultM.denote]
    rw [hns, houts]

/-- **The same, through the runtime denotation relation.**  `ResultRelR v r`
says `v` decodes to a representable witness whose denotation IS the reference
`RuntimeResult` — so this is adequacy against `interpretDesign` stated in the
form §63.30's relations were built for. -/
theorem mainM_resultRel {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} {G : Nat → Option Nat}
    (hwf : DesignCert.DesignCertWF D) (hsem : Direct.DesignSemWF D)
    (hstate : StateShapeOK D st)
    (hGsrc : ∀ k, k < D.sources.size → G k = srcGeomOf D k)
    (hadm : ∀ c ∈ D.nodes.toList, NodeAdmitted c)
    (harity : ∀ c ∈ D.nodes.toList, ArityOK c.op c.deps.toList)
    (hprop : ∀ (j : Nat) (c : DenseNodeCert), D.nodes[j]? = some c →
               WritePropagates D G j c)
    (hfits : ∀ c ∈ D.nodes.toList, WriteAddrFits D i st G c)
    (hin : ∀ sd ∈ D.sources.toList, SourceInputBound sd i.size)
    (hstb : ∀ sd ∈ D.sources.toList, SourceFlopBound sd st.flops.size)
    (hfclk : ∀ f ∈ D.flops.toList, f.clock < e.size)
    (hmclk : ∀ md ∈ D.memories.toList, md.clock < e.size)
    (hfsize : D.flops.size ≤ st.flops.size) :
    ∃ v : Val,
      SEval hwMemS []
        (.call "mainM" [.lit (encDesign D), .lit (encEdges e), .lit (encInput i),
                        .lit (StateM.encStateM st)]) v
      ∧ StateM.ResultRelR v (Compiler.interpretDesign D e i (StateM.denote st)) := by
  obtain ⟨r, hev, hden⟩ :=
    mainM_adequate hwf hsem hstate hGsrc hadm harity hprop hfits hin hstb hfclk hmclk hfsize
  refine ⟨StateM.encResultM r, hev, ?_⟩
  rw [← hden]
  exact StateM.ResultRelR_encResultM r

#print axioms mainM_adequate
#print axioms mainM_resultRel

#print axioms outputs_scalar
#print axioms flops_scalar
#print axioms memNextsOf_denote_array

end HwMem
end Projection
