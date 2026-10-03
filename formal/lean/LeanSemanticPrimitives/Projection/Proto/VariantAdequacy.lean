/-
# B2: the canonical witness for the TOTAL variant

`IHwAdequate_proved` does not use a surface completeness theorem.  It
CONSTRUCTS one evaluation and reads both directions off it: `Eval_det` forward,
`ResultRel_canonical` back.  So the variant needs its own witness and nothing
else, which is what this file builds.

The route, deliberately narrow:

* `main_agree` (`HardwareAdequacy.lean:1787`) proves the witness for `hwS` by a
  `refine` script over the body's outer `call`/`switch`/`let` structure, with
  one `*_agree` lemma per binding.  Those lemmas are SIGMA-POLYMORPHIC, so they
  apply unchanged at the variant's (shorter) environment.
* the only structural difference is the `env0` binding.  Where `main_agree`
  binds it and then passes `.ref rfl`, here `mkSources`'s derivation is handed
  DIRECTLY to `evalNodes_agree` as its environment argument.
* removing a binding changes the environment for everything after it, but
  `slookup` is by NAME and `env0` is not shadowed, so every later `.ref rfl`
  still computes.  That is why no "lookup irrelevance" lemma is needed: the
  derivations are rebuilt at the new sigma rather than transported across it.
* the whole body is main-free, so it is built at `hwS` and moved to `hwSVarT`
  ONCE, by B1.  B1 is never applied to a `call "main"` node -- that node is
  what the rewrite changes, and `SEval_call4` builds it here from `hwSVarT`'s
  own `sFn` equation.

No operator or graph adequacy is reproved: `applyOp`, `evalNodes`, `mkOutputs`
and `flopNexts` arrive as the existing lemmas.
-/
import LeanSemanticPrimitives.Projection.Proto.RewriteTotal
import LeanSemanticPrimitives.Projection.HardwareAdequacy
import LeanSemanticPrimitives.Projection.ProjectionCorrect
import LeanSemanticPrimitives.Projection.ProjectedStep

namespace Projection
namespace ProtoVar

open Projection.Surface Projection.Hw Compiler Projection.Acceptance

/-- The variant's `main`, as `hwSVarT` actually holds it. -/
def mainFunVarT : Option SFun := sFn hwSVarT "main"

def mainBodyVarT : SExp := (mainFunVarT.map SFun.body).getD (.lit .nil)

/-- The `sFn` equation, by kernel reduction. -/
theorem mainFunVarT_params : (mainFunVarT.map (fun f => f.params.length)) = some 4 := by rfl

/-- The rewritten body calls no `main`, so B1 moves it in one step. -/
theorem mainBodyVarT_mainFree : noMainCallB mainBodyVarT = true := by rfl

/-! ## The witness

Mirrors `main_agree` step for step.  The ONE difference is marked. -/

