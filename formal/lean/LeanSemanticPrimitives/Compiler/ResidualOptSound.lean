/-
# The optimizer frame is sound

`validate_sound`: when the checker accepts a certificate, the rebuilt program computes
exactly what the original does, on every step, from every state and input:
`denoteResidual (applyCert R c) e i s = denoteResidual R e i s`. So `optimize` -- the rebuild
when the checker accepts, the original otherwise -- never changes a result
(`optimize_correct`), and with `compileDesign_correct` (`CompileOpt.compileOpt_correct`) the
optimized program is `interpretDesign`.

The proof walks the old bindings once. After `k` old bindings (`t` of them kept) the new
environment holds, at `σ r`, what the old one holds at `r`, for every reference `r` the
checker accepts below slot `S + k` (`Inv.agree`). A kept binding's operands are such
references, so it computes the same value (`denoteExpr_rename`) and lands at `σ (S + k)`; a
dropped binding is never read. The terminals then read equal values
(`flopNext_rename`, `memNext_rename`).

Mathlib-free.
-/
import LeanSemanticPrimitives.Compiler.ResidualOpt

namespace Compiler
namespace ResidualOpt

open Residual

/-! ## Environment facts -/

theorem denoteRef_push_lt {env : SlotEnv} {v : CertVal} {r : Nat} (h : r < env.size) :
    denoteRef (env.push v) r = denoteRef env r := by
  simp [denoteRef, Array.getElem?_push_lt h, Array.getElem?_eq_getElem h]

theorem denoteRef_push_size {env : SlotEnv} {v : CertVal} :
    denoteRef (env.push v) env.size = v := by
  simp [denoteRef]

/-! ## Renaming preserves values -/

