/-
# The INCREMENTAL proof walk, validated on a diamond

`prove_reified` hands the whole binding chain to one `simp`.  `Reify.lean` was
written for the other approach -- advance one binding at a time, keeping the
environment a single opaque variable -- and this file is the evidence that the
approach actually closes, end to end, with those lemmas.

The design is a DIAMOND rather than a chain, deliberately:

  n0  reads two SOURCES
  n1  reads n0
  n2  reads n0 AGAIN            -- the re-read a chain would never exercise
  n3  reads n1 AND source 0     -- the FAR source read, three pushes down,
                                   which is the case `srcAgree_push` exists for

Each step is O(1): the binding's value is discharged against the CURRENT
environment, the three facts (size, source agreement, live binding reads) are
re-established across the push, and `generalize` then throws the term away. No
`simp` in this file is ever shown the whole chain.

The staging in `dia_fast_eq_compileAndRun` is load-bearing and was found the
hard way: unfolding the certificate too early re-forms `compileDesign dia` and
undoes the `.ok` witness, and rewriting `diaR.sources` too early stops the
walk's conclusion from matching, because the walk names it.

Run: lake env lean probes/d3_incremental_walk_test.lean
-/
import LeanSemanticPrimitives.Compiler.ReifyProof
import LeanSemanticPrimitives.Compiler.Reify
set_option maxRecDepth 4000000
set_option maxHeartbeats 0
open Compiler