theorem main_agree_varT {D : DesignCert} {e : ClockEdges} {i : RuntimeInput}
    {s : RuntimeState} (hsup : SupportedByProjection D) (hrs : RuntimeSized D e i s) :
    SEval hwSVarT []
      (.call "main" [.lit (encDesign D), .lit (encEdges e), .lit (encInput i),
                     .lit (encState s)])
      (encResult (interpretDesign D e i s)) := by
  have hlenVA : (prefixVals D i s (D.sources.size + D.nodes.size)).length
      = D.sources.size + D.nodes.size := prefixVals_length _ _ _ _
  have hsv : SlotVals (prefixVals D i s (D.sources.size + D.nodes.size)) (slotVal D i s) :=
    prefixVals_slotVals _ _ _ _
  have hout : (interpretDesign D e i s).outputs.toList
      = D.outputs.toList.map (fun o => bv_resize o.width (slotVal D i s o.slot)) := by
    simp [interpretDesign, slotVal]
  have hflops : (interpretDesign D e i s).nextState.flops.toList
      = flopNextsFrom (evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D i s))
          e s 0 D.flops.toList := by
    rw [flopNextsFrom_mapIdx]
    simp [interpretDesign]
  have hbout : ∀ o ∈ D.outputs.toList,
      o.slot < (prefixVals D i s (D.sources.size + D.nodes.size)).length := by
    intro o ho
    rw [hlenVA]
    exact hsup.wf.slotsInRange.1 o ho
  have hbflop : ∀ f ∈ D.flops.toList,
      f.clock < e.size ∧ f.din < (prefixVals D i s (D.sources.size + D.nodes.size)).length ∧
      (∀ x, f.enable = some x → x < (prefixVals D i s (D.sources.size + D.nodes.size)).length) ∧
      (∀ r, f.resetPin = some r →
        r < (prefixVals D i s (D.sources.size + D.nodes.size)).length) := by
    intro f hf
    have hfl : f ∈ D.flops := by simpa using hf
    obtain ⟨hd, he, hr⟩ := hsup.wf.slotsInRange.2.1 f hf
    rw [hlenVA]
    exact ⟨Nat.lt_of_lt_of_le (hsup.flopClocks f hfl) hrs.edges, hd, he, hr⟩
  have hfsize : 0 + D.flops.toList.length ≤ s.flops.size := by
    simpa using hrs.flopsSized
  -- the top `call` is built at `hwSVarT`, from ITS OWN `sFn` equation.  B1 is
  -- NOT applied here: this node is exactly what the rewrite changed.
  refine SEval_call4 .lit .lit .lit .lit (by rfl) (by rfl) ?_
  -- the body is main-free, so B1 moves the WHOLE of it in one step
  refine SEval_hwSVarT_of_hwS (by rfl) ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval.letN (lenL_agree D.sources.toList _ _ (.ref rfl)) ?_
  refine SEval.letN (lenL_agree D.nodes.toList _ _ (.ref rfl)) ?_
  have hcast : Int.ofNat D.sources.size + Int.ofNat D.nodes.size
      = Int.ofNat (D.sources.size + D.nodes.size) := by
    simp only [Int.ofNat_eq_natCast]; omega
  refine SEval.letN
    (SEval_prim2 (a := Val.int (Int.ofNat D.sources.size))
      (b := Val.int (Int.ofNat D.nodes.size))
      (v := Val.int (Int.ofNat (D.sources.size + D.nodes.size)))
      (.ref rfl) (.ref rfl) (by simp only [evalPrim]; rw [hcast])) ?_
  -- ===================== THE ONE DIFFERENCE =====================
  -- `main_agree` binds `env0` here and then passes `.ref rfl` below.  The
  -- rewrite removed that binding, so `mkSources`'s derivation is handed
  -- DIRECTLY to `evalNodes_agree` as its environment argument.
  refine SEval.letN
    (evalNodes_agree hsup.wf hsup.ops hsup.arities D.nodes.size 0 (by omega) _ _ _ _
      (.ref rfl)
      (by
        show SEval hwS _ _ (objEnv (prefixVals D i s (D.sources.size + 0)))
        rw [Nat.add_zero, prefixVals_sources D i s]
        exact mkSources_agree D.sources.toList [] _ _ _ _ _
          (fun sd hsd => hsup.sources sd hsd)
          (fun sd hsd => hrs.inputs sd hsd)
          (fun sd hsd => hrs.stateReads sd hsd)
          (.ref rfl) (.ref rfl) (.ref rfl) .lit)
      (.ref rfl)) ?_
  -- ==============================================================
  refine SEval.letN (mkOutputs_agree hsv D.outputs.toList _ _ _ _ hbout (.ref rfl)
    (.ref rfl) (by rw [hlenVA]; exact .ref rfl)) ?_
  refine SEval.letN (flopNexts_agree hsv D.flops.toList 0 _ _ _ _ _ _ _ hbflop hfsize
    (.ref rfl) (.ref rfl) (.ref rfl) (by rw [hlenVA]; exact .ref rfl) (.ref rfl) .lit) ?_
  have hres : encResult (interpretDesign D e i s)
      = Val.ctor tagResult
          [Val.ctor tagState
            [encListG encBV (flopNextsFrom
              (evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D i s)) e s 0
              D.flops.toList)],
           encListG encBV
             (D.outputs.toList.map (fun o => bv_resize o.width (slotVal D i s o.slot)))] := by
    simp only [encResult, encState, encBVs, encArr, hout, hflops]
  rw [hres]
  exact SEval.mk (.cons (SEval.mk (.cons (.ref rfl) .nil)) (.cons (.ref rfl) .nil))

