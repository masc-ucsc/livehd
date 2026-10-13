/-
# Binding-time analysis and specialization for the MEMORY entry

Stage 1: the annotated program.
-/
import LeanSemanticPrimitives.Projection.ResidualFragment
import LeanSemanticPrimitives.Projection.MemSupport

namespace Projection
namespace MemProjection

open Surface Compiler Projection.Hw Projection.HwMem

/-- The memory route's BTA: the DESIGN is static, and exactly the edge vector,
the input vector and the state are dynamic. -/
def hwMemA : Except BTAError AProgram :=
  bta hwMemP hwMemInl [.stat, .dyn, .dyn, .dyn] 200

def hwMemAP : AProgram := match hwMemA with | .ok a => a | .error _ => ⟨[], 0⟩

#guard hwMemA.toOption.isSome
#guard wfAProgram hwMemAP
#guard eraseProgram hwMemAP == hwMemP
#guard (hwMemAP.fn hwMemAP.entry).map AFunDef.params == some [.stat, .dyn, .dyn, .dyn]

/-! ## Stage 2: specialization, and the arity-3 residual -/

def projectMemDesign (D : DesignCert) : Except MixError Program :=
  mixDriver 20000 200 hwMemAP [encDesign D]

def mixedR : Program :=
  match projectMemDesign mixedDesign with | .ok p => p | .error _ => ⟨[], 0⟩

#guard (projectMemDesign mixedDesign).toOption.isSome
#guard mixedR.funs.map FunDef.arity == [3]
#guard (checkResidual mixedR).isSome

/-! ## Stage 3: the proved specialization

Two kernel reductions, as in `ProjectionCorrect`; the memory program has ten
more functions than the scalar one, so both need the raised budget. -/

set_option maxHeartbeats 4000000 in
set_option maxRecDepth 1000000 in
theorem hwMemA_isOk : Option.isSome (Except.toOption hwMemA) = true := rfl

set_option maxHeartbeats 4000000 in
set_option maxRecDepth 1000000 in
theorem hwMemAP_entry_params :
    (hwMemAP.fn hwMemAP.entry).map AFunDef.params = some [.stat, .dyn, .dyn, .dyn] := rfl

theorem hwMemA_ok : hwMemA = .ok hwMemAP := by
  have h0 := hwMemA_isOk
  unfold hwMemAP
  cases h : hwMemA with
  | ok a    => rfl
  | error z => rw [h] at h0; simp [Except.toOption] at h0

/-- **Annotation erasure**, as a theorem: the annotated program is the memory
interpreter with annotations ON it, not a different program. -/
theorem hwMemAP_erases : eraseProgram hwMemAP = hwMemP := bta_erases hwMemA_ok

theorem hwMemAP_wf : wfAProgram hwMemAP = true := bta_sound hwMemA_ok

theorem hwMemAP_entry :
    ∃ afd, hwMemAP.fn hwMemAP.entry = some afd
         ∧ afd.params = [.stat, .dyn, .dyn, .dyn] := by
  have h := hwMemAP_entry_params
  cases hfn : hwMemAP.fn hwMemAP.entry with
  | none     => rw [hfn] at h; simp at h
  | some afd =>
      rw [hfn] at h
      simp only [Option.map_some, Option.some.injEq] at h
      exact ⟨afd, rfl, h⟩

/-! ### The specializer, with the four argument VALUES left abstract

`Hw.specialize_correct_of` bakes in `encState`/`ResultRel`, which are the
memory-free codecs; this is the same proof with the static value, the three
dynamic values and the answer left as `Val`s, so the memory codecs fit.  The
source/dynamic SPLIT is preserved exactly: one static argument and three
dynamic ones, in that order, which `hentry`'s `[.stat, .dyn, .dyn, .dyn]`
pins. -/