def dia : DesignCert :=
  { sources  := #[ SourceDesc.input 0 8, SourceDesc.const 8 (-1) ]
    nodes    := #[ { op := LGraphOp.Op_And, width := 8, deps := #[0, 1], origin := 0 }
                 , { op := LGraphOp.Op_Not, width := 8, deps := #[2],    origin := 1 }
                 , { op := LGraphOp.Op_Ror, width := 1, deps := #[2],    origin := 2 }
                 , { op := LGraphOp.Op_And, width := 8, deps := #[3, 0], origin := 3 } ]
    outputs  := #[ { slot := 5, width := 8 } ]
    flops    := #[]
    memories := #[] }
def diaR : ResidualProgram := match compileDesign dia with | .ok R => R | .error _ => default

-- the four binding values, named so the script never re-spells them
abbrev V0 (e0 : SlotEnv) : BV := Residual.randV 8 [refBV e0 0, refBV e0 1]
abbrev V1 (e0 : SlotEnv) : BV := Residual.rnotV 8 (V0 e0)
abbrev V2 (e0 : SlotEnv) : BV := Residual.rredOrV 1 [V0 e0]
abbrev V3 (e0 : SlotEnv) : BV := Residual.randV 8 [V1 e0, refBV e0 0]

/-- The incremental walk, by hand: four steps, each O(1), with every
intermediate environment generalized away.  Source agreement travels by
`srcAgree_push` and live binding reads by `bindAgree_push`. -/
theorem dia_walk (i : RuntimeInput) (st : RuntimeState) :
    ∃ e : SlotEnv,
      runBindings diaR.bindings.toList (sourceEnvArr diaR.sources i st) = e
      ∧ refBV e 5 = V3 (sourceEnvArr diaR.sources i st) := by
  have hbs : diaR.bindings.toList =
      [ { ty := .bv 8, rhs := .rand 8 #[0,1] }, { ty := .bv 8, rhs := .rnot 8 2 }
      , { ty := .bv 1, rhs := .rredOr 1 #[2] }, { ty := .bv 8, rhs := .rand 8 #[3,0] } ] := rfl
  have hsrcs : diaR.sources = dia.sources := rfl
  rw [hbs, hsrcs]
  set e0 : SlotEnv := sourceEnvArr dia.sources i st with he0
  have hsz0 : e0.size = 2 := by simp [he0, sourceEnvArr, dia]
  have hag0 : ∀ j, j < e0.size → refBV e0 j = refBV e0 j := fun _ _ => rfl

  -- STEP 0 : reads sources 0 and 1
  have hv0 : denoteExpr e0 (.rand 8 #[0,1]) = CertVal.bv (V0 e0) := by
    simp [denoteExpr, refBVs, V0]
  rw [runBindings_step _ _ e0 _ hv0]
  have hsz1 : (e0.push (CertVal.bv (V0 e0))).size = 3 := by simp [hsz0]
  have hag1 := srcAgree_push (base := e0) (CertVal.bv (V0 e0)) (Nat.le_refl _) hag0
  have hb1 : refBV (e0.push (CertVal.bv (V0 e0))) 2 = V0 e0 := by
    have h := refBV_push_self e0 (CertVal.bv (V0 e0)); rw [hsz0] at h
    simpa [CertVal.asBV] using h
  generalize (e0.push (CertVal.bv (V0 e0))) = e1 at hsz1 hag1 hb1 ⊢

  -- STEP 1 : reads binding 0 (slot 2)
  have hv1 : denoteExpr e1 (.rnot 8 2) = CertVal.bv (V1 e0) := by
    simp only [denoteExpr, hb1, V1]
  rw [runBindings_step _ _ e1 _ hv1]
  have hsz2 : (e1.push (CertVal.bv (V1 e0))).size = 4 := by simp [hsz1]
  have hag2 := srcAgree_push (base := e0) (CertVal.bv (V1 e0))
    (by rw [hsz1, hsz0]; omega) hag1
  have hb2 : refBV (e1.push (CertVal.bv (V1 e0))) 2 = V0 e0 :=
    bindAgree_push _ 2 _ (by rw [hsz1]; omega) hb1
  have hb2' : refBV (e1.push (CertVal.bv (V1 e0))) 3 = V1 e0 := by
    have h := refBV_push_self e1 (CertVal.bv (V1 e0)); rw [hsz1] at h
    simpa [CertVal.asBV] using h
  generalize (e1.push (CertVal.bv (V1 e0))) = e2 at hsz2 hag2 hb2 hb2' ⊢

  -- STEP 2 : reads binding 0 again -- the DIAMOND re-read
  have hv2 : denoteExpr e2 (.rredOr 1 #[2]) = CertVal.bv (V2 e0) := by
    simp only [denoteExpr, refBVs, List.map_cons, List.map_nil, hb2, V2]
  rw [runBindings_step _ _ e2 _ hv2]
  have hsz3 : (e2.push (CertVal.bv (V2 e0))).size = 5 := by simp [hsz2]
  have hag3 := srcAgree_push (base := e0) (CertVal.bv (V2 e0))
    (by rw [hsz2, hsz0]; omega) hag2
  have hb3 : refBV (e2.push (CertVal.bv (V2 e0))) 3 = V1 e0 :=
    bindAgree_push _ 3 _ (by rw [hsz2]; omega) hb2'
  generalize (e2.push (CertVal.bv (V2 e0))) = e3 at hsz3 hag3 hb3 ⊢

  -- STEP 3 : reads binding 1 (slot 3) AND source 0 -- the FAR source read
  have hv3 : denoteExpr e3 (.rand 8 #[3,0]) = CertVal.bv (V3 e0) := by
    simp only [denoteExpr, refBVs, List.map_cons, List.map_nil, hb3,
               hag3 0 (by rw [hsz0]; omega), V3]
  rw [runBindings_step _ _ e3 _ hv3]
  have hsz4 : (e3.push (CertVal.bv (V3 e0))).size = 6 := by simp [hsz3]
  have hb4 : refBV (e3.push (CertVal.bv (V3 e0))) 5 = V3 e0 := by
    have h := refBV_push_self e3 (CertVal.bv (V3 e0)); rw [hsz3] at h
    simpa [CertVal.asBV] using h
  generalize (e3.push (CertVal.bv (V3 e0))) = e4 at hsz4 hb4 ⊢
  exact ⟨e4, by simp [runBindings], hb4⟩


reify_design dia as dia_fast

theorem dia_compilesOk : compilesOk dia = true := by rfl
theorem dia_hR : compileDesign dia = .ok diaR := compileDesign_ok_witness dia dia_compilesOk
theorem diaR_outputs : diaR.outputs = dia.outputs.map compileOutput := rfl
theorem diaR_flops : diaR.flopUpdates = dia.flops.map compileFlop := rfl
theorem diaR_mems : diaR.memoryUpdates = dia.memories.map compileMemory := rfl
theorem diaR_sources' : diaR.sources = dia.sources := rfl
theorem dia_outputs : dia.outputs = #[{ slot := 5, width := 8 }] := rfl
theorem dia_flops : dia.flops = #[] := rfl
theorem dia_mems : dia.memories = #[] := rfl
theorem dia_sources : dia.sources = #[SourceDesc.input 0 8, SourceDesc.const 8 (-1)] := rfl
-- the residual's own literals, via the projection lemmas plus one map evaluation
theorem diaR_outputs_lit : diaR.outputs = #[{ slot := 5, width := 8 }] := by
  simp [diaR_outputs, dia_outputs, compileOutput]
theorem diaR_flops_lit : diaR.flopUpdates = #[] := by simp [diaR_flops, dia_flops]
theorem diaR_mems_lit : diaR.memoryUpdates = #[] := by simp [diaR_mems, dia_mems]

/-- END TO END on the diamond, by the INCREMENTAL walk: no single `simp` is ever
handed the whole chain. -/
theorem dia_fast_eq_compileAndRun :
    forall i st, dia_fast i st = compileAndRun dia i st := by
  intro i st
  obtain ⟨eN, heN, hout⟩ := dia_walk i st
  -- STAGE 1 is MINIMAL on purpose: just enough to expose the environment term.
  -- Unfolding `dia` here would undo `dia_hR` by re-forming `compileDesign dia`,
  -- and rewriting `diaR.sources` would stop `heN` matching -- the walk names it.
  simp only [compileAndRun, dia_hR, denoteResidual]
  -- STAGE 2: the environment appears exactly as the walk states it.
  rw [heN]
  -- STAGE 3: now the output map can be evaluated and the walk's slot fact used.
  simp only [diaR_outputs_lit, diaR_flops_lit, diaR_mems_lit]
  simp only [show (Array.map (fun o => bv_resize o.width (refBV eN o.slot))
      #[({ slot := 5, width := 8 } : ResidualOutput)]) = #[bv_resize 8 (refBV eN 5)] from by
        simp]
  rw [hout]
  -- STAGE 4: and only now are the sources unfolded, on both sides at once.
  simp [diaR_sources', dia_sources, V3, V1, V0, refBV_sourceEnv, dia_fast, CertVal.asBV]

theorem dia_fast_correct :
    forall i st, dia_fast i st = interpretDesign dia i st := by
  intro i st
  rw [dia_fast_eq_compileAndRun i st]
  exact compileAndRun_correct dia dia_compilesOk i st

#print axioms dia_fast_eq_compileAndRun
#print axioms dia_fast_correct
#print axioms dia_walk