/-! ## From the witness to adequacy

Exactly `IHwAdequate_proved`'s pattern (`HardwareAdequacy.lean:1879`): ONE
evaluation, with `Eval_det` giving the forward direction and
`ResultRel_canonical` the converse.  No completeness theorem is involved. -/

set_option maxRecDepth 100000 in
theorem hwResolvedVarT_isOk : Option.isSome (Except.toOption hwResolvedVarT) = true := rfl

theorem hwResolvedVarT_ok : hwResolvedVarT = .ok (hwPVarT, hwInlVarT) := by
  have h0 := hwResolvedVarT_isOk
  unfold hwPVarT hwInlVarT
  cases h : hwResolvedVarT with
  | ok pi   => cases pi; rfl
  | error e => rw [h] at h0; simp [Except.toOption] at h0

theorem hwPVarT_resolves : resolveProgram hwSVarT = .ok (hwPVarT, hwInlVarT) :=
  hwResolvedVarT_ok

theorem hwSVarT_entry : hwSVarT.entry = "main" := by rfl

theorem hw_entry_varT {vs : List Val} {v : Val}
    (h : SEval hwSVarT [] (.call "main" (vs.map SExp.lit)) v) :
    Eval hwPVarT [] (.call hwPVarT.entry (vs.map Term.lit)) v :=
  SEval_entry hwPVarT_resolves (by rw [hwSVarT_entry]; exact h)

/-- Adequacy of the TOTAL variant interpreter: the same `iff`
`IHwAdequate_proved` states for `hwP`, for `hwPVarT`. -/
theorem IHwAdequate_varT {D : DesignCert} {e : ClockEdges} {i : RuntimeInput}
    {s : RuntimeState} (hsup : SupportedByProjection D) (hrs : RuntimeSized D e i s) :
    ∀ r : Val,
      Eval hwPVarT []
        (.call hwPVarT.entry
          [.lit (encDesign D), .lit (encEdges e), .lit (encInput i), .lit (encState s)]) r
      ↔ ResultRel r (interpretDesign D e i s) := by
  have hmf : MemFree (interpretDesign D e i s).nextState :=
    interpretDesign_memFree hsup.memFree e i s
  have hobj : Eval hwPVarT []
      (.call hwPVarT.entry [.lit (encDesign D), .lit (encEdges e), .lit (encInput i),
                            .lit (encState s)])
      (encResult (interpretDesign D e i s)) :=
    hw_entry_varT (vs := [encDesign D, encEdges e, encInput i, encState s])
      (main_agree_varT hsup hrs)
  intro r
  constructor
  · intro hr
    have : r = encResult (interpretDesign D e i s) := Surface.Eval_det hr hobj
    rw [this]
    exact ResultRel_encResult hmf
  · intro hr
    rw [ResultRel_canonical hr]
    exact hobj

/-! ## B3's prerequisites, as THEOREMS

`RewriteTotal.lean` carries these as `#guard`s and says they are checks.  B3
consumes them, and a check may not be cited as a discharged premise, so they
are promoted here by kernel reduction. -/

set_option maxRecDepth 100000 in
theorem hwAVarT_isOk : Option.isSome (Except.toOption hwAVarT) = true := rfl

theorem hwAVarT_ok : hwAVarT = .ok hwAPVarT := by
  have h0 := hwAVarT_isOk
  unfold hwAPVarT
  cases h : hwAVarT with
  | ok a    => rfl
  | error e => rw [h] at h0; simp [Except.toOption] at h0

set_option maxRecDepth 100000 in
theorem hwAPVarT_wf : wfAProgram hwAPVarT = true := rfl

/-- Not a kernel reduction: `bta` guarantees erasure for every successful run,
and `hwAVarT_ok` instantiates it -- the same route `hwAP_erases` takes. -/
theorem hwAPVarT_erases : eraseProgram hwAPVarT = hwPVarT := bta_erases hwAVarT_ok

set_option maxRecDepth 100000 in
theorem hwAPVarT_entry_params :
    (hwAPVarT.fn hwAPVarT.entry).map AFunDef.params = some [.stat, .dyn, .dyn, .dyn] := rfl