theorem specializeFour_correct_of {A : AProgram} {P : Program}
    {sd d1 d2 d3 w : Val}
    (hentry : ∃ afd, A.fn A.entry = some afd ∧ afd.params = [.stat, .dyn, .dyn, .dyn])
    (herase : eraseProgram A = P)
    (hadq : ∀ v : Val,
      Eval P [] (.call P.entry ([sd, d1, d2, d3].map Term.lit)) v ↔ v = w)
    {sf wf : Nat} {R : Program} (hproj : mixDriver sf wf A [sd] = .ok R) (v : Val) :
    Eval R [] (.call R.entry ([d1, d2, d3].map Term.lit)) v ↔ v = w := by
  obtain ⟨afd, hafd, hparams⟩ := hentry
  obtain ⟨fd, hpf, hfa⟩ := mixDriver_entry hproj hafd
  have hdyn : dynCount afd.params = 3 := by rw [hparams]; rfl
  have hcallR := Eval_entry (P := R) (fd := fd) (args := [d1, d2, d3]) (v := v) hpf
    (by rw [hfa, hdyn]; rfl)
  have hsa : srcArgs afd.params [sd] [d1, d2, d3] = some [sd, d1, d2, d3] := by
    rw [hparams]; simp [srcArgs]
  have hmix := mixDriver_iff hproj [d1, d2, d3] [sd, d1, d2, d3] v fd afd hpf hafd hsa
  rw [herase] at hmix
  have hent : P.entry = A.entry := by rw [← herase]; simp only [eraseProgram]
  have hmfd : P.fn P.entry = some (eraseFunDef afd) := by
    have h := eraseProgram_fn hafd
    rw [herase] at h
    rw [hent]; exact h
  have hcallI := Eval_entry (P := P) (fd := eraseFunDef afd)
    (args := [sd, d1, d2, d3]) (v := v) hmfd (by simp [eraseFunDef, hparams])
  refine Iff.trans ?_ (hadq v)
  refine Iff.trans hcallR.symm ?_
  refine Iff.trans hmix ?_
  simpa [eraseFunDef] using hcallI

/-! ### The memory entry's answer is UNIQUE

`admits_run_full` gives one value; `Eval_det` makes it the only one, which is
what turns the existence statement into the `iff` the specializer consumes. -/

theorem memEntry_unique {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} (hA : admitsB D = true)
    (hR : runtimeFullOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateFullOKB D r.nextState = true
      ∧ ∀ v : Val,
          Eval hwMemP []
            (.call hwMemP.entry
              ([encDesign D, encEdges e, encInput i,
                StateM.encStateM st].map Term.lit)) v
          ↔ v = StateM.encResultM r := by
  obtain ⟨r, hev, hden, hok⟩ := admits_run_full hA hR
  exact ⟨r, hden, hok, fun v => ⟨fun hv => Eval_det hv hev, fun hv => hv ▸ hev⟩⟩

/-- **The first Futamura projection, on the MEMORY entry.**  Specializing the
memory interpreter to a design — the design STATIC, and exactly the edge
vector, the input vector and the state DYNAMIC — yields a residual that, run on
those three arguments, computes exactly what the shared reference semantics
computes for that cycle, as an `iff` over `Eval` with no fuel in the
conclusion. -/
theorem projectMemDesign_correct {D : DesignCert} {R : Program}
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hproj : projectMemDesign D = .ok R)
    (hA : admitsB D = true) (hR : runtimeFullOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateFullOKB D r.nextState = true
      ∧ ∀ v : Val,
          Eval R [] (.call R.entry
            ([encEdges e, encInput i, StateM.encStateM st].map Term.lit)) v
          ↔ v = StateM.encResultM r := by
  obtain ⟨r, hden, hok, hiff⟩ := memEntry_unique hA hR
  exact ⟨r, hden, hok,
    fun v => specializeFour_correct_of hwMemAP_entry hwMemAP_erases hiff hproj v⟩

/-! ## Stage 4: the bundle, checked at its returned bound -/

structure MemSimulator where
  /-- the design this residual was specialized FOR -/
  design : DesignCert
  /-- the residual itself -/
  prog   : Program
  /-- the bound `checkResidual` computed for it -/
  bound  : Nat
deriving Inhabited

/-- The bundle is honest: the residual really is this design's memory-route
projection, it passed the SAME production residual checker with this bound, and
the design is admitted by the memory route. -/
structure MemSimWF (sim : MemSimulator) : Prop where
  proj  : projectMemDesign sim.design = .ok sim.prog
  chk   : checkResidual sim.prog = some sim.bound
  admit : admitsB sim.design = true

/-- Specialize once, check once. -/
def mkMemSim (D : DesignCert) : Option MemSimulator :=
  if admitsB D then
    match projectMemDesign D with
    | .error _ => none
    | .ok R    =>
        match checkResidual R with
        | none   => none
        | some b => some ⟨D, R, b⟩
  else none

theorem mkMemSim_wf {D : DesignCert} {sim : MemSimulator}
    (h : mkMemSim D = some sim) : MemSimWF sim := by
  unfold mkMemSim at h
  split at h
  · rename_i hadm
    split at h <;> try simp at h
    rename_i R hR
    split at h <;> try simp at h
    rename_i b hb
    cases h
    exact ⟨hR, hb, hadm⟩
  · simp at h

/-- One cycle, through the residual, at the bound the checker returned. -/
def runMemSim (sim : MemSimulator) (e : ClockEdges) (i : RuntimeInput)
    (st : StateM) : Option StateM.ResultM :=
  match evalFuel sim.bound sim.prog []
      (.call sim.prog.entry
        [.lit (encEdges e), .lit (encInput i), .lit (StateM.encStateM st)]) with
  | .value v => StateM.decResultM v
  | _        => none

/-- **At the returned bound.**  For an honest bundle on a state, input and edge
vector the runtime policy accepts, the residual's run TERMINATES within
`sim.bound` — `.outOfFuel` is unreachable — decodes through the real result
codec, agrees with the shared reference semantics, and leaves a state that
passes the same policy, so the next cycle needs no new state check. -/
theorem runMemSim_correct {sim : MemSimulator} (hw : MemSimWF sim)
    {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hR : runtimeFullOKB sim.design i st e = true) :
    ∃ r : StateM.ResultM,
      runMemSim sim e i st = some r
      ∧ r.denote = Compiler.interpretDesign sim.design e i (StateM.denote st)
      ∧ stateFullOKB sim.design r.nextState = true := by
  obtain ⟨r, hden, hok, hiff⟩ := projectMemDesign_correct hw.proj hw.admit hR
  have hev : Eval sim.prog [] (.call sim.prog.entry
      [.lit (encEdges e), .lit (encInput i), .lit (StateM.encStateM st)])
      (StateM.encResultM r) := (hiff _).mpr rfl
  have hfuel := checkResidual_complete hw.chk hev
  refine ⟨r, ?_, hden, hok⟩
  unfold runMemSim
  rw [hfuel]
  exact StateM.decResultM_encResultM r

/-! ## Stage 5: executed, with genuine memory transitions

`mixedDesign` reads the mutable table at the address on `input 0`, ANDs the
word with `input 3`, and writes the result back when the byte-enable mask is
non-zero.  At `initState` word `k` holds `k + 1`, so address 20 holds 21; ANDed
with `0xF` that is 5, and the enabled write stores 5 — a real transition, taken
through the RESIDUAL, at the bound the checker returned. -/

def mixedSim : Option MemSimulator := mkMemSim mixedDesign

private def mIn    : RuntimeInput := #[mk_bv 5 20, mk_bv 32 0xFFFFFFFF, mk_bv 1 1, mk_bv 32 0xF]
private def mInOff : RuntimeInput := #[mk_bv 5 20, mk_bv 32 0,          mk_bv 1 1, mk_bv 32 0xF]
private def mE     : ClockEdges   := #[true]
private def mQuiet : ClockEdges   := #[false]

#guard mixedSim.isSome
#guard runtimeFullOKB mixedDesign mIn initState mE == true

/- the word really changes: 21 -> 5 -/
#guard (match mixedSim with
        | some sim =>
            (match runMemSim sim mE mIn initState with
             | some r => (r.nextState.mems[0]!).get 20 == mk_bv 32 5
                         && (initState.mems[0]!).get 20 == mk_bv 32 21
             | none   => false)
        | none => false)

