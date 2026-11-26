/-
# The `fp` route's encoded engine

`fpStep` (`Run/Backend`) is the `fp` route on runtime states: every step converts the state to the
residual's (`Ckpt.save`, `CkptStateM.toStateM`), checks the whole runtime policy, encodes it, runs
the residual, decodes the whole result and turns it back into a runtime state. A run pays all of
that every cycle. `fpEncEngine` keeps the residual's own state value between steps, as
`dcert2_memroute`'s fast loop does (`Projection/MemRunFast`):

* the state stays encoded: a step's result value holds, as its state part, the encoding of the
  next state (`next_from_parts`, from the codec's canonicity), so the next step runs on it and
  only the outputs are decoded;
* the policy check is its size half: every step's result passes the state half
  (`runMemSimWF_correct` gives `stateFullOKB`), so the check is the input, flop-bound and edge
  checks (`runtimeFullOKB_after`), which depend only on sizes; the engine computes them once for
  the sizes a driver uses (`maxInput D` inputs, one edge per clock) and checks a state that is
  not known to pass (a loaded one that does not) in full;
* the runtime state is built only when a driver asks for it (`view`: checkpoints, compaction,
  lockstep).

`fpEncEngine_agree`: the engine agrees with `interpretDesign` (`EngineAgree`). A step is exactly
`interpretDesign`'s step on the state its value stands for; only `load` goes through a checkpoint,
so a loaded state agrees with the one given below every memory's depth, as `fpStep`'s does.

Mathlib-free.
-/
import HDLProof.Run.Sound
import LeanSemanticPrimitives.Projection.MemRunFast

namespace HDLProof
namespace Run

open Compiler Projection Projection.MemProjection Projection.HwMem
open Projection.CkptResume (StateAgree WordsFit restore_save_agree wordsFit_step wordsFit_restore
  mems_size_step)

/-- The `fp` route's encoded state: the residual's state value (`StateM.encStateM` of a state), and
whether that state is known to pass the state half of the runtime check (`stateFullOKB`). -/
structure FpEncState where
  sv : Val
  ok : Bool

/-- The policy check of a step: its size half when the state is known to pass the state half
(`q.ok`) and the flop bound holds at the design's flop count (`fbOK`), with the input and edge
halves taken from `inOK` / `eOK` when the sizes are the ones they were computed for; else the whole
check on the decoded state. -/
def fpEncPass (D : DesignCert) (inN : Nat) (inOK : Bool) (eN : Nat) (eOK fbOK : Bool)
    (e : ClockEdges) (i : RuntimeInput) (q : FpEncState) : Bool :=
  if q.ok && fbOK then
    (if i.size = inN then inOK else inputOKB D i.size) && (if e.size = eN then eOK else edgesOKB D e)
  else
    match StateM.decStateM q.sv with
    | some st => runtimeFullOKB D i st e
    | none => false

/-- One step on the encoded state: the residual on the value, the next state's value kept, the
outputs decoded. -/
def fpEncCore (sim : MemSimulator) (sh : Option (Program × Nat)) (e : ClockEdges) (i : RuntimeInput)
    (q : FpEncState) : Except (String × FpEncState) (Array BV × FpEncState) :=
  match ShareTails.stepEnc sim sh e i q.sv with
  | none => .error ("fp: the residual gave no value", q)
  | some v =>
    match MemRunFast.resultParts v with
    | none => .error ("fp: the residual gave no value", q)
    | some (sp, op) =>
      match decBVs op with
      | none => .error ("fp: the residual gave no value", q)
      | some os => .ok (os, { sv := sp, ok := true })

/-- The `fp` engine on the residual's own state (module header). -/
def fpEncEngine (D : DesignCert) (sim : MemSimulator) (sh : Option (Program × Nat)) :
    Engine FpEncState :=
  -- the input and edge halves of the check depend on sizes only: once, for the sizes drivers use
  let inN := maxInput D
  let inOK := inputOKB D inN
  let eN := D.clocks.size
  let eOK := edgesOKB D (Array.replicate eN true)
  let fbOK := flopBoundOKB D D.flops.size
  { name := "fp"
    load := fun s =>
      let st := CkptStateM.toStateM (Ckpt.save D 0 0 #[] s)
      { sv := StateM.encStateM st, ok := stateFullOKB D st }
    step := fun e i q =>
      if fpEncPass D inN inOK eN eOK fbOK e i q then fpEncCore sim sh e i q
      else .error ("fp: the runtime policy refuses the state, the input or the edge vector", q)
    view := fun q =>
      match StateM.decStateM q.sv with
      | some st => st.denote
      | none => { flops := #[], mems := #[] } }

/-- The flop-bound check only asks that each source's flop index be below `n`. -/
theorem flopBoundOKB_mono {D : DesignCert} {n m : Nat} (h : flopBoundOKB D n = true) (hle : n ≤ m) :
    flopBoundOKB D m = true := by
  unfold flopBoundOKB at h ⊢
  rw [List.all_eq_true] at h ⊢
  intro sd hsd
  have h1 := h sd hsd
  rw [decide_eq_true_iff] at h1 ⊢
  cases sd <;> first | exact Nat.lt_of_lt_of_le h1 hle | trivial

/-- A state that passes the state half has at least the design's flops. -/
theorem flops_le_of_stateFull {D : DesignCert} {st : StateM} (h : stateFullOKB D st = true) :
    D.flops.size ≤ st.flops.size := by
  unfold stateFullOKB stateOKB at h
  simp only [Bool.and_eq_true, decide_eq_true_eq] at h
  exact h.1.1

/-- The encoded engine's invariant: its value encodes a state, which passes the state half of the
check when the flag says so. -/
def FpEncInv (D : DesignCert) (q : FpEncState) : Prop :=
  ∃ st, q.sv = StateM.encStateM st ∧ (q.ok = true → stateFullOKB D st = true)

theorem fpEnc_view {D : DesignCert} {sim : MemSimulator} {sh : Option (Program × Nat)}
    {q : FpEncState} {st : StateM} (h : q.sv = StateM.encStateM st) :
    (fpEncEngine D sim sh).view q = st.denote := by
  show (match StateM.decStateM q.sv with | some st => st.denote | none => _) = _
  rw [h, StateM.decStateM_encStateM]

/-- The state a load builds stands for the restored checkpoint of the state given. -/
theorem fpEnc_load_view {D : DesignCert} {sim : MemSimulator} {sh : Option (Program × Nat)}
    (hdepth : MemAddr.depthError D = none) (s : RuntimeState) :
    (fpEncEngine D sim sh).view ((fpEncEngine D sim sh).load s) = Ckpt.restore (Ckpt.save D 0 0 #[] s) := by
  rw [fpEnc_view (st := CkptStateM.toStateM (Ckpt.save D 0 0 #[] s)) rfl]
  exact CkptStateM.toStateM_denote _ (save_depths hdepth 0 0 #[] s)

/-- **The encoded `fp` engine agrees with `interpretDesign`**, for the residual of `D` (fused,
plain writes admitted), with or without its tails shared. -/
theorem fpEncEngine_agree {D : DesignCert} {R : Program} {sf wf : Nat} {sim : MemSimulator}
    {sh : Option (Program × Nat)}
    (hsem : Direct.DesignSemWF D) (hdepth : MemAddr.depthError D = none)
    (hroot : Ckpt.imgRootError D = none) (hword : Ckpt.wordError D = none)
    (hproj : MemFused.projectMemFDesignAt sf wf D = .ok R) (hsim : mkMemSimOfW D R = some sim)
    (hsh : sh = ShareTails.shareSim sim ∨ sh = none) :
    EngineAgree D (fpEncEngine D sim sh) (FpEncInv D) := by
  obtain ⟨hd, hp, hchk0, -⟩ := mkMemSimOfW_parts hsim
  have hchk : Hw.checkResidual sim.prog = some sim.bound := by rw [hp]; exact hchk0
  have hw : MemFused.MemSimWFAtWF sf wf sim := MemFused.mkMemSimOfW_wfF hproj hsim
  -- the load, through a checkpoint
  have hload : ∀ s, Fits D s → FpEncInv D ((fpEncEngine D sim sh).load s) ∧
      Fits D ((fpEncEngine D sim sh).view ((fpEncEngine D sim sh).load s)) ∧
      StateAgree D ((fpEncEngine D sim sh).view ((fpEncEngine D sim sh).load s)) s := by
    intro s hfit
    rw [fpEnc_load_view hdepth s]
    refine ⟨⟨CkptStateM.toStateM (Ckpt.save D 0 0 #[] s), rfl, fun h => h⟩, ⟨?_, ?_⟩,
      restore_save_agree 0 0 #[] hfit.1 hfit.2⟩
    · simp [Ckpt.restore, Ckpt.save]
    · exact wordsFit_restore (Ckpt.validate_save D 0 0 #[] s hroot hword)
  refine ⟨hload, fun _ s _ hs => hload s hs, ?_, fun pk hpk => by cases hpk⟩
  -- a step
  intro q hq hfit e i o q' hs
  obtain ⟨st, hsv, hok⟩ := hq
  have hview : (fpEncEngine D sim sh).view q = st.denote := fpEnc_view hsv
  -- the policy passed
  have hpass : fpEncPass D (maxInput D) (inputOKB D (maxInput D)) D.clocks.size
      (edgesOKB D (Array.replicate D.clocks.size true)) (flopBoundOKB D D.flops.size) e i q = true →
      runtimeFullOKB D i st e = true := by
    intro hpb
    unfold fpEncPass at hpb
    by_cases hf : (q.ok && flopBoundOKB D D.flops.size) = true
    · rw [ite_eq_left hf] at hpb
      simp only [Bool.and_eq_true] at hf
      have hfull := hok hf.1
      rw [MemRunFast.runtimeFullOKB_after hfull]
      have hfb := flopBoundOKB_mono hf.2 (flops_le_of_stateFull hfull)
      have hin : (if i.size = maxInput D then inputOKB D (maxInput D) else inputOKB D i.size)
          = inputOKB D i.size := by
        split
        · rename_i h; rw [h]
        · rfl
      have hed : (if e.size = D.clocks.size then edgesOKB D (Array.replicate D.clocks.size true)
            else edgesOKB D e) = edgesOKB D e := by
        split
        · rename_i h; exact MemRunFast.edgesOKB_size D (by simp [h])
        · rfl
      rw [hin, hed] at hpb
      simp only [Bool.and_eq_true] at hpb ⊢
      exact ⟨⟨hpb.1, hfb⟩, hpb.2⟩
    · rw [ite_eq_right hf, hsv, StateM.decStateM_encStateM] at hpb
      exact hpb
  -- the step
  have hs' : (if fpEncPass D (maxInput D) (inputOKB D (maxInput D)) D.clocks.size
        (edgesOKB D (Array.replicate D.clocks.size true)) (flopBoundOKB D D.flops.size) e i q
      then fpEncCore sim sh e i q
      else .error ("fp: the runtime policy refuses the state, the input or the edge vector", q))
      = .ok (o, q') := hs
  split at hs'
  · rename_i hpb
    have hR := hpass hpb
    have hR' : runtimeFullOKB sim.design i st e = true := by rw [hd]; exact hR
    obtain ⟨r, hrun, hden, hfullr⟩ := MemFused.runMemSimWF_correct hw hR'
    have henc : ShareTails.stepEnc sim sh e i q.sv = MemRunFast.runMemSimEnc sim e i q.sv := by
      rcases hsh with rfl | rfl
      · exact ShareTails.stepEnc_eq hchk e i q.sv
      · rfl
    unfold fpEncCore at hs'
    rw [henc] at hs'
    cases hv : MemRunFast.runMemSimEnc sim e i q.sv with
    | none => rw [hv] at hs'; cases hs'
    | some v =>
      rw [hv] at hs'
      dsimp only at hs'
      cases hparts : MemRunFast.resultParts v with
      | none => rw [hparts] at hs'; cases hs'
      | some pr =>
        obtain ⟨sp, op⟩ := pr
        rw [hparts] at hs'
        dsimp only at hs'
        cases hos : decBVs op with
        | none => rw [hos] at hs'; cases hs'
        | some os =>
          rw [hos] at hs'
          simp only [Except.ok.injEq, Prod.mk.injEq] at hs'
          obtain ⟨rfl, rfl⟩ := hs'
          rw [hsv] at hv
          obtain ⟨hsp, hop⟩ := MemRunFast.next_from_parts hv hparts hrun
          rw [hos, Option.some.injEq] at hop
          subst hop
          have hview' : (fpEncEngine D sim sh).view { sv := sp, ok := true } = r.nextState.denote :=
            fpEnc_view hsp
          have hr : interpretDesign D e i st.denote = r.denote := by rw [hden, hd]
          refine ⟨⟨r.nextState, hsp, fun _ => by rw [← hd]; exact hfullr⟩, ?_⟩
          rw [hview, hview']
          rw [hview] at hfit
          refine ⟨?_, ?_, ?_⟩
          · show r.outputs = (interpretDesign D e i st.denote).outputs
            rw [hr]; rfl
          · show StateAgree D r.nextState.denote (interpretDesign D e i st.denote).nextState
            rw [hr]; exact StateAgree.refl' D _
          · show Fits D r.nextState.denote
            have hn : r.nextState.denote = (interpretDesign D e i st.denote).nextState := by
              rw [hr]; rfl
            rw [hn]
            exact ⟨mems_size_step e, wordsFit_step hsem hroot hword hfit.2 e⟩
  · cases hs'

end Run
end HDLProof

#audit_axioms HDLProof.Run.fpEncEngine_agree