/-- The entry fact B3 needs, in the shape `specializeDesign_correct` consumes. -/
theorem hwAPVarT_entry :
    ∃ afd, hwAPVarT.fn hwAPVarT.entry = some afd ∧
           afd.params = [.stat, .dyn, .dyn, .dyn] := by
  have h := hwAPVarT_entry_params
  cases hfn : hwAPVarT.fn hwAPVarT.entry with
  | none     => rw [hfn] at h; simp at h
  | some afd =>
      rw [hfn] at h
      simp only [Option.map_some, Option.some.injEq] at h
      exact ⟨afd, rfl, h⟩

/-! ## B3: the residual theorem for the total variant

`specialize_correct_of` (`ProjectionCorrect.lean`) is the composition with the
interpreter as a PARAMETER.  `specializeDesign_correct` is its `hwAP` instance;
this is its `hwAPVarT` instance.  The source/dynamic split is the generic
lemma's, so the certificate stays the single static argument and
`[edges, input, state]` the three dynamic ones, in that order.

Correctness is CONDITIONAL ON SUCCESS AT A BUDGET, exactly as before: `sf`/`wf`
enter only through `hproj`. -/

theorem specializeDesign_varT_correct {sf wf : Nat} {D : DesignCert} {R : Program}
    (hproj : mixDriver sf wf hwAPVarT [encDesign D] = .ok R)
    (hsup : SupportedByProjection D)
    {e : ClockEdges} {i : RuntimeInput} {s : RuntimeState}
    (hwf : Compiler.RuntimeWF D i s) (hrs : RuntimeSized D e i s) (r : Val) :
    Eval R [] (.call R.entry
        [.lit (encEdges e), .lit (encInput i), .lit (encState s)]) r
      ↔ ResultRel r (interpretDesign D e i s) :=
  specialize_correct_of hwAPVarT_entry hwAPVarT_erases
    (fun D e i s hsup _hwf hrs r => IHwAdequate_varT hsup hrs r)
    hproj hsup hwf hrs r

/-! ## A sequential fixture, instantiated at the total variant

`seqD` carries a flop, so this exercises the flop-commit path rather than only
the combinational one.  Stated over every stimulus, not a chosen one. -/

theorem seq_cycle_varT (d en rst q : Int) (r : Val) :
    Eval hwPVarT []
      (.call hwPVarT.entry
        [.lit (encDesign seqD), .lit (encEdges (allEdges seqD)),
         .lit (encInput (seqIn d en rst)), .lit (encState (seqSt q))]) r
      ↔ ResultRel r
          (interpretDesign seqD (allEdges seqD) (seqIn d en rst) (seqSt q)) :=
  IHwAdequate_varT SupportCheck.seq_supported
    (GuardCorollary.seq_sized d en rst q) r

/-! ## The executable link, for the total variant

`runProjected` was already backend-independent.  `SimWF` was not -- its `proj`
field named `projectDesign`, which hardcodes `hwAP`.  `SimSound`
(`ResidualFragment.lean`) is that bundle with the HOW abstracted away, and the
old `SimWF` is an instance of it (`SimWF.toSimSound`), so nothing was
duplicated: `runProjected_correct`, `runProjected_success`, `stepOf_correct`,
`stepOf_succeeds` and `stepTrace_projected` are the SAME theorems, now stated
over the contract.

This is the other instance. -/

/-- A checked residual from the PROVED specializer and the TOTAL variant.
Budget-parametric: `sf`/`wf` enter only through `hproj`, so correctness is
conditional on success at whatever budget was used. -/
theorem simSound_varT {sf wf : Nat} {D : DesignCert} {R : Program} {b : Nat}
    (hproj : mixDriver sf wf hwAPVarT [encDesign D] = .ok R)
    (hchk : checkResidual R = some b)
    (hsup : SupportedByProjection D) :
    SimSound ⟨D, R, b⟩ where
  chk   := hchk
  sup   := hsup
  agree := fun hwf hrs r => specializeDesign_varT_correct hproj hsup hwf hrs r

/-! ### A total-variant PROJECTED sequential fixture

