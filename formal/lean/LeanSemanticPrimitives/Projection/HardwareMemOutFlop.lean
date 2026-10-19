/-
# Outputs and flop updates in a MIXED environment

`Hw.mkOutputs_agree` and the flop family take the GLOBAL premise
`SlotVals vals arg`, which §63.33 proved unsatisfiable in any environment
holding an `encMemImg`.  They are mirrored here with the local condition
`SlotHere`, exactly as the 27 operator lemmas were: `SlotVals` is unchanged,
every original theorem keeps its meaning, and each mirrored proof is the
original with the slot read localized.  No scalar behaviour is restated.

Preserved, because it is the same code: active/quiet clocks (`firesAt`), the
flop ENABLE, SYNCHRONOUS reset, ASYNCHRONOUS reset with its polarity
(`rstActive`, `xor f.resetActiveLow`), and the old-state hold.

## Where the premises come from

`Direct.DesignSemWF.outputKinds` says every output slot is a bit vector, and
`flopKinds` says a flop's `din`, `enable` and `resetPin` are.  With the
invariant's `kind` field those give `SlotHere` at exactly the slots these
lemmas read — so the scalar premises are the CHECKER's, not assumptions.
-/
import LeanSemanticPrimitives.Projection.HardwareMemCommit
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace HwLocal

open Surface Compiler Projection.Hw

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000


theorem mkOutputs_agreeOn {vals : List Val} {arg : Nat → BV} :
    ∀ (outs : List OutputDesc) (σ : SEnv) (eo ee en : SExp),
      (∀ o ∈ outs, SlotHere vals arg o.slot) →
      SEval hwS σ eo (encListG encOutput outs) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ en (.int (Int.ofNat vals.length)) →
      SEval hwS σ (.call "mkOutputs" [eo, ee, en])
        (encListG encBV (outs.map (fun o => bv_resize o.width (arg o.slot))))
  | [],      _, _, _, _, _,  ho, henv, hlen => by
      refine SEval_call3 ho henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) .lit
  | o :: os, _, _, _, _, hb, ho, henv, hlen => by
      refine SEval_call3 ho henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval_switch_of_tag (SEval_hd (.ref rfl)) rfl rfl ?_
      exact SEval_consP
        (SEval_prim2 (.ref rfl)
          (slot_readH (hb o List.mem_cons_self) (.ref rfl) (.ref rfl) (.ref rfl)) rfl)
        (mkOutputs_agreeOn os _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl))

theorem rstActive_agreeOn {vals : List Val} {arg : Nat → BV}
    {σ : SEnv} {erp eal ee enn : SExp} {rp : Option Nat} {al : Bool}
    (hbnd : ∀ r, rp = some r → SlotHere vals arg r)
    (hrp : SEval hwS σ erp (encONat rp)) (hal : SEval hwS σ eal (.bool al))
    (henv : SEval hwS σ ee (objEnv vals))
    (hlen : SEval hwS σ enn (.int (Int.ofNat vals.length))) :
    SEval hwS σ (.call "rstActive" [erp, eal, ee, enn]) (.bool (rstOf arg al rp)) := by
  cases rp with
  | none =>
      refine SEval_call4 hrp hal henv hlen rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) .lit
  | some r =>
      refine SEval_call4 hrp hal henv hlen rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      cases al with
      | false =>
          simp only [rstOf, Bool.false_xor]
          exact SEval.iteF (.ref rfl)
            (nz_agree (slot_readH (hbnd r rfl) (.ref rfl) (.ref rfl)
              (SEval_hd (.ref rfl))))
      | true =>
          simp only [rstOf, Bool.true_xor]
          exact SEval.iteT (.ref rfl)
            (SEval_prim1 (nz_agree (slot_readH (hbnd r rfl) (.ref rfl) (.ref rfl)
              (SEval_hd (.ref rfl)))) rfl)

