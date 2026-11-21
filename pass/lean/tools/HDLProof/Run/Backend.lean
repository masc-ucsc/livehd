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
-/
import LeanSemanticPrimitives.Compiler.DirectSemantics
import LeanSemanticPrimitives.Compiler.CompileDesignDefs
import LeanSemanticPrimitives.Compiler.ResidualFast
import LeanSemanticPrimitives.Compiler.ResidualOpt

namespace HDLProof
namespace Run

open Compiler

inductive Route where
  | ir | vc | ref
deriving BEq, Inhabited, Repr

def Route.parse? : String → Option Route
  | "ir" => some .ir
  | "vc" => some .vc
  | "ref" => some .ref
  | _ => none

def Route.name : Route → String
  | .ir => "ir" | .vc => "vc" | .ref => "ref"

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

/-- The stepper of route `r` for design `D`, or why it cannot be built. -/
def mkStepper (D : DesignCert) (r : Route) (opt : Bool := true) : Except String Stepper :=
  match r with
  | .ir =>
    match Direct.checkDesign D with
    | .error e => .error s!"ir: the direct simulator refuses the certificate: {e.render}"
    | .ok _ => .ok { name := "ir", step := irStep D }
  | .vc =>
    match compileDesign D with
    | .error e => .error s!"vc: compileDesign refused: {repr e}"
    | .ok R0 =>
      let R := if opt then (ResidualOpt.optimizeWith R0).1 else R0
      .ok { name := "vc", step := fun e i s => .ok (denoteResidual R e i s) }
  | .ref => .ok { name := "ref", step := fun e i s => .ok (interpretDesign D e i s) }

/-- Every clock fires. -/
def allClocks (D : DesignCert) : ClockEdges := Array.replicate D.clocks.size true

end Run
end HDLProof