`seq_cycle_varT` above checks `Eval hwPVarT` -- the INTERPRETER.  These check a
SPECIALIZED RESIDUAL, through `stepOf`, which is a different claim.

`seqD` has a flop with both an enable and a reset, so the three transitions are
reachable from the stimulus alone:

    seqIn d 1 1   reset asserted  (reset beats enable)
    seqIn d 1 0   enabled         (the flop takes `din`)
    seqIn d 0 0   held            (enable low: the flop keeps `q`)

Both halves are covered: every successful result is the reference one
(`stepOf_correct` via `stepTrace_projected`), AND a valid run DOES succeed at
the checker bound (`stepOf_succeeds`). -/

theorem seq_varT_step {sf wf : Nat} {R : Program} {b : Nat}
    (hproj : mixDriver sf wf hwAPVarT [encDesign seqD] = .ok R)
    (hchk : checkResidual R = some b) (d en rst q : Int) :
    stepOf ⟨seqD, R, b⟩ seqD (allEdges seqD) (seqIn d en rst) (seqSt q)
      = .ok (interpretDesign seqD (allEdges seqD) (seqIn d en rst) (seqSt q)) :=
  stepOf_succeeds (simSound_varT hproj hchk SupportCheck.seq_supported)
    (show runtimeOK (ProjectedSimulator.mk seqD R b).design
            (allEdges seqD) (seqIn d en rst) (seqSt q) = true from
      FixtureStep.seq_runtimeOK d en rst q)

/-- The three transitions, named, as instances of the above. -/
theorem seq_varT_reset {sf wf : Nat} {R : Program} {b : Nat}
    (hproj : mixDriver sf wf hwAPVarT [encDesign seqD] = .ok R)
    (hchk : checkResidual R = some b) (d q : Int) :
    stepOf ⟨seqD, R, b⟩ seqD (allEdges seqD) (seqIn d 1 1) (seqSt q)
      = .ok (interpretDesign seqD (allEdges seqD) (seqIn d 1 1) (seqSt q)) :=
  seq_varT_step hproj hchk d 1 1 q

theorem seq_varT_enabled {sf wf : Nat} {R : Program} {b : Nat}
    (hproj : mixDriver sf wf hwAPVarT [encDesign seqD] = .ok R)
    (hchk : checkResidual R = some b) (d q : Int) :
    stepOf ⟨seqD, R, b⟩ seqD (allEdges seqD) (seqIn d 1 0) (seqSt q)
      = .ok (interpretDesign seqD (allEdges seqD) (seqIn d 1 0) (seqSt q)) :=
  seq_varT_step hproj hchk d 1 0 q

theorem seq_varT_held {sf wf : Nat} {R : Program} {b : Nat}
    (hproj : mixDriver sf wf hwAPVarT [encDesign seqD] = .ok R)
    (hchk : checkResidual R = some b) (d q : Int) :
    stepOf ⟨seqD, R, b⟩ seqD (allEdges seqD) (seqIn d 0 0) (seqSt q)
      = .ok (interpretDesign seqD (allEdges seqD) (seqIn d 0 0) (seqSt q)) :=
  seq_varT_step hproj hchk d 0 0 q

/-- A THREE-cycle trace -- reset, then enable, then hold -- each cycle starting
from the previous one's `nextState`, which no single-cycle theorem says. -/
theorem seq_varT_trace {sf wf : Nat} {R : Program} {b : Nat}
    (hproj : mixDriver sf wf hwAPVarT [encDesign seqD] = .ok R)
    (hchk : checkResidual R = some b) (q : Int) (rs : List RuntimeResult)
    (h : stepTrace (stepOf ⟨seqD, R, b⟩) seqD (seqSt q)
           [(allEdges seqD, seqIn 5 1 1), (allEdges seqD, seqIn 7 1 0),
            (allEdges seqD, seqIn 9 0 0)] = .ok rs) :
    rs = refTrace seqD (seqSt q)
           [(allEdges seqD, seqIn 5 1 1), (allEdges seqD, seqIn 7 1 0),
            (allEdges seqD, seqIn 9 0 0)] :=
  stepTrace_projected (simSound_varT hproj hchk SupportCheck.seq_supported)
    (seqSt q) _ rs h

end ProtoVar
end Projection