theorem capture_agreeOn {vals : List Val} {arg : Nat → BV}
    {σ : SEnv} {een eedge ew edin ee enn efq ei : SExp}
    {enOpt : Option Nat} {edge : Bool} {w din idx : Nat} {st : RuntimeState}
    (hben : ∀ x, enOpt = some x → SlotHere vals arg x)
    (hbdin : SlotHere vals arg din) (hbidx : idx < st.flops.size)
    (h1 : SEval hwS σ een (encONat enOpt))
    (h2 : SEval hwS σ eedge (.bool edge))
    (h3 : SEval hwS σ ew (.int (Int.ofNat w)))
    (h4 : SEval hwS σ edin (.int (Int.ofNat din)))
    (h5 : SEval hwS σ ee (objEnv vals))
    (h6 : SEval hwS σ enn (.int (Int.ofNat vals.length)))
    (h7 : SEval hwS σ efq (encBVs st.flops))
    (h8 : SEval hwS σ ei (.int (Int.ofNat idx))) :
    SEval hwS σ (.call "capture" [een, eedge, ew, edin, ee, enn, efq, ei])
      (encBV (if edge && enabledOf arg enOpt
              then bv_resize w (arg din)
              else st.flops[idx]?.getD (mk_bv w 0))) := by
  have hold : st.flops[idx]?.getD (mk_bv w 0) = st.flops[idx] := by
    simp [Array.getElem?_eq_getElem hbidx]
  rw [hold]
  refine SEval_call8 h1 h2 h3 h4 h5 h6 h7 h8 rfl rfl ?_
  cases enOpt with
  | none =>
      refine SEval.iteT (SEval_prim1 (.ref rfl) rfl) ?_
      simp only [enabledOf, Bool.and_true]
      cases edge with
      | false =>
          simp only [Bool.false_eq_true, if_false]
          exact SEval.iteF (.ref rfl) (nthD_arr hbidx (.ref rfl) (.ref rfl))
      | true =>
          simp only [if_true]
          exact SEval.iteT (.ref rfl)
            (SEval_prim2 (.ref rfl)
              (slot_readH hbdin (.ref rfl) (.ref rfl) (.ref rfl)) rfl)
  | some x =>
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      simp only [enabledOf]
      refine SEval_ite_of_bool (bb := edge && bv_nonzero (arg x))
        (SEval_prim2 (.ref rfl)
          (nz_agree (slot_readH (hben x rfl) (.ref rfl) (.ref rfl)
            (SEval_hd (.ref rfl)))) rfl) ?_ ?_
      · intro hT
        simp only [hT, if_true]
        exact SEval_prim2 (.ref rfl)
          (slot_readH hbdin (.ref rfl) (.ref rfl) (.ref rfl)) rfl
      · intro hF
        simp only [hF, Bool.false_eq_true, if_false]
        exact nthD_arr hbidx (.ref rfl) (.ref rfl)

theorem flopNext_agreeOn {vals : List Val} {rho : Nat → CertVal}
   
    {e : ClockEdges} {st : RuntimeState} {idx : Nat} {f : FlopDesc}
    (hck : f.clock < e.size) (hdin : SlotHere vals (fun k => (rho k).asBV) f.din)
    (hen : ∀ x, f.enable = some x → SlotHere vals (fun k => (rho k).asBV) x)
    (hrpb : ∀ r, f.resetPin = some r → SlotHere vals (fun k => (rho k).asBV) r)
    (hidx : idx < st.flops.size)
    {σ : SEnv} {ef eedge ee enn efq ei : SExp}
    (h1 : SEval hwS σ ef (encFlop f))
    (h2 : SEval hwS σ eedge (encEdges e))
    (h3 : SEval hwS σ ee (objEnv vals))
    (h4 : SEval hwS σ enn (.int (Int.ofNat vals.length)))
    (h5 : SEval hwS σ efq (encBVs st.flops))
    (h6 : SEval hwS σ ei (.int (Int.ofNat idx))) :
    SEval hwS σ (.call "flopNext" [ef, eedge, ee, enn, efq, ei])
      (encBV (srcFlopNext rho e st idx f)) := by
  rw [srcFlopNext_named]
  refine SEval_call6 h1 h2 h3 h4 h5 h6 rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval.letN (firesAt_agree hck (.ref rfl) (.ref rfl)) ?_
  refine SEval.letN (capture_agreeOn hen hdin hidx (.ref rfl) (.ref rfl) (.ref rfl)
    (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)) ?_
  cases hrp : f.resetPin with
  | none =>
      refine SEval.iteT (SEval_prim1 (.ref rfl) rfl) ?_
      simp only [rstOf, Bool.false_and, Bool.false_eq_true, if_false]
      exact .ref rfl
  | some r =>
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      cases har : f.asyncReset with
      | true =>
          simp only [Bool.or_true, Bool.and_true]
          refine SEval.iteT (.ref rfl) ?_
          refine SEval_ite_of_bool
            (bb := rstOf (fun k => (rho k).asBV) f.resetActiveLow (some r)) ?_ ?_ ?_
          · rw [← hrp]
            exact rstActive_agreeOn hrpb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)
          · intro hT; simp only [hT, if_true]; exact SEval_prim2 (.ref rfl) (.ref rfl) rfl
          · intro hF
            simp only [hF, Bool.false_eq_true, if_false]
            exact .ref rfl
      | false =>
          simp only [Bool.or_false]
          refine SEval.iteF (.ref rfl) ?_
          refine SEval_ite_of_bool
            (bb := rstOf (fun k => (rho k).asBV) f.resetActiveLow (some r) && fires e f.clock)
            (SEval_prim2 (a := Val.bool (rstOf (fun k => (rho k).asBV) f.resetActiveLow (some r)))
              ?_ (.ref rfl) rfl) ?_ ?_
          · rw [← hrp]
            exact rstActive_agreeOn hrpb (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)
          · intro hT; simp only [hT, if_true]; exact SEval_prim2 (.ref rfl) (.ref rfl) rfl
          · intro hF
            simp only [hF, Bool.false_eq_true, if_false]
            exact .ref rfl

