/-
# `projectDesign_correct` — the first Futamura projection, on hardware

Phase 4 item 3.  `mixDriver_iff` says the residual computes what the ERASED
annotated interpreter computes on the reassembled argument list;
`IHwAdequacyGoal_proved` says that interpreter computes `interpretDesign`.  This
file is the composition, and it is the first statement on this branch that
relates a RESIDUAL PROGRAM to the shared reference semantics.

What the statement does and does not carry:

  * it quantifies over ANY `R` with `projectDesign D = .ok R`.  Nothing here is
    about a particular design, a fixture, or a `#guard`.
  * the specialization fuel appears ONLY inside that success equation.  The
    semantic conclusion is an `iff` over `Eval` with no fuel in it, on either
    side.
  * `secondProjection_correct` is not used.  That is a different theorem about
    a different projection and would import a second, unneeded assumption.

The three facts about `hwAP` that are KERNEL REDUCTIONS live here, isolated and
measured, for the same reason `hwResolved_ok` lives in `HardwareAdequacy.lean`:
so the cost is paid once and shows up in the per-module timing.
-/

import LeanSemanticPrimitives.Projection.HardwareAdequacy
import LeanSemanticPrimitives.Projection.PartialEvaluatorCorrect

namespace Projection
namespace Hw

open Surface Compiler

/-! ## The residual entry keeps exactly the dynamic parameters

`mixFun` emits `⟨dynCount fd.params, …⟩`, and `SpecOK` records that; what is
missing is the step from "the driver succeeded" to "the ENTRY is one of those".
`indexOfReq_spec` supplies it: the entry index really does name the entry
request. -/

theorem mixDriver_entry {sf wf : Nat} {A : AProgram} {statics : List Val}
    {Pr : Program} (h : mixDriver sf wf A statics = .ok Pr)
    {afd : AFunDef} (haf : A.fn A.entry = some afd) :
    ∃ fd, Pr.fn Pr.entry = some fd ∧ fd.arity = dynCount afd.params := by
  simp only [mixDriver] at h
  split at h <;> try contradiction
  rename_i reqs _
  split at h <;> try contradiction
  rename_i funs hgen
  split at h <;> try contradiction
  rename_i ent hidx
  cases h
  have hreq : reqs[ent]? = some ⟨A.entry, statics⟩ := indexOfReq_spec hidx
  obtain ⟨fd, afd', hfd, hafd, harity, -⟩ :=
    specOK_all A ⟨funs, ent⟩ reqs sf (by simpa [generate] using hgen) 0 ent
      ⟨A.entry, statics⟩ hreq
  rw [haf] at hafd
  cases hafd
  exact ⟨fd, hfd, harity⟩

/-! ## The annotated interpreter, concretely

Two kernel reductions, and nothing else in this file reduces anything.  Both
need a raised heartbeat budget; at the default the elaborator reports a bare
"not definitionally equal", which reads like the equation being false rather
than like a budget.  Measured on this machine: about 9 s for `hwA_isOk` and
about 10 s for `hwAP_entry_params`. -/

set_option maxHeartbeats 4000000 in
set_option maxRecDepth 1000000 in
theorem hwA_isOk : Option.isSome (Except.toOption hwA) = true := rfl

set_option maxHeartbeats 4000000 in
set_option maxRecDepth 1000000 in
theorem hwAP_entry_params :
    (hwAP.fn hwAP.entry).map AFunDef.params = some [.stat, .dyn, .dyn, .dyn] := rfl

theorem hwA_ok : hwA = .ok hwAP := by
  have h0 := hwA_isOk
  unfold hwAP
  cases h : hwA with
  | ok a    => rfl
  | error z => rw [h] at h0; simp [Except.toOption] at h0

/-- The annotated interpreter erases to the interpreter.  Not a check: `bta`
guarantees it for every successful run, and `hwA_ok` is what instantiates it. -/
theorem hwAP_erases : eraseProgram hwAP = hwP := bta_erases hwA_ok

theorem hwAP_entry :
    ∃ afd, hwAP.fn hwAP.entry = some afd ∧ afd.params = [.stat, .dyn, .dyn, .dyn] := by
  have h := hwAP_entry_params
  cases hfn : hwAP.fn hwAP.entry with
  | none      => rw [hfn] at h; simp at h
  | some afd  =>
      rw [hfn] at h
      simp only [Option.map_some, Option.some.injEq] at h
      exact ⟨afd, rfl, h⟩

