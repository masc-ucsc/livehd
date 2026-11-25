/-
# `EngineSelector`: choose a route, prepare it for a design, get its engine

`EngineSelector D knobs` is the one place that turns a design and a choice of route into a
simulator that can be stepped:

    D      : DesignCert                        the design certificate (`CertIO.admitAny` on a `.dcert`)
    knobs  : Knobs                             the route and its modes (defaults: the fastest proved)
    result : Except SelectError SelectedEngine

It does the route's one-time work for `D` and hands back the engine:
- ir: `checkDesign`, then `Staged.prepare` (or nothing more with `irStaged := false`);
- vc: `compileDesign`, the optimizer (unless `vcOpt := false`), the well-ordering check, the
  fanout table;
- ref: nothing (`interpretDesign` itself);
- fp: the certificate checks, the specialization, `checkResidual`, the shared tails; the engine
  keeps the residual's own state between steps (`Run/FpEngine`).
`SelectedEngine` is `{ σ, engine, notes }`: the engine (`Compiler.Engine σ`: `load`, `step`,
`view`, `reload`, `peek?`) on a state type `σ` of the route's own kind, and one line per thing
prepared (`cert_trace opt:stats` prints vc's). `SelectError` carries the refusal itself
(`detail`) and the message the drivers print (`message`).

## How to use it

    -- a loop over an engine: the state goes from one step to the next as the argument of a
    -- recursive function, so each step holds the only reference to it (`Compiler/Engine`)
    partial def run {σ : Type} (E : Engine σ) (n k : Nat) (q : σ) : IO Unit := do
      if k ≥ n then return
      match E.step (edgesAt D .all k) (inputsAt k) q with
      | .ok (outs, q') => IO.println outs; run E n (k + 1) q'
      | .error (why, _) => IO.eprintln why            -- a refusal hands the state back

    match EngineSelector D {} with                     -- {}: vc, optimized, event-driven, tables
    | .error e => IO.eprintln e.message                -- e.g. "vc: compileDesign refused: ..."
    | .ok sel => run sel.engine n 0 (sel.engine.load (Direct.resetState D))

Other routes and modes: `EngineSelector D { route := .ir }`, `{ route := .vc, vcInc := .off }`,
or the drivers' options through `Knobs.parse {} ["route:fp", "share:off"]`.
`withSelectedEngine D knobs k` runs `k σ engine notes` instead of returning the engine.
`EngineSelector_ir_sound`, `EngineSelector_ref_sound`, `EngineSelector_vc_sound` and
`EngineSelector_fp_agree` say that whatever it returns is the design's semantics; a closed loop on it
(`cycleStep`, `runCycles_sound`, `EngineSelector_runCycles_fp`) is the reference trace of its ticks.
`cert_run` is the first driver on it.

## Knobs

`Knobs`' defaults are the fastest proved combination: vc, optimized, event-driven, memories as
tables. A driver that runs another route by default (`cert_run`, `cert_trace`: ir) sets its route
first and applies its options on top (`Knobs.apply?`). Every route option the drivers took
before maps onto a knob, and every combination they could run is still built:

| option      | knob                                        | drivers that take it              |
|-------------|---------------------------------------------|-----------------------------------|
| `route:R`   | `route` (ir, vc, ref, fp; `tv` = vc)        | cert_run, cert_trace              |
| `opt:off`   | `vcOpt := false` and `fpShare := false`     | cert_run, cert_trace              |
| `inc:off`   | `vcInc := .off` (the full step every time)  | cert_run, cert_trace              |
| `inc:scan`  | `vcInc := .scan` (unless `inc:off`)         | cert_run, cert_trace              |
| `mem:fun`   | `vcMem := .func` (memories as functions)    | cert_run, cert_trace              |
| `share:off` | `fpShare := false`                          | cert_run, dcert2_memroute         |
| `loop:full` | `fpLoopFull := true` (convert every step)   | cert_run, dcert2_memroute         |
| `ir:raw`    | `irStaged := false` (`directStepRaw`)       | cert_run (the judge's step)       |
| `compact:K` | `compact := some K` (`0`: off)              | cert_run, cert_trace              |

The engines and what proves them:
- ir: `irEngine` (staged) or `irRawEngine`, after `checkDesign`; `irEngine_sound`.
- ref: `refEngine`; `refEngine_sound`.
- vc: `compileDesign`, the optimizer unless `opt:off`, then by `vcInc`/`vcMem`: `vcEngine`
  (event-driven, memories as tables; compiled code skips chunks with nothing pending),
  `vcEvFunEngine`, `vcScanEngine`, or `vcFullEngine` (also for a program that is not
  well-ordered). Each is sound against `denoteResidual` of the program it runs
  (`Compiler/VcEngine`), and so against `interpretDesign` (`CompileOpt`'s `*_interp`, on the
  lake side).
- fp: `fpEncEngine` (`Run/FpEngine`): the residual's state kept encoded between steps, the policy's
  size half checked once per sizes, only the outputs decoded; `fpEncEngine_agree`: its outputs are
  `interpretDesign`'s and its state agrees below every memory's depth (a step is exact; a load goes
  through a checkpoint). OLDER, SLOWER with `loop:full`: `mkStepper`'s step (`fpStep`), which
  converts the whole state to the residual's and back every step; `fpStep_agree`. Both with shared
  tails unless `fpShare := false`.
Mathlib-free.
-/
import HDLProof.Run.FpEngine
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
  /-- OLDER, SLOWER: `fp`'s step on runtime states (`fpStep`), converting every step -/
  fpLoopFull : Bool := false
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
    | "loop:full" => some (.ok { k with fpLoopFull := true })
    | _ => none

/-- Apply options in order; the ones that are not knobs come back, in order, for the driver. -/
def Knobs.parse (k : Knobs) (opts : List String) : Except String (Knobs × List String) :=
  opts.foldlM (init := (k, [])) fun (k, rest) o =>
    match k.apply? o with
    | none => .ok (k, rest ++ [o])
    | some (.ok k') => .ok (k', rest)
    | some (.error e) => .error e

/-- What `EngineSelector` returns: the route's state type `σ`, its engine on `σ`, and one note per
thing prepared (`cert_trace opt:stats` prints vc's). -/
structure SelectedEngine where
  σ      : Type
  engine : Engine σ
  notes  : Array String := #[]

/-- Why `EngineSelector` could not prepare the route for the design: the refusal itself (`detail`:
a check's rendering, the compiler's error) and the message drivers print (`mkStepper`'s wording). -/
structure SelectError where
  route   : Route
  detail  : String
  message : String

/-- vc's memory depths, for its tables. -/
def vcDepth (D : DesignCert) : Nat → Nat := fun k => (D.memories[k]?.map (·.depth)).getD 0

/-- vc's engine on the program `R`, chosen by the knobs (a program that is not well-ordered runs
the full step). -/
def vcEngineOf (D : DesignCert) (k : Knobs) (R : ResidualProgram) : SelectedEngine :=
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

/-- **Choose the route `knobs` name, prepare it for design `D`, and return its engine** (with its
state type and notes), or why the route refuses `D`. See the module header for how to use it. -/
def EngineSelector (D : DesignCert) (k : Knobs) : Except SelectError SelectedEngine :=
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
          let sh := if k.fpShare then Projection.ShareTails.shareSim sim else none
          if k.fpLoopFull then .ok ⟨_, Engine.ofStep "fp" (fpStep D ⟨sim, sh⟩), #[]⟩
          else .ok ⟨_, fpEncEngine D sim sh, #[]⟩

/-- Run `cont` on the engine `knobs` name for `D`; the build's refusal otherwise. -/
def withSelectedEngine {α : Type} (D : DesignCert) (k : Knobs)
    (cont : (σ : Type) → Engine σ → Array String → IO α) : IO (Except SelectError α) :=
  match EngineSelector D k with
  | .error e => pure (.error e)
  | .ok b => do return .ok (← cont b.σ b.engine b.notes)

/-! ## What `EngineSelector` builds is sound -/

theorem EngineSelector_ir_sound {D : DesignCert} {k : Knobs} {b : SelectedEngine} (hr : k.route = .ir)
    (h : EngineSelector D k = .ok b) : b.engine.Sound (interpretDesign D) := by
  unfold EngineSelector at h
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

theorem EngineSelector_ref_sound {D : DesignCert} {k : Knobs} {b : SelectedEngine} (hr : k.route = .ref)
    (h : EngineSelector D k = .ok b) : b.engine.Sound (interpretDesign D) := by
  unfold EngineSelector at h
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
theorem EngineSelector_vc_sound {D : DesignCert} {k : Knobs} {b : SelectedEngine} (hr : k.route = .vc)
    (h : EngineSelector D k = .ok b) : ∃ R0, compileDesign D = .ok R0 ∧
      b.engine.Sound (denoteResidual (if k.vcOpt then ResidualOpt.optimize R0 else R0)) := by
  unfold EngineSelector at h
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

/-- fp's engine agrees with `interpretDesign` (`EngineAgree`), for a certificate `fpChecks` accepts:
the encoded engine (`fpEncEngine_agree`), or with `loop:full` the step on runtime states
(`fpStep_agree`). -/
theorem EngineSelector_fp_agree {D : DesignCert} {k : Knobs} {b : SelectedEngine} (hr : k.route = .fp)
    (h : EngineSelector D k = .ok b) :
    fpChecks D = none ∧ ∃ Inv : b.σ → Prop, EngineAgree D b.engine Inv := by
  unfold EngineSelector at h
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
        simp only [hc, hres, hs] at h
        have hsh : (if k.fpShare then Projection.ShareTails.shareSim sim else none)
              = Projection.ShareTails.shareSim sim ∨
            (if k.fpShare then Projection.ShareTails.shareSim sim else none) = none := by
          cases k.fpShare <;> simp
        refine ⟨rfl, ?_⟩
        cases hlf : k.fpLoopFull
        · simp only [hlf, Bool.false_eq_true, ite_false, Except.ok.injEq] at h
          subst h
          exact ⟨_, fpEncEngine_agree hsem hdepth hroot hword hproj hs hsh⟩
        · simp only [hlf, ite_true, Except.ok.injEq] at h
          subst h
          exact ⟨_, ofStep_engineAgree (fpStep_agree hsem haddr hdepth hroot hword hproj hs hsh)⟩

/-- **A closed-loop run on fp's engine, from the engine's load of a state `s0` that fits, prints
`refTrace`'s outputs from `s0`** and ends in a state that agrees with `refTrace`'s. (For ir, ref
and vc it is `runCycles_sound` on `EngineSelector_*_sound`; vc against its program's
`denoteResidual`, which `compileOpt_correct` makes `interpretDesign`'s on the lake side.) -/
theorem EngineSelector_runCycles_fp {ctx : Ctx} {k : Knobs} {b : SelectedEngine} (hr : k.route = .fp)
    (h : EngineSelector ctx.D k = .ok b) {s0 : RuntimeState} (hfit : Fits ctx.D s0) {n : Nat}
    {host : HostState} {c0 : Nat} {ts : List Tick} {os : List (Array BV)} {st' : LoopState b.σ}
    {why : Option String}
    (hrun : runCycles ctx b.engine n ⟨b.engine.load s0, host, c0⟩ = .ok (ts, os, st', why)) :
    (refTrace ctx.D s0 ts).steps.map (·.outputs) = os ∧
    Projection.CkptResume.StateAgree ctx.D (b.engine.view st'.s) (refTrace ctx.D s0 ts).finalState := by
  obtain ⟨hc, Inv, hag⟩ := EngineSelector_fp_agree hr h
  obtain ⟨hsem, haddr, -, hroot, -⟩ := fpChecks_none hc
  obtain ⟨hinv, hfit', hag0⟩ := hag.load s0 hfit
  exact runCycles_agree hag hsem haddr hroot n _ s0 ts os st' why hinv hfit' hag0 hrun

end Run
end HDLProof

#audit_axioms HDLProof.Run.EngineSelector_ir_sound
#audit_axioms HDLProof.Run.EngineSelector_vc_sound
#audit_axioms HDLProof.Run.EngineSelector_fp_agree
#audit_axioms HDLProof.Run.EngineSelector_runCycles_fp
