import HDLProof.Sym.Native
import LeanSemanticPrimitives.Projection.MemFused
import LeanSemanticPrimitives.Projection.MemVariant
import LeanSemanticPrimitives.Compiler.CompileDesignDefs

/-! The simulator route of the proof tools' symbolic step (`sim:` in `sym_verify` and
`sym_refmap`). Each route is a `Stepper` with a proved `MemResidual` witness:
- `fp`: the memory interpreter specialized to the certificate (`route:` picks the variant:
  `fused` `projectMemFDesignAt` / `of_projF`, `var` `projectMemVarDesignAt` / `of_projVar`,
  `plain` `projectMemDesignAt` / `of_proj`); fuel from `Hw.checkResidual`.
- `ir`: the certificate's dense nodes walked directly (`irStepper_sound`); fuel = node count.
- `vc`: the verified compiler's bindings (`compileDesign`, `vcStepper_sound`); fuel = binding
  count.
All three need `admitsWB D`, which the drivers check first.

The default is `vc` (`defaultSim`): on a large design it is the fastest of the three and the
smallest in memory (`projects/sym_routes/PERF.md`), and it needs no specialization. `closed`,
`emit:` and `route:` exist only for fp, so a run that names one of them and no `sim:` uses fp. -/

open Compiler Projection Projection.MemProjection HDLProof.Sym HDLProof.Sym.Native
namespace HDLProof.Kit

structure SymRoute where
  /-- the symbolic step -/
  S    : Stepper
  /-- the fuel the step needs -/
  fuel : Nat
  /-- the fp residual, when this route built one (counterexample replay uses it) -/
  fp   : Option Program
  /-- for reports -/
  name : String

/-- The route a run uses: `sim:` when given (`tv` is the old name of `vc`); otherwise `vc`, or
`fp` when the run asks for something only fp has (`fpOnly`: `closed`, `emit:`, `route:`). -/
def defaultSim (asked : Option String) (fpOnly : Bool) : String :=
  match asked with
  | some s => if s == "tv" then "vc" else s
  | none => if fpOnly then "fp" else "vc"

/-- How the route was chosen, for the run's report. -/
def simWhy (asked : Option String) (fpOnly : Bool) : String :=
  match asked with
  | some _ => ""
  | none => if fpOnly then " (no sim: given; closed / emit: / route: exist only for fp)" else " (the default)"

/-- Build the route `sim` (fp | ir | vc) for `D`; `route` is fp's variant. -/
def mkSymRoute (sim route : String) (sf wf : Nat) (D : DesignCert) : Except String SymRoute :=
  match sim with
  | "fp" =>
      let res :=
        if route == "plain" then projectMemDesignAt sf wf D
        else if route == "var" then MemVariant.projectMemVarDesignAt sf wf D
        else MemFused.projectMemFDesignAt sf wf D
      match res with
      | .error e => .error s!"specialization failed: {repr e}"
      | .ok R =>
          match Hw.checkResidual R with
          | none => .error "checkResidual rejected the residual"
          | some bound =>
              let projName := if route == "plain" then "projectMemDesignAt"
                else if route == "var" then "projectMemVarDesignAt, env0 inlined"
                else "projectMemFDesignAt, fold fused"
              .ok { S := R, fuel := bound, fp := some R, name := s!"fp ({projName}, bound {bound})" }
  | "ir" => .ok { S := irStepper D, fuel := D.nodes.size, fp := none,
                  name := s!"ir (dense nodes, {D.nodes.size})" }
  | "vc" =>
      match compileDesign D with
      | .error e => .error s!"compileDesign refused: {repr e}"
      | .ok R => .ok { S := vcStepper R, fuel := R.bindings.size, fp := none,
                       name := s!"vc (compileDesign, {R.bindings.size} bindings)" }
  | s => .error s!"sim:{s} is not one of fp | ir | vc"

/-- An fp residual for concrete replay: the route's own, else specialized now. -/
def SymRoute.fpProgram (rt : SymRoute) (sf wf : Nat) (D : DesignCert) : Option Program :=
  match rt.fp with
  | some R => some R
  | none => (MemFused.projectMemFDesignAt sf wf D).toOption

end HDLProof.Kit