/-! ## The theorem -/

/-- The composition, with the INTERPRETER as a parameter.

The previous version inlined this at `hwAP`, which made it fuel-parametric but
NOT interpreter-parametric: the hypothesis named `hwAP` and the proof consumed
`hwAP_entry`, `hwAP_erases` and `IHwAdequacyGoal_proved`.  Nothing in the body
needs those to be about `hwAP` -- it uses only `mixDriver_entry`,
`mixDriver_iff`, `Eval_entry` and `eraseProgram_fn` -- so the three interpreter
facts are premises here and the body is otherwise unchanged.

The source/dynamic argument SPLIT is preserved exactly: the certificate is the
single static argument and `[edges, input, state]` the three dynamic ones, in
that order, which is what `hentry`'s `[.stat, .dyn, .dyn, .dyn]` pins. -/
theorem specialize_correct_of {A : AProgram} {P : Program}
    (hentry : ∃ afd, A.fn A.entry = some afd ∧ afd.params = [.stat, .dyn, .dyn, .dyn])
    (herase : eraseProgram A = P)
    (hadq : ∀ (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState),
              SupportedByProjection D → Compiler.RuntimeWF D i s → RuntimeSized D e i s →
              ∀ r : Val,
                Eval P [] (.call P.entry
                  [.lit (encDesign D), .lit (encEdges e), .lit (encInput i),
                   .lit (encState s)]) r
                ↔ ResultRel r (interpretDesign D e i s))
    {sf wf : Nat} {D : DesignCert} {R : Program}
    (hproj : mixDriver sf wf A [encDesign D] = .ok R)
    (hsup : SupportedByProjection D)
    {e : ClockEdges} {i : RuntimeInput} {s : RuntimeState}
    (hwf : Compiler.RuntimeWF D i s) (hrs : RuntimeSized D e i s) (r : Val) :
    Eval R [] (.call R.entry
        [.lit (encEdges e), .lit (encInput i), .lit (encState s)]) r
      ↔ ResultRel r (interpretDesign D e i s) := by
  obtain ⟨afd, hafd, hparams⟩ := hentry
  obtain ⟨fd, hpf, hfa⟩ := mixDriver_entry hproj hafd
  have hdyn : dynCount afd.params = 3 := by rw [hparams]; rfl
  -- the residual: body form and call form are the same thing
  have hcallR := Eval_entry (P := R) (fd := fd)
    (args := [encEdges e, encInput i, encState s]) (v := r) hpf (by rw [hfa, hdyn]; rfl)
  -- the specializer
  have hsa : srcArgs afd.params [encDesign D] [encEdges e, encInput i, encState s]
      = some [encDesign D, encEdges e, encInput i, encState s] := by
    rw [hparams]; simp [srcArgs]
  have hmix := mixDriver_iff hproj [encEdges e, encInput i, encState s]
    [encDesign D, encEdges e, encInput i, encState s] r fd afd hpf hafd hsa
  rw [herase] at hmix
  -- the interpreter: body form and call form are the same thing
  have hent : P.entry = A.entry := by
    rw [← herase]; simp only [eraseProgram]
  have hmfd : P.fn P.entry = some (eraseFunDef afd) := by
    have h := eraseProgram_fn hafd
    rw [herase] at h
    rw [hent]; exact h
  have hcallI := Eval_entry (P := P) (fd := eraseFunDef afd)
    (args := [encDesign D, encEdges e, encInput i, encState s]) (v := r) hmfd
    (by simp [eraseFunDef, hparams])
  -- and adequacy
  have hadq' := hadq D e i s hsup hwf hrs r
  refine Iff.trans ?_ hadq'
  refine Iff.trans hcallR.symm ?_
  refine Iff.trans hmix ?_
  simpa [eraseFunDef] using hcallI

/-- **The first Futamura projection, on hardware.**

Specializing `I_hw` to a design yields a residual program that, run on the
three dynamic arguments of one cycle, computes exactly what the shared
reference semantics computes for that cycle — as an `iff`, over `Eval`, with no
fuel in the conclusion.

