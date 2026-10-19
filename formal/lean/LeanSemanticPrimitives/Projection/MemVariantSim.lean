/-
# The checked memory simulator, specialized through the `env0`-inlined variant

`MemProjection.runMemSim_correct` and `HardwareMemWriteAdmit.runMemSimW_correct`
hold for a bundle whose residual came from `projectMemDesignAt`. The
execution function `runMemSim`, the bundle constructors `mkMemSimOf` /
`mkMemSimOfW` and the production checker `checkResidual` do not mention the
specializer, so they are REUSED unchanged here. Only the honesty predicate
names a different specializer: `projectMemVarDesignAt`, whose correctness
(`MemVariant.projectMemVarDesignAt(W)_correct`) has the same conclusion.
-/
import LeanSemanticPrimitives.Projection.MemVariant

namespace Projection
namespace MemVariant

open Surface Compiler Projection.Hw Projection.HwMem Projection.MemProjection

/-- The honest bundle, original admission, variant specializer. -/
structure MemSimWFAtV (sf wf : Nat) (sim : MemSimulator) : Prop where
  proj  : projectMemVarDesignAt sf wf sim.design = .ok sim.prog
  chk   : checkResidual sim.prog = some sim.bound
  admit : admitsB sim.design = true

/-- The honest bundle, plain-write admission, variant specializer. -/
structure MemSimWFAtWV (sf wf : Nat) (sim : MemSimulator) : Prop where
  proj  : projectMemVarDesignAt sf wf sim.design = .ok sim.prog
  chk   : checkResidual sim.prog = some sim.bound
  admit : admitsWB sim.design = true

theorem mkMemSimOf_wfV {sf wf : Nat} {D : DesignCert} {R : Program}
    {sim : MemSimulator} (hproj : projectMemVarDesignAt sf wf D = .ok R)
    (h : mkMemSimOf D R = some sim) : MemSimWFAtV sf wf sim := by
  obtain ⟨hd, hp, hchk, hadm⟩ := mkMemSimOf_parts h
  exact ⟨by rw [hd, hp]; exact hproj, by rw [hp]; exact hchk, by rw [hd]; exact hadm⟩

theorem mkMemSimOfW_wfV {sf wf : Nat} {D : DesignCert} {R : Program}
    {sim : MemSimulator} (hproj : projectMemVarDesignAt sf wf D = .ok R)
    (h : mkMemSimOfW D R = some sim) : MemSimWFAtWV sf wf sim := by
  obtain ⟨hd, hp, hchk, hadm⟩ := mkMemSimOfW_parts h
  exact ⟨by rw [hd, hp]; exact hproj, by rw [hp]; exact hchk, by rw [hd]; exact hadm⟩

/-- **`runMemSim_correct`, through the variant.** -/
theorem runMemSimV_correct {sf wf : Nat} {sim : MemSimulator} (hw : MemSimWFAtV sf wf sim)
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hR : runtimeFullOKB sim.design i st e = true) :
    ∃ r : StateM.ResultM,
      runMemSim sim e i st = some r
      ∧ r.denote = Compiler.interpretDesign sim.design e i (StateM.denote st)
      ∧ stateFullOKB sim.design r.nextState = true := by
  obtain ⟨r, hden, hok, hiff⟩ := projectMemVarDesignAt_correct hw.proj hw.admit hR
  have hev : Eval sim.prog [] (.call sim.prog.entry
      [.lit (encEdges e), .lit (encInput i), .lit (StateM.encStateM st)])
      (StateM.encResultM r) := (hiff _).mpr rfl
  have hfuel := checkResidual_complete hw.chk hev
  refine ⟨r, ?_, hden, hok⟩
  unfold runMemSim
  rw [hfuel]
  exact StateM.decResultM_encResultM r

/-- **`runMemSimW_correct`, through the variant.** -/
theorem runMemSimWV_correct {sf wf : Nat} {sim : MemSimulator} (hw : MemSimWFAtWV sf wf sim)
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hR : runtimeFullOKB sim.design i st e = true) :
    ∃ r : StateM.ResultM,
      runMemSim sim e i st = some r
      ∧ r.denote = Compiler.interpretDesign sim.design e i (StateM.denote st)
      ∧ stateFullOKB sim.design r.nextState = true := by
  obtain ⟨r, hden, hok, hiff⟩ := projectMemVarDesignAtW_correct hw.proj hw.admit hR
  have hev : Eval sim.prog [] (.call sim.prog.entry
      [.lit (encEdges e), .lit (encInput i), .lit (StateM.encStateM st)])
      (StateM.encResultM r) := (hiff _).mpr rfl
  have hfuel := checkResidual_complete hw.chk hev
  refine ⟨r, ?_, hden, hok⟩
  unfold runMemSim
  rw [hfuel]
  exact StateM.decResultM_encResultM r

#audit_axioms mkMemSimOf_wfV
#audit_axioms mkMemSimOfW_wfV
#audit_axioms runMemSimV_correct
#audit_axioms runMemSimWV_correct

end MemVariant
end Projection