/- a zero byte-enable leaves it EXACTLY alone, on the same firing edge -/
#guard (match mixedSim with
        | some sim =>
            (match runMemSim sim mE mInOff initState with
             | some r => r.nextState.mems[0]! == initImg
             | none   => false)
        | none => false)

/- …and so does a QUIET clock, with the write enabled -/
#guard (match mixedSim with
        | some sim =>
            (match runMemSim sim mQuiet mIn initState with
             | some r => r.nextState.mems[0]! == initImg
             | none   => false)
        | none => false)

/- TWO cycles through the residual: the second reads what the first wrote
   (5), ANDs it with 0xF and stores 5 again, and the returned state still
   passes the strengthened policy -/
#guard (match mixedSim with
        | some sim =>
            (match runMemSim sim mE mIn initState with
             | some r1 =>
                 stateFullOKB mixedDesign r1.nextState
                 && (match runMemSim sim mE mIn r1.nextState with
                     | some r2 => (r2.nextState.mems[0]!).get 20 == mk_bv 32 5
                                  && stateFullOKB mixedDesign r2.nextState
                     | none    => false)
             | none    => false)
        | none => false)

/- the residual really is arity 3, and the design really is gone from it -/
#guard (match mixedSim with
        | some sim => sim.prog.funs.map FunDef.arity == [3]
        | none     => false)

#print axioms hwMemAP_erases
#print axioms specializeFour_correct_of
#print axioms memEntry_unique
#print axioms projectMemDesign_correct
#print axioms runMemSim_correct

end MemProjection
end Projection