The specialization fuel enters only through `projectDesign D = .ok R`.

Now an INSTANCE of `specialize_correct_of` at `A := hwAP`.  The statement is
unchanged; what changed is that the interpreter is no longer baked into the
proof. -/
theorem specializeDesign_correct {sf wf : Nat} {D : DesignCert} {R : Program}
    (hproj : mixDriver sf wf hwAP [encDesign D] = .ok R)
    (hsup : SupportedByProjection D)
    {e : ClockEdges} {i : RuntimeInput} {s : RuntimeState}
    (hwf : Compiler.RuntimeWF D i s) (hrs : RuntimeSized D e i s) (r : Val) :
    Eval R [] (.call R.entry
        [.lit (encEdges e), .lit (encInput i), .lit (encState s)]) r
      ↔ ResultRel r (interpretDesign D e i s) :=
  specialize_correct_of hwAP_entry hwAP_erases
    (fun D e i s hsup hwf hrs r => IHwAdequacyGoal_proved D e i s hsup hwf hrs r)
    hproj hsup hwf hrs r

/-- The fuel `projectDesign` happens to fix.  It appears ONLY here, in the
hypothesis; the conclusion is the same `iff`. -/
theorem projectDesign_correct {D : DesignCert} {R : Program}
    (hproj : projectDesign D = .ok R)
    (hsup : SupportedByProjection D)
    {e : ClockEdges} {i : RuntimeInput} {s : RuntimeState}
    (hwf : Compiler.RuntimeWF D i s) (hrs : RuntimeSized D e i s) (r : Val) :
    Eval R [] (.call R.entry
        [.lit (encEdges e), .lit (encInput i), .lit (encState s)]) r
      ↔ ResultRel r (interpretDesign D e i s) :=
  specializeDesign_correct hproj hsup hwf hrs r

/-- The body form, for a caller that already has the residual's entry function
in hand.  Same content; `Eval_entry` is the only difference. -/
theorem projectDesign_correct_body {D : DesignCert} {R : Program} {fd : FunDef}
    (hproj : projectDesign D = .ok R) (hpf : R.fn R.entry = some fd)
    (hsup : SupportedByProjection D)
    {e : ClockEdges} {i : RuntimeInput} {s : RuntimeState}
    (hwf : Compiler.RuntimeWF D i s) (hrs : RuntimeSized D e i s) (r : Val) :
    Eval R [encEdges e, encInput i, encState s] fd.body r
      ↔ ResultRel r (interpretDesign D e i s) := by
  obtain ⟨afd, hafd, hparams⟩ := hwAP_entry
  obtain ⟨fd', hpf', hfa⟩ := mixDriver_entry hproj hafd
  rw [hpf] at hpf'
  cases hpf'
  have hdyn : dynCount afd.params = 3 := by rw [hparams]; rfl
  exact Iff.trans
    (Eval_entry (P := R) (fd := fd) (args := [encEdges e, encInput i, encState s])
      (v := r) hpf (by rw [hfa, hdyn]; rfl))
    (projectDesign_correct hproj hsup hwf hrs r)

/-! ## The acceptance `#guard`s, as theorems

Phase 4 item 4.  The guards in `HardwareInterpreter.lean` stay where they are
and keep running as regressions -- they catch a definitional change faster than
a proof does, and at zero proof cost.  What follows is the same content as
THEOREMS, and strictly stronger in three ways: quantified over every result
value, in both directions, and with no fuel.

Representative cases only: the combinational fixture, and the sequential one in
its three regimes (capture, hold, reset). -/

namespace GuardCorollary
open Compiler Projection.Acceptance

theorem tiny_wf : Compiler.RuntimeWF tinyD tinyIn tinySt := ⟨rfl, rfl⟩
theorem seq_wf (d en rst q : Int) :
    Compiler.RuntimeWF seqD (seqIn d en rst) (seqSt q) := ⟨rfl, rfl⟩

theorem tiny_sized : RuntimeSized tinyD (allEdges tinyD) tinyIn tinySt where
  edges      := RuntimeSized_allEdges_edges
  inputs     := by decide
  stateReads := by decide
  flopsSized := by decide

