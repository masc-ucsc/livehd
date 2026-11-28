/-
# Steppers: one clock step of a design, on a chosen route

A stepper maps a clock-edge vector, an input vector and a state to the step's result: the
outputs BEFORE the edge and the state AFTER it, exactly `interpretDesign`'s shape. Four routes:

* `ir`: the direct simulator. `checkDesign` and `Staged.prepare` once, then per step
  `checkRuntime` and the staged step (`Compiler/DirectStaged`), which `Staged.step_eq` proves equal
  to `directStepRaw`, and `directStepRaw_correct` to `interpretDesign`
  (`Compiler/DirectSemantics.lean`; `runDirect` iterates the unstaged pair).
* `vc`: the verified compiler. `compileDesign` once, its optimizer once
  (`ResidualOpt.optimizeWith`; `opt:off` keeps the unoptimized program), then per step the whole
  `denoteResidual`, which `compileDesign_correct` / `compileOpt_correct` prove equal to
  `interpretDesign`. A stepper carries no state between steps, so this is the full step:
  `cert_trace route:vc` runs it only with `inc:off`; its default step reuses the last step's
  environment (`Compiler/ResidualEv`, `ResidualFlat`, `ResidualWork`). `Run/Engine`'s
  `EngineSelector` builds every route and mode as an engine that keeps such state, and the drivers
  run those engines. Kept here: `irStep` (`irStep_sound`), `fpStep` (the `fp` engine's `loop:full`
  step, `fpStep_agree`), and the `fp` route's checks and residual (`fpChecks`, `fpResidual`).
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

/-- The `ir` stepper, once `checkDesign D` has passed. -/
def irStep (D : DesignCert) (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    Except String RuntimeResult :=
  match Direct.checkRuntime D e i s with
  | .error err => .error err.render
  | .ok _ => .ok (Direct.directStepRaw D e i s)

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

end Run
end HDLProof
