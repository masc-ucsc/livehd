/-
# Steppers: one clock step of a design, on a chosen route

A stepper maps a clock-edge vector, an input vector and a state to the step's result: the
outputs BEFORE the edge and the state AFTER it, exactly `interpretDesign`'s shape. Three routes:

* `ir`: the direct simulator. `checkDesign` once, then per step `checkRuntime` and
  `directStepRaw`, which `directStepRaw_correct` proves equal to `interpretDesign`
  (`Compiler/DirectSemantics.lean`; the same pair `runDirect` iterates).
* `vc`: the verified compiler. `compileDesign` once, its optimizer once
  (`ResidualOpt.optimizeWith`; `opt:off` keeps the unoptimized program), then per step
  `denoteResidual`, which `compileDesign_correct` / `compileOpt_correct` prove equal to
  `interpretDesign`. These are the steps `cert_trace route:vc` runs.
* `ref`: `interpretDesign` itself. Exact by definition and slow on memories (a write wraps the
  memory function, so reads walk the chain).
* `fp`: the Futamura projection's residual (`dcert2_memroute`'s route: `MemFused`, plain writes
  admitted, the step with its list tails shared, `Projection/ShareTails`). The residual is
  specialized and checked once; each step converts the state to the residual's (`Ckpt.save`, then
  `CkptStateM.toStateM`), checks the runtime policy, runs the residual and returns the state it
  denotes. Its outputs are `interpretDesign`'s, and its state agrees with `interpretDesign`'s below
  every memory's depth (`Run/Sound`, `fpStep_agree`, `runCycles_agree`); `opt:off` runs the
  residual as specialized (no shared tails).
-/
import LeanSemanticPrimitives.Compiler.DirectSemantics
import LeanSemanticPrimitives.Compiler.DirectStaged
import LeanSemanticPrimitives.Compiler.CompileDesignDefs
import LeanSemanticPrimitives.Compiler.ResidualFast
import LeanSemanticPrimitives.Compiler.ResidualOpt
import LeanSemanticPrimitives.Compiler.MemAddr
import LeanSemanticPrimitives.Projection.MemFused
import LeanSemanticPrimitives.Projection.HardwareMemWriteAdmit
import LeanSemanticPrimitives.Projection.ShareTails
import LeanSemanticPrimitives.Projection.CkptStateM

namespace HDLProof
namespace Run

open Compiler

inductive Route where
  | ir | vc | ref | fp
deriving BEq, Inhabited, Repr

def Route.parse? : String → Option Route
  | "ir" => some .ir
  | "vc" => some .vc
  | "ref" => some .ref
  | "fp" => some .fp
  | _ => none

def Route.name : Route → String
  | .ir => "ir" | .vc => "vc" | .ref => "ref" | .fp => "fp"

structure Stepper where
  name : String
  step : ClockEdges → RuntimeInput → RuntimeState → Except String RuntimeResult

instance : Inhabited Stepper := ⟨{ name := "none", step := fun _ _ _ => .error "no stepper" }⟩

/-- The `ir` stepper, once `checkDesign D` has passed. -/
def irStep (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    Except String RuntimeResult :=
  match Direct.checkRuntime D e i s with
  | .error err => .error err.render
  | .ok _ => .ok (Direct.directStepRaw D e i s)

/-- The `ir` stepper on the staged direct step (`Compiler/DirectStaged`): the design is prepared
once, each step is `Staged.step`, which `Staged.step_eq` proves equal to `directStepRaw`. -/
def irStepStaged (P : Staged.Prep) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    Except String RuntimeResult :=
  match Direct.checkRuntime P.D e i s with
  | .error err => .error err.render
  | .ok _ => .ok (Staged.step P e i s)

/-- The `fp` route's residual: the checked bundle and, unless `opt:off`, its shared-tails form. -/
structure FpSim where
  sim : Projection.MemProjection.MemSimulator
  sh  : Option (Projection.Program × Nat)

/-- The `fp` stepper: the state as the residual's, the runtime policy, one residual step. -/
def fpStep (D : DesignCert) (f : FpSim) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    Except String RuntimeResult :=
  let st := Projection.CkptStateM.toStateM (Ckpt.save D 0 0 #[] s)
  if Projection.HwMem.runtimeFullOKB D i st e then
    match Projection.ShareTails.runMemSimS f.sim f.sh e i st with
    | some r => .ok r.denote
    | none => .error "fp: the residual gave no value"
  else .error "fp: the runtime policy refuses the state, the input or the edge vector"

/-- The specializer's step budget, as `dcert2_memroute auto` sizes it. -/
def fpStepFuel (D : DesignCert) : Nat := max 20000 (4 * (D.nodes.size + D.sources.size))

/-- The certificate checks the `fp` route's agreement theorem reads (`Run/Sound`). -/
def fpChecks (D : DesignCert) : Option String :=
  match Direct.checkDesign D with
  | .error e => some s!"the direct check refuses the certificate: {e.render}"
  | .ok _ =>
    match MemAddr.addrError D, MemAddr.depthError D, Ckpt.imgRootError D, Ckpt.wordError D with
    | some e, _, _, _ | none, some e, _, _ | none, none, some e, _ | none, none, none, some e => some e
    | none, none, none, none => none

/-- The specialized residual: at the `auto` budget, and once more at ten times it after an
out-of-fuel, as `dcert2_memroute auto auto` does. -/
def fpResidual (D : DesignCert) : Except String Projection.Program :=
  let sf := fpStepFuel D
  match Projection.MemFused.projectMemFDesignAt sf 2000 D with
  | .ok R => .ok R
  | .error .outOfFuel =>
    match Projection.MemFused.projectMemFDesignAt (10 * sf) 20000 D with
    | .ok R => .ok R
    | .error e => .error s!"fp: specialization failed ({repr e})"
  | .error e => .error s!"fp: specialization failed ({repr e})"

/-- The stepper of route `r` for design `D`, or why it cannot be built. -/
def mkStepper (D : DesignCert) (r : Route) (opt : Bool := true) : Except String Stepper :=
  match r with
  | .ir =>
    match Direct.checkDesign D with
    | .error e => .error s!"ir: the direct simulator refuses the certificate: {e.render}"
    | .ok _ =>
      let P := Staged.prepare D
      .ok { name := "ir", step := irStepStaged P }
  | .vc =>
    match compileDesign D with
    | .error e => .error s!"vc: compileDesign refused: {repr e}"
    | .ok R0 =>
      let R := if opt then (ResidualOpt.optimizeWith R0).1 else R0
      .ok { name := "vc", step := fun e i s => .ok (denoteResidual R e i s) }
  | .ref => .ok { name := "ref", step := fun e i s => .ok (interpretDesign D e i s) }
  | .fp =>
    match fpChecks D with
    | some e => .error s!"fp: {e}"
    | none =>
      match fpResidual D with
      | .error e => .error e
      | .ok R =>
        match Projection.HwMem.mkMemSimOfW D R with
        | none => .error "fp: the memory route does not admit the design, or checkResidual rejects its residual"
        | some sim =>
          .ok { name := "fp",
                step := fpStep D ⟨sim, if opt then Projection.ShareTails.shareSim sim else none⟩ }

/-- Every clock fires. -/
def allClocks (D : DesignCert) : ClockEdges := Array.replicate D.clocks.size true

end Run
end HDLProof