theorem seq_sized (d en rst q : Int) :
    RuntimeSized seqD (allEdges seqD) (seqIn d en rst) (seqSt q) where
  edges      := RuntimeSized_allEdges_edges
  -- the three sizes are literals: an array's size does not depend on its
  -- elements, so the bounds hold at every stimulus
  inputs     := by rw [show (seqIn d en rst).size = 3 from rfl]; decide
  stateReads := by rw [show (seqSt q).flops.size = 1 from rfl]; decide
  flopsSized := by rw [show (seqSt q).flops.size = 1 from rfl]; decide

/-- `#guard runHw tinyD … == refOf tinyD …`, as a theorem. -/
theorem tiny_cycle (r : Val) :
    Eval hwP []
      (.call hwP.entry [.lit (encDesign tinyD), .lit (encEdges (allEdges tinyD)),
                        .lit (encInput tinyIn), .lit (encState tinySt)]) r
      ↔ ResultRel r (interpretDesign tinyD (allEdges tinyD) tinyIn tinySt) :=
  IHwAdequate_proved SupportCheck.tiny_supported tiny_sized r

/-- …and the direction the guard actually checks: the interpreter DOES produce
the reference answer, not merely nothing else. -/
theorem tiny_cycle_value :
    Eval hwP []
      (.call hwP.entry [.lit (encDesign tinyD), .lit (encEdges (allEdges tinyD)),
                        .lit (encInput tinyIn), .lit (encState tinySt)])
      (encResult (interpretDesign tinyD (allEdges tinyD) tinyIn tinySt)) :=
  (tiny_cycle _).mpr (ResultRel_encResult (interpretDesign_memFree rfl _ _ _))

/-- The sequential fixture, in all three regimes the guards exercise:
`(5,1,0)` captures, `(3,0,0)` holds the old state, `(3,1,1)` resets -- and
reset beats enable. -/
theorem seq_cycle (d en rst q : Int) (r : Val) :
    Eval hwP []
      (.call hwP.entry [.lit (encDesign seqD), .lit (encEdges (allEdges seqD)),
                        .lit (encInput (seqIn d en rst)), .lit (encState (seqSt q))]) r
      ↔ ResultRel r (interpretDesign seqD (allEdges seqD) (seqIn d en rst) (seqSt q)) :=
  IHwAdequate_proved SupportCheck.seq_supported (seq_sized d en rst q) r

theorem seq_cycle_value (d en rst q : Int) :
    Eval hwP []
      (.call hwP.entry [.lit (encDesign seqD), .lit (encEdges (allEdges seqD)),
                        .lit (encInput (seqIn d en rst)), .lit (encState (seqSt q))])
      (encResult (interpretDesign seqD (allEdges seqD) (seqIn d en rst) (seqSt q))) :=
  (seq_cycle d en rst q _).mpr
    (ResultRel_encResult (interpretDesign_memFree rfl _ _ _))

/-! ### Why the RESIDUAL-level guards stay guards

`Acceptance`'s other block checks the residual rather than the interpreter --
`runR tinyR …`, `runR seqR …`.  Those are NOT converted here, and the reason is
measured rather than assumed.

Instantiating `projectDesign_correct` at a concrete design needs
`projectDesign tinyD = .ok tinyR` as a KERNEL equation, and the specializer
does not reduce in the kernel:

    (mixDriver 0 0 hwAP [encDesign tinyD]).toOption.isSome = false   `rfl`, 0.2 s
    (mixDriver 50 5 hwAP [encDesign tinyD]).toOption.isSome = true   `rfl` FAILS
    (projectDesign tinyD).toOption.isSome = true                     `rfl` FAILS

all three with `maxHeartbeats 0` and `maxRecDepth 4000000`, and all three
reported as "not definitionally equal" rather than as a budget.  Fuel 0
short-circuits and reduces fine, and the evaluator answers `true` at every fuel
from (50, 5) upward, so the blockage is inside the specializer's reduction --
not the fuel magnitude, and not `hwAP`, which `hwAP_entry_params` above does
reduce.

`#guard` passes because it runs the COMPILED evaluator.  Closing the gap needs
either a kernel-reducible specializer path or `native_decide`, and
`native_decide` is excluded on this branch by the axiom audit.  Same class as
Phase 6's rule that certificates must not be Lean literals.  Recorded, not
worked around. -/

end GuardCorollary

end Hw
end Projection