theorem refBVs_rename {env env' : SlotEnv} {σ : Nat → Nat} {args : Array Nat}
    (h : ∀ r ∈ args.toList, denoteRef env' (σ r) = denoteRef env r) :
    refBVs env' (args.map σ) = refBVs env args := by
  simp only [refBVs, Array.toList_map, List.map_map]
  apply List.map_congr_left
  intro r hr
  simp only [Function.comp, refBV, h r hr]

theorem denoteExpr_rename {env env' : SlotEnv} {σ : Nat → Nat} (e : ResidualExpr)
    (h : ∀ r ∈ exprRefs e, denoteRef env' (σ r) = denoteRef env r) :
    denoteExpr env' (renameExpr σ e) = denoteExpr env e := by
  cases e <;> simp only [exprRefs] at h <;> simp only [renameExpr, denoteExpr]
  all_goals first
    | rw [refBVs_rename h]
    | (simp only [List.mem_cons, List.not_mem_nil, or_false, forall_eq_or_imp, forall_eq] at h
       simp only [refBV, refMem, h])

theorem flopNext_rename {env env' : SlotEnv} {σ : Nat → Nat} {e : ClockEdges} {s : RuntimeState}
    {idx : Nat} (f : ResidualFlopUpdate)
    (h : ∀ r ∈ flopRefs f, denoteRef env' (σ r) = denoteRef env r) :
    flopNext env' e s idx (renameFlop σ f) = flopNext env e s idx f := by
  obtain ⟨w, din, en, rst, rv, al, ck, ar⟩ := f
  cases en <;> cases rst <;>
    simp_all [flopRefs, renameFlop, flopNext, refBV]

theorem inDom_renameMem (σ : Nat → Nat) (m : ResidualMemoryUpdate) :
    (renameMem σ m).toDesc.inDom = m.toDesc.inDom := by
  funext k
  simp [MemoryDesc.inDom, renameMem, ResidualMemoryUpdate.toDesc, renameCommit,
    Function.comp_def]

theorem domSpan_renameMem (σ : Nat → Nat) (m : ResidualMemoryUpdate) :
    (renameMem σ m).toDesc.domSpan = m.toDesc.domSpan := by
  show (m.commits.map (renameCommit σ)).foldl (fun a c => max a (c.mask.log2 + 1)) (m.clock + 1)
      = m.commits.foldl (fun a c => max a (c.mask.log2 + 1)) (m.clock + 1)
  rw [Array.foldl_map]
  rfl

theorem allFire_renameMem (σ : Nat → Nat) (m : ResidualMemoryUpdate) (e : ClockEdges) :
    (renameMem σ m).toDesc.allFire e = m.toDesc.allFire e := by
  unfold MemoryDesc.allFire
  rw [domSpan_renameMem, inDom_renameMem]

theorem firedIs_renameMem (σ : Nat → Nat) (m : ResidualMemoryUpdate) (e : ClockEdges) :
    (renameMem σ m).toDesc.firedIs e = m.toDesc.firedIs e := by
  funext c
  unfold MemoryDesc.firedIs
  rw [domSpan_renameMem, inDom_renameMem]

theorem memNext_rename {env env' : SlotEnv} {σ : Nat → Nat} {e : ClockEdges} {s : RuntimeState}
    {idx : Nat} (m : ResidualMemoryUpdate)
    (h : ∀ r ∈ memRefs m, denoteRef env' (σ r) = denoteRef env r) :
    memNext env' e s idx (renameMem σ m) = memNext env e s idx m := by
  have hnext : refMem env' (σ m.nextImg) = refMem env m.nextImg := by
    simp only [refMem, h m.nextImg (by simp [memRefs])]
  have hheld : memHeld env' s idx (renameMem σ m) = memHeld env s idx m := by
    unfold memHeld
    cases ha : m.asyncImg with
    | none => simp [renameMem, ha]
    | some a =>
      simp only [renameMem, ha, Option.map_some]
      simp only [refMem, h a (by simp [memRefs, ha])]
  have hpred : (fun c => (renameMem σ m).toDesc.firedIs e (renameCommit σ c))
      = m.toDesc.firedIs e := by
    funext c
    rw [firedIs_renameMem]
    rfl
  unfold memNext
  have hcomm : (renameMem σ m).commits = m.commits.map (renameCommit σ) := rfl
  have hempty : (m.commits.map (renameCommit σ)).isEmpty = m.commits.isEmpty := by
    simp only [Array.isEmpty, Array.size_map]
  have hck : (renameMem σ m).clock = m.clock := rfl
  have hnx : (renameMem σ m).nextImg = σ m.nextImg := rfl
  rw [allFire_renameMem, hcomm, hempty, Array.find?_map, hck, hnx]
  simp only [Function.comp_def, hpred]
  cases hE : m.commits.isEmpty <;> simp only [Bool.false_eq_true, eq_self, ite_false, ite_true]
  · cases hA : m.toDesc.allFire e <;> simp only [Bool.false_eq_true, eq_self, ite_false, ite_true]
    · cases hf : m.commits.find? (m.toDesc.firedIs e) with
      | none => simp only [Option.map_none]; exact hheld
      | some c =>
        simp only [Option.map_some, renameCommit]
        have hc : c ∈ m.commits := Array.mem_of_find?_eq_some hf
        simp only [refMem, h c.nextImg (by
          simp only [memRefs, List.mem_cons, List.mem_append, List.mem_map, Array.mem_toList_iff]
          exact Or.inr (Or.inl ⟨c, hc, rfl⟩))]
    · exact hnext
  · cases hF : fires e m.clock <;> simp only [Bool.false_eq_true, eq_self, ite_false, ite_true]
    · exact hheld
    · exact hnext

/-! ## The walk over the bindings -/

section
variable (R : ResidualProgram) (c : OptCert)

/-- After `k` old bindings, `t` of them kept. -/
structure Inv (k t : Nat) (env env' : SlotEnv) : Prop where
  size  : env.size = R.sources.size + k
  size' : env'.size = c.newSources.size + t
  agree : ∀ r, refOK R c (R.sources.size + k) r = true →
            c.σ r < env'.size ∧ denoteRef env' (c.σ r) = denoteRef env r

theorem refOK_lt {b r : Nat} (h : refOK R c b r = true) : r < R.sources.size ∨ r < b := by
  unfold refOK at h
  by_cases hr : r < R.sources.size
  · exact Or.inl hr
  · simp only [hr, if_false, Bool.and_eq_true, decide_eq_true_eq] at h
    exact Or.inr h.1

theorem refOK_step {k r : Nat} (h : refOK R c (R.sources.size + k + 1) r = true)
    (hne : r ≠ R.sources.size + k) : refOK R c (R.sources.size + k) r = true := by
  unfold refOK at h ⊢
  by_cases hr : r < R.sources.size
  · simpa [hr] using h
  · simp only [hr, if_false, Bool.and_eq_true, decide_eq_true_eq] at h ⊢
    exact ⟨by omega, h.2⟩

theorem refOK_new_false {k : Nat} (hk : c.keep.getD k false = false) :
    refOK R c (R.sources.size + k + 1) (R.sources.size + k) = false := by
  unfold refOK
  rw [if_neg (by omega)]
  simp [hk]

theorem inv_zero (i : RuntimeInput) (s : RuntimeState) :
    Inv R c 0 0 (sourceEnvArr R.sources i s) (sourceEnvArr c.newSources i s) where
  size  := by simp [sourceEnvArr_size]
  size' := by simp [sourceEnvArr_size]
  agree := by
    intro r h
    unfold refOK at h
    by_cases hr : r < R.sources.size
    · simp only [hr, if_true, beq_iff_eq] at h
      have hlt : c.σ r < c.newSources.size := by
        rcases Nat.lt_or_ge (c.σ r) c.newSources.size with h3 | h3
        · exact h3
        · rw [Array.getElem?_eq_none h3, Array.getElem?_eq_getElem hr] at h
          simp at h
      refine ⟨by simpa [sourceEnvArr_size] using hlt, ?_⟩
      simp only [denoteRef, sourceEnvArr, Array.getElem?_map, h]
    · simp only [hr, if_false, Bool.and_eq_true, decide_eq_true_eq] at h
      omega

theorem keep_head {k : Nat} {flag : Bool} {rest : List (ResidualBinding × Bool)}
    (hk : (flag :: rest.map Prod.snd) = c.keep.toList.drop k) :
    c.keep.getD k false = flag ∧ rest.map Prod.snd = c.keep.toList.drop (k + 1) := by
  constructor
  · have h1 := congrArg (fun l => l[0]?) hk
    simp only [List.getElem?_cons_zero, List.getElem?_drop, Nat.add_zero] at h1
    rw [Array.getD_eq_getD_getElem?, ← Array.getElem?_toList, ← h1]
    rfl
  · have h2 := congrArg List.tail hk
    simpa [List.tail_drop] using h2

theorem scan_sound : ∀ (pairs : List (ResidualBinding × Bool)) (k t : Nat) (env env' : SlotEnv),
    pairs.map Prod.snd = c.keep.toList.drop k →
    Inv R c k t env env' → scanOK R c pairs k t = true →
    ∃ t', Inv R c (k + pairs.length) t' (runBindings (pairs.map Prod.fst) env)
            (runBindings (keptRenamed c.σ pairs) env')
  | [], k, t, env, env', _, hinv, _ => ⟨t, by simpa [runBindings, keptRenamed] using hinv⟩
  | (b, true) :: rest, k, t, env, env', hk, hinv, hs => by
    simp only [scanOK, Bool.and_eq_true, beq_iff_eq, List.all_eq_true] at hs
    obtain ⟨⟨hrefs, hpos⟩, hrest⟩ := hs
    have hv : denoteExpr env' (renameExpr c.σ b.rhs) = denoteExpr env b.rhs :=
      denoteExpr_rename b.rhs (fun r hr => (hinv.agree r (hrefs r hr)).2)
    have hinv' : Inv R c (k + 1) (t + 1) (env.push (denoteExpr env b.rhs))
        (env'.push (denoteExpr env' (renameExpr c.σ b.rhs))) := {
      size  := by simp [hinv.size]; omega
      size' := by simp [hinv.size']; omega
      agree := by
        intro r hr
        by_cases hrk : r = R.sources.size + k
        · subst hrk
          rw [hpos]
          refine ⟨by simp [hinv.size'], ?_⟩
          rw [← hinv.size', ← hinv.size, denoteRef_push_size, denoteRef_push_size, hv]
        · have hr0 := refOK_step R c (by simpa [Nat.add_assoc] using hr) hrk
          obtain ⟨hlt, heq⟩ := hinv.agree r hr0
          have hrlt : r < env.size := by
            rcases refOK_lt R c hr0 with h | h <;> rw [hinv.size] <;> omega
          refine ⟨by simp; omega, ?_⟩
          rw [denoteRef_push_lt hlt, denoteRef_push_lt hrlt, heq] }
    have hk' := (keep_head c (by simpa using hk)).2
    obtain ⟨t', h⟩ := scan_sound rest (k + 1) (t + 1) _ _ hk' hinv' hrest
    refine ⟨t', ?_⟩
    have hlen : k + (rest.length + 1) = k + 1 + rest.length := by omega
    simpa [runBindings, keptRenamed, List.length_cons, hlen] using h
  | (b, false) :: rest, k, t, env, env', hk, hinv, hs => by
    simp only [scanOK] at hs
    have hkf := (keep_head c (by simpa using hk)).1
    have hk' := (keep_head c (by simpa using hk)).2
    have hinv' : Inv R c (k + 1) t (env.push (denoteExpr env b.rhs)) env' := {
      size  := by simp [hinv.size]; omega
      size' := hinv.size'
      agree := by
        intro r hr
        by_cases hrk : r = R.sources.size + k
        · subst hrk
          rw [← Nat.add_assoc, refOK_new_false R c hkf] at hr
          exact absurd hr (by decide)
        · have hr0 := refOK_step R c (by simpa [Nat.add_assoc] using hr) hrk
          obtain ⟨hlt, heq⟩ := hinv.agree r hr0
          have hrlt : r < env.size := by
            rcases refOK_lt R c hr0 with h | h <;> rw [hinv.size] <;> omega
          exact ⟨hlt, by rw [denoteRef_push_lt hrlt, heq]⟩ }
    obtain ⟨t', h⟩ := scan_sound rest (k + 1) t _ _ hk' hinv' hs
    refine ⟨t', ?_⟩
    have hlen : k + (rest.length + 1) = k + 1 + rest.length := by omega
    simpa [runBindings, keptRenamed, List.length_cons, hlen] using h

end

/-! ## The theorems -/

theorem mapIdx_map_congr {α β γ : Type} (a : Array α) (rn : α → β) (f : Nat → β → γ)
    (g : Nat → α → γ) (h : ∀ i (hi : i < a.size), f i (rn a[i]) = g i a[i]) :
    (a.map rn).mapIdx f = a.mapIdx g := by
  apply Array.ext
  · simp
  · intro i h1 h2
    simp only [Array.getElem_mapIdx, Array.getElem_map]
    exact h i (by simpa using h2)

/-- **The checker is sound.** -/
theorem validate_sound {R : ResidualProgram} {c : OptCert} (hv : validate R c = true)
    (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    denoteResidual (applyCert R c) e i s = denoteResidual R e i s := by
  simp only [validate, Bool.and_eq_true, beq_iff_eq] at hv
  obtain ⟨⟨⟨⟨hsize, hscan⟩, hout⟩, hflop⟩, hmem⟩ := hv
  have hlen : R.bindings.toList.length = c.keep.toList.length := by simp [hsize]
  obtain ⟨t', hinv⟩ := scan_sound R c (R.bindings.toList.zip c.keep.toList) 0 0 _ _
    (by simp [List.map_snd_zip, hlen]) (inv_zero R c i s) hscan
  rw [List.map_fst_zip (by omega)] at hinv
  have hB : 0 + (R.bindings.toList.zip c.keep.toList).length = R.bindings.size := by
    simp [hsize]
  rw [hB] at hinv
  -- every terminal reference reads the same value
  have agreeB : ∀ r, refOK R c (R.sources.size + R.bindings.size) r = true →
      denoteRef (runBindings (keptRenamed c.σ (R.bindings.toList.zip c.keep.toList))
          (sourceEnvArr c.newSources i s)) (c.σ r)
        = denoteRef (runBindings R.bindings.toList (sourceEnvArr R.sources i s)) r :=
    fun r hr => (hinv.agree r hr).2
  have hout' : (R.outputs.map (renameOutput c.σ)).map (fun o => bv_resize o.width (refBV
        (runBindings (keptRenamed c.σ (R.bindings.toList.zip c.keep.toList))
          (sourceEnvArr c.newSources i s)) o.slot))
      = R.outputs.map (fun o => bv_resize o.width (refBV
          (runBindings R.bindings.toList (sourceEnvArr R.sources i s)) o.slot)) := by
    rw [Array.map_map]
    apply Array.map_congr_left
    intro o ho
    simp only [Function.comp, renameOutput, refBV]
    rw [agreeB o.slot ((Array.all_eq_true'.mp hout) o ho)]
  have hfl' : (R.flopUpdates.map (renameFlop c.σ)).mapIdx (fun idx f => flopNext
        (runBindings (keptRenamed c.σ (R.bindings.toList.zip c.keep.toList))
          (sourceEnvArr c.newSources i s)) e s idx f)
      = R.flopUpdates.mapIdx (fun idx f => flopNext
          (runBindings R.bindings.toList (sourceEnvArr R.sources i s)) e s idx f) := by
    apply mapIdx_map_congr
    intro j hj
    apply flopNext_rename
    intro r hr
    exact agreeB r (List.all_eq_true.mp ((Array.all_eq_true.mp hflop) j hj) r hr)
  have hme' : (R.memoryUpdates.map (renameMem c.σ)).mapIdx (fun idx m => memNext
        (runBindings (keptRenamed c.σ (R.bindings.toList.zip c.keep.toList))
          (sourceEnvArr c.newSources i s)) e s idx m)
      = R.memoryUpdates.mapIdx (fun idx m => memNext
          (runBindings R.bindings.toList (sourceEnvArr R.sources i s)) e s idx m) := by
    apply mapIdx_map_congr
    intro j hj
    apply memNext_rename
    intro r hr
    exact agreeB r (List.all_eq_true.mp ((Array.all_eq_true.mp hmem) j hj) r hr)
  simp only [denoteResidual, applyCert, List.toList_toArray, hout', hfl', hme']

/-- **`optimize` never changes a result.** -/
theorem optimize_correct (R : ResidualProgram) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) : denoteResidual (optimize R) e i s = denoteResidual R e i s := by
  show denoteResidual (if validate R (optimizeRaw R) = true then (applyCert R (optimizeRaw R), true)
      else (R, false)).1 e i s = _
  by_cases hv : validate R (optimizeRaw R) = true
  · rw [if_pos hv]
    exact validate_sound hv e i s
  · rw [if_neg hv]

end ResidualOpt
end Compiler

#print axioms Compiler.ResidualOpt.denoteExpr_rename
#print axioms Compiler.ResidualOpt.validate_sound
#print axioms Compiler.ResidualOpt.optimize_correct