/-- The flop vector as the object builds it: one `srcFlopNext` per flop, with
the index counting up.  Spelled as its own recursion rather than through
`mapIdx` so that the fold lemma is the recursion itself and the bridge to
`Array.mapIdx` is one separate, elementwise statement. -/

theorem flopNexts_agreeOn {vals : List Val} {rho : Nat → CertVal}
    {e : ClockEdges} {st : RuntimeState} :
    ∀ (fl : List FlopDesc) (idx : Nat) (σ : SEnv) (ef eedge ee enn efq ei : SExp),
      (∀ f ∈ fl, f.clock < e.size ∧ SlotHere vals (fun k => (rho k).asBV) f.din ∧
         (∀ x, f.enable = some x → SlotHere vals (fun k => (rho k).asBV) x) ∧
         (∀ r, f.resetPin = some r → SlotHere vals (fun k => (rho k).asBV) r)) →
      idx + fl.length ≤ st.flops.size →
      SEval hwS σ ef (encListG encFlop fl) →
      SEval hwS σ eedge (encEdges e) →
      SEval hwS σ ee (objEnv vals) →
      SEval hwS σ enn (.int (Int.ofNat vals.length)) →
      SEval hwS σ efq (encBVs st.flops) →
      SEval hwS σ ei (.int (Int.ofNat idx)) →
      SEval hwS σ (.call "flopNexts" [ef, eedge, ee, enn, efq, ei])
        (encListG encBV (flopNextsFrom rho e st idx fl))
  | [],      _,   _, _, _, _, _, _, _, _,  _,    hfv, hev, henv, hlen, hfq, hi => by
      refine SEval_call6 hfv hev henv hlen hfq hi rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) .lit
  | f :: fs, idx, _, _, _, _, _, _, _, hb, hidx, hfv, hev, henv, hlen, hfq, hi => by
      refine SEval_call6 hfv hev henv hlen hfq hi rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      obtain ⟨hck, hdin, hen, hrp⟩ := hb f List.mem_cons_self
      have hidx0 : idx < st.flops.size := by simp only [List.length_cons] at hidx; omega
      exact SEval_consP
        (flopNext_agreeOn hck hdin hen hrp hidx0 (SEval_hd (.ref rfl)) (.ref rfl)
          (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl))
        (flopNexts_agreeOn fs (idx + 1) _ _ _ _ _ _ _
          (fun x hx => hb x (List.mem_cons_of_mem _ hx))
          (by simp only [List.length_cons] at hidx; omega)
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)
          (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim] <;> omega)))
#audit_axioms mkOutputs_agreeOn
#audit_axioms rstActive_agreeOn
#audit_axioms capture_agreeOn
#audit_axioms flopNext_agreeOn
#audit_axioms flopNexts_agreeOn

end HwLocal
end Projection
