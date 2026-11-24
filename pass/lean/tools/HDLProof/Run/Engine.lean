/-
# Knobs and `mkEngine`: one place that builds a route

A driver says which route it runs and how, as `Knobs`, and `mkEngine` builds the route's engine
(`Compiler.Engine σ`, `Compiler/Engine`): a step on a state of the route's own kind, so a route
keeps between steps whatever makes its next step fast.

`Knobs`' defaults are the fastest proved combination: vc, optimized, event-driven, memories as
tables. A driver that runs another route by default (`cert_run`, `cert_trace`: ir) sets its route
first and applies its options on top (`Knobs.apply?`). Every route option the drivers took
before maps onto a knob, and every combination they could run is still built:

| option      | knob                                        | drivers that took it         |
|-------------|---------------------------------------------|------------------------------|
| `route:R`   | `route` (ir, vc, ref, fp; `tv` = vc)        | cert_run, cert_trace         |
| `opt:off`   | `vcOpt := false` and `fpShare := false`     | cert_run, cert_trace         |
| `inc:off`   | `vcInc := .off` (the full step every time)  | cert_trace                   |
| `inc:scan`  | `vcInc := .scan` (unless `inc:off`)         | cert_trace                   |
| `mem:fun`   | `vcMem := .func` (memories as functions)    | cert_trace                   |
| `share:off` | `fpShare := false`                          | dcert2_memroute              |
| `ir:raw`    | `irStaged := false` (`directStepRaw`)       | new (the judge's step)       |
| `compact:K` | `compact := some K` (`0`: off)              | cert_run, cert_trace         |

The engines and what proves them:
- ir: `irEngine` (staged) or `irRawEngine`, after `checkDesign`; `irEngine_sound`.
- ref: `refEngine`; `refEngine_sound`.
- vc: `compileDesign`, the optimizer unless `opt:off`, then by `vcInc`/`vcMem`: `vcEngine`
  (event-driven, memories as tables; compiled code skips chunks with nothing pending),
  `vcEvFunEngine`, `vcScanEngine`, or `vcFullEngine` (also for a program that is not
  well-ordered). Each is sound against `denoteResidual` of the program it runs
  (`Compiler/VcEngine`), and so against `interpretDesign` (`CompileOpt`'s `*_interp`, on the
  lake side).
- fp: `mkStepper`'s step (`fpStep`, shared tails unless `fpShare := false`) as an engine on
  runtime states; `fpStep_agree`: its outputs are `interpretDesign`'s and its state agrees below
  every memory's depth.
`mkEngine_ir_sound`, `mkEngine_ref_sound`, `mkEngine_vc_sound` and `mkEngine_fp_agree` say this of
whatever `mkEngine` returns.

Mathlib-free.
-/
import HDLProof.Run.Sound
import LeanSemanticPrimitives.Compiler.VcEngine

namespace HDLProof
namespace Run

open Compiler Compiler.Direct

/-- vc's step after the first. -/
inductive VcInc where
  /-- event-driven: only the bindings that read a changed slot (`ResidualEv`); the default -/
  | event
  /-- OLDER, SLOWER: incremental, visiting every binding (`ResidualInc.incStep`) -/
  | scan
  /-- OLDER, SLOWEST: the whole `denoteResidual` -/
  | off
deriving BEq, Inhabited, Repr

/-- How vc's event-driven step keeps memories. -/
inductive VcMem where
  /-- tables (`ResidualFlat`); the default -/
  | table
  /-- OLDER, SLOWER: functions (closure chains, `ResidualEv`) -/
  | func
deriving BEq, Inhabited, Repr

/-- A route and how to run it (see the module header). -/
structure Knobs where
  route    : Route := .vc
  vcOpt    : Bool := true
  vcInc    : VcInc := .event
  vcMem    : VcMem := .table
  irStaged : Bool := true
  fpShare  : Bool := true
  /-- `none`: automatic (the driver's policy); `some 0`: off; `some K`: every `K` cycles. -/
  compact  : Option Nat := none
deriving Inhabited, Repr

/-- Apply one option to the knobs: `none` when the option is not a knob (the driver's own),
otherwise the new knobs or why the option is malformed. -/
def Knobs.apply? (k : Knobs) (opt : String) : Option (Except String Knobs) :=
  let drop (p : String) : String := (opt.drop p.length).toString
  if opt.startsWith "route:" then
    some <| match (if drop "route:" == "tv" then some Route.vc else Route.parse? (drop "route:")) with
      | some r => .ok { k with route := r }
      | none => .error s!"unknown route {opt}"
  else if opt.startsWith "compact:" then
    some <| match (drop "compact:").toNat? with
      | some n => .ok { k with compact := some n }
      | none => .error s!"compact: {repr opt} is not a number of cycles"
  else
    match opt with
    | "opt:off" => some (.ok { k with vcOpt := false, fpShare := false })
    | "inc:off" => some (.ok { k with vcInc := .off })
    | "inc:scan" => some (.ok { k with vcInc := if k.vcInc == .off then .off else .scan })
    | "mem:fun" => some (.ok { k with vcMem := .func })
    | "share:off" => some (.ok { k with fpShare := false })
    | "ir:raw" => some (.ok { k with irStaged := false })
    | _ => none

/-- Apply options in order; the ones that are not knobs come back, in order, for the driver. -/
def Knobs.parse (k : Knobs) (opts : List String) : Except String (Knobs × List String) :=
  opts.foldlM (init := (k, [])) fun (k, rest) o =>
    match k.apply? o with
    | none => .ok (k, rest ++ [o])
    | some (.ok k') => .ok (k', rest)
    | some (.error e) => .error e

/-- A built route: its state type, its engine, and what was built (one line each; `cert_trace
opt:stats` prints vc's). -/
structure Built where
  σ      : Type
  engine : Engine σ
  notes  : Array String := #[]

/-- Why a route could not be built: the refusal itself (`detail`: a check's rendering, the
compiler's error) and the message `mkStepper` gives for it. -/
structure BuildError where
  route   : Route
  detail  : String
  message : String

/-- vc's memory depths, for its tables. -/
def vcDepth (D : DesignCert) : Nat → Nat := fun k => (D.memories[k]?.map (·.depth)).getD 0

/-- vc's engine on the program `R`, chosen by the knobs (a program that is not well-ordered runs
the full step). -/
def vcEngineOf (D : DesignCert) (k : Knobs) (R : ResidualProgram) : Built :=
  if k.vcInc != .off && ResidualInc.wellOrdered R then
    match k.vcInc, k.vcMem with
    | .scan, _ => ⟨_, vcScanEngine R, #[]⟩
    | _, .func => ⟨_, vcEvFunEngine R (ResidualInc.fanout R), #[]⟩
    | _, .table => ⟨_, vcEngine R (ResidualInc.fanout R) (vcDepth D), #[]⟩
  else ⟨_, vcFullEngine R, #[]⟩

/-- vc's notes, worded as `cert_trace opt:stats` prints them. -/
def vcNotes (k : Knobs) (R0 R : ResidualProgram) (accepted : Bool) : Array String :=
  let verdict := if !k.vcOpt then "off" else if accepted then "accepted" else "REFUSED (unoptimized)"
  let inc := k.vcInc != .off && ResidualInc.wellOrdered R
  let incNote :=
    if inc then
      (if k.vcInc == .scan then "on (scan)" else if k.vcMem == .func then "on (event-driven)"
       else "on (event-driven, memories as tables)")
    else if k.vcInc != .off then "off (not well-ordered)" else "off"
  #[s!"vc optimize: {verdict}; sources {R0.sources.size} -> {R.sources.size}, \
      bindings {R0.bindings.size} -> {R.bindings.size}",
    s!"vc incremental: {incNote}"]

/-- The engine `knobs` name for design `D`, or why it cannot be built. -/
def mkEngine (D : DesignCert) (k : Knobs) : Except BuildError Built :=
  match k.route with
  | .ir =>
    match Direct.checkDesign D with
    | .error e =>
      .error ⟨.ir, e.render, s!"ir: the direct simulator refuses the certificate: {e.render}"⟩
    | .ok _ => .ok ⟨_, if k.irStaged then irEngine D else irRawEngine D, #[]⟩
  | .ref => .ok ⟨_, refEngine D, #[]⟩
  | .vc =>
    match compileDesign D with
    | .error e => .error ⟨.vc, s!"{repr e}", s!"vc: compileDesign refused: {repr e}"⟩
    | .ok R0 =>
      let (R, accepted) := if k.vcOpt then ResidualOpt.optimizeWith R0 else (R0, false)
      let b := vcEngineOf D k R
      .ok { b with notes := vcNotes k R0 R accepted }
  | .fp =>
    match fpChecks D with
    | some e => .error ⟨.fp, e, s!"fp: {e}"⟩
    | none =>
      match fpResidual D with
      | .error e => .error ⟨.fp, e, e⟩
      | .ok R =>
        match Projection.HwMem.mkMemSimOfW D R with
        | none =>
          let m := "fp: the memory route does not admit the design, or checkResidual rejects its residual"
          .error ⟨.fp, m, m⟩
        | some sim =>
          .ok ⟨_, Engine.ofStep "fp"
            (fpStep D ⟨sim, if k.fpShare then Projection.ShareTails.shareSim sim else none⟩), #[]⟩

/-- Run `cont` on the engine `knobs` name for `D`; the build's refusal otherwise. -/
def withEngine {α : Type} (D : DesignCert) (k : Knobs)
    (cont : (σ : Type) → Engine σ → Array String → IO α) : IO (Except BuildError α) :=
  match mkEngine D k with
  | .error e => pure (.error e)
  | .ok b => do return .ok (← cont b.σ b.engine b.notes)

/-! ## What `mkEngine` builds is sound -/

theorem mkEngine_ir_sound {D : DesignCert} {k : Knobs} {b : Built} (hr : k.route = .ir)
    (h : mkEngine D k = .ok b) : b.engine.Sound (interpretDesign D) := by
  unfold mkEngine at h
  rw [hr] at h
  dsimp only at h
  split at h
  · cases h
  · rename_i u hd
    cases u
    cases h
    cases k.irStaged
    · exact irRawEngine_sound hd
    · exact irEngine_sound hd

theorem mkEngine_ref_sound {D : DesignCert} {k : Knobs} {b : Built} (hr : k.route = .ref)
    (h : mkEngine D k = .ok b) : b.engine.Sound (interpretDesign D) := by
  unfold mkEngine at h
  rw [hr] at h
  cases h
  exact refEngine_sound D

theorem vcEngineOf_sound (D : DesignCert) (k : Knobs) (R : ResidualProgram) :
    (vcEngineOf D k R).engine.Sound (denoteResidual R) := by
  unfold vcEngineOf
  by_cases hc : (k.vcInc != .off && ResidualInc.wellOrdered R) = true
  · have hw : ResidualInc.wellOrdered R = true := by
      simp only [Bool.and_eq_true] at hc; exact hc.2
    rw [ite_eq_left hc]
    cases k.vcInc <;> cases k.vcMem
    · exact vcEngine_sound hw _
    · exact vcEvFunEngine_sound hw
    · exact vcScanEngine_sound hw
    · exact vcScanEngine_sound hw
    · exact vcEngine_sound hw _
    · exact vcEvFunEngine_sound hw
  · rw [ite_eq_right hc]
    exact vcFullEngine_sound R

/-- vc's engine is `denoteResidual`'s of the compiled program, optimized unless `opt:off`
(`ResidualOpt.optimize` is the optimizer's program); `compileDesign_correct` and
`compileOpt_correct` (lake side) make that `interpretDesign`'s. -/
theorem mkEngine_vc_sound {D : DesignCert} {k : Knobs} {b : Built} (hr : k.route = .vc)
    (h : mkEngine D k = .ok b) : ∃ R0, compileDesign D = .ok R0 ∧
      b.engine.Sound (denoteResidual (if k.vcOpt then ResidualOpt.optimize R0 else R0)) := by
  unfold mkEngine at h
  rw [hr] at h
  dsimp only at h
  split at h
  · cases h
  · rename_i R0 hc
    refine ⟨R0, hc, ?_⟩
    cases h
    have key : ∀ c : Bool,
        (vcEngineOf D k (if c = true then ResidualOpt.optimizeWith R0 else (R0, false)).1).engine.Sound
          (denoteResidual (if c = true then ResidualOpt.optimize R0 else R0)) := by
      intro c
      cases c
      · exact vcEngineOf_sound D k R0
      · exact vcEngineOf_sound D k _
    exact key k.vcOpt

/-- fp's engine is a step on runtime states that agrees with `interpretDesign` (`StepAgree`), for
a certificate `fpChecks` accepts. -/
theorem mkEngine_fp_agree {D : DesignCert} {k : Knobs} {b : Built} (hr : k.route = .fp)
    (h : mkEngine D k = .ok b) :
    fpChecks D = none ∧ ∃ step, StepAgree D step ∧ b = ⟨RuntimeState, Engine.ofStep "fp" step, #[]⟩ := by
  unfold mkEngine at h
  rw [hr] at h
  dsimp only at h
  cases hc : fpChecks D with
  | some e => simp [hc] at h
  | none =>
    obtain ⟨hsem, haddr, hdepth, hroot, hword⟩ := fpChecks_none hc
    cases hres : fpResidual D with
    | error e => simp [hc, hres] at h
    | ok R =>
      obtain ⟨sf, wf, hproj⟩ := fpResidual_ok hres
      cases hs : Projection.HwMem.mkMemSimOfW D R with
      | none => simp [hc, hres, hs] at h
      | some sim =>
        simp only [hc, hres, hs, Except.ok.injEq] at h
        exact ⟨rfl, _, fpStep_agree hsem haddr hdepth hroot hword hproj hs
          (by cases k.fpShare <;> simp), h.symm⟩

/-- **A closed-loop run on the engine `mkEngine` builds is the reference trace of its ticks**: for
ir, ref and vc (`runCycles_sound`; vc against its program's `denoteResidual`, which
`compileOpt_correct` makes `interpretDesign`'s on the lake side) the run's outputs and final state
are `refTrace`'s; for fp (`runCycles_fp_outputs`) the outputs, and a final state that agrees. -/
theorem mkEngine_runCycles_fp {ctx : Ctx} {k : Knobs} {b : Built} (hr : k.route = .fp)
    (h : mkEngine ctx.D k = .ok b) {n : Nat} {st : LoopState b.σ} {ts : List Tick}
    {os : List (Array BV)} {st' : LoopState b.σ} {why : Option String}
    (hfit : Fits ctx.D (b.engine.view st.s)) (hrun : runCycles ctx b.engine n st = .ok (ts, os, st', why)) :
    (refTrace ctx.D (b.engine.view st.s) ts).steps.map (·.outputs) = os ∧
    Projection.CkptResume.StateAgree ctx.D (b.engine.view st'.s)
      (refTrace ctx.D (b.engine.view st.s) ts).finalState := by
  obtain ⟨hc, step, hs, rfl⟩ := mkEngine_fp_agree hr h
  exact runCycles_fp_outputs hs hc hfit hrun

end Run
end HDLProof

#audit_axioms HDLProof.Run.mkEngine_ir_sound
#audit_axioms HDLProof.Run.mkEngine_vc_sound
#audit_axioms HDLProof.Run.mkEngine_fp_agree
#audit_axioms HDLProof.Run.mkEngine_runCycles_fp
