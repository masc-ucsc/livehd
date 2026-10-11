/-
Real-design runner for the TOTAL-VARIANT backend: the PROVED `mixDriver` with
`hwAPVarT`.  A SEPARATE executable from `proto_probe`, so an in-flight run of
the older backend is never overwritten.

Differences from `proto_probe --host-var-total`, which this replaces:

* it goes through the CHECKED SIMULATOR PATH.  The design is specialized ONCE
  into a `ProjectedSimulator`, the fragment checker must ACCEPT, and every
  cycle runs `stepOf` at the CHECKED BOUND.  The old mode called `runResidAt`
  with a fallback bound when the checker rejected, which interprets at an
  arbitrary budget and is not a checked run at all;
* it FAILS CLOSED: a checker rejection is exit 1, not a fallback;
* full acceptance -- six width-aware stimuli, a threaded trace, and the control
  interpreter -- not three seeds;
* all SIX `SupportedByProjection` fields are printed.

Exit: 0 all checks passed / 1 disagreement or checker rejection / 2 setup or
unsupported / 3 projection ran out of fuel at the requested budget.
-/
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport
import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Compiler.CertIO

open Compiler Projection Projection.Hw Projection.Runner Projection.ProtoVar

def szOf (R : Program) : Nat := (R.funs.map (fun fd => CertLoad.tsize fd.body)).foldl (·+·) 0

/-- One cycle through the CHECKED simulator, at `sim.bound`. -/
def runStep (sim : ProjectedSimulator) (D : DesignCert) (e : ClockEdges)
    (i : RuntimeInput) (s : RuntimeState) : Outcome :=
  outcomeOfSim (stepOf sim D e i s)

/-- A threaded trace: each side carries its OWN state forward. -/
def traceBoth (sim : ProjectedSimulator) (D : DesignCert) (n seed : Nat) :
    IO (Bool × Nat) := do
  let mut stRef := mkStateFor D seed
  let mut stRes := mkStateFor D seed
  let mut agree := true
  let mut seen : List Val := [encState stRef]
  for k in List.range n do
    let e := edgeSchedule D k
    let i := mkInputFor D (seed + k)
    let want := interpretDesign D e i stRef
    let got  := runStep sim D e i stRes
    unless Outcome.agree got (Outcome.ok want) do agree := false
    let key := encState want.nextState
    unless seen.any (· == key) do seen := key :: seen
    stRef := want.nextState
    match got with
    | .ok g => stRes := g.nextState
    | _     => agree := false
  return (agree, seen.length)

def main (args : List String) : IO UInt32 := do
  match args with
  | path :: a :: b :: _ => do
      let D ← CertIO.loadCert path
      let sf := (a.toNat?).getD 20000
      let wf := (b.toNat?).getD 200
      IO.println s!"{path}"
      IO.println s!"  sources {D.sources.size} nodes {D.nodes.size} \
flops {D.flops.size} outputs {D.outputs.size} clocks {D.clocks.size}"
      let sr := CertLoad.supportReport D
      IO.println s!"  support: wf {sr.wf} memFree {sr.memFree} sources {sr.sources} \
ops {sr.ops} arities {sr.arities} flopClocks {sr.flopClocks} | ALL {sr.allOK}"
      unless sr.allOK do
        IO.eprintln "  NOT SupportedByProjection -- refusing to compare"
        return 2
      IO.println s!"  backend: PROVED mixDriver + hwAPVarT (TOTAL variant), fuel {sf}/{wf}"
      IO.println "  NOTE: this is a native EXECUTION.  The concrete hproj is NOT \
kernel-certified; mixDriver does not kernel-reduce at nontrivial fuel."
      (← IO.getStdout).flush
      -- ONE specialization.  An earlier version called `mkSimVarT`, which
      -- collapses projection and checking into an `Option`, and then re-ran
      -- `mixDriver` to find out WHICH had failed -- doubling the cost of a
      -- failed hour-long projection.  Both results are retained from a single
      -- run instead, and there is no fallback bound anywhere.
      let (res, tms) ← stage "specialize"
        (fun (r : Except MixError Program) => match r with
                                              | .ok p    => szOf p
                                              | .error _ => 0)
        (fun _ => mixDriver sf wf hwAPVarT [encDesign D])
      match res with
      | .error .outOfFuel => do
          IO.eprintln s!"  projection OUT OF FUEL at {sf}/{wf} after {tms} ms -- a \
failure at THIS budget, not impossibility"
          return 3
      | .error e => do
          IO.eprintln s!"  projection FAILED ({repr e}) after {tms} ms"
          return 2
      | .ok R => do
          let (chk, tchk) ← stage "checkResidual"
            (fun (o : Option Nat) => o.getD 0) (fun _ => Hw.checkResidual R)
          match chk with
          | none => do
              IO.eprintln "  checkResidual REJECTED -- failing closed; no execution \
at a fallback bound"
              return 1
          | some b => do
            let sim : ProjectedSimulator := ⟨D, R, b⟩

            IO.println s!"  residual {szOf sim.prog} terms, checker bound {sim.bound} \
  (height; proved sufficient, not a minimum)"
            let mut bad := false
            let mut tRef := 0
            let mut tRes := 0
            for seed in [0, 1, 2, 3, 4, 7] do
              let e := allEdges D
              let i := mkInputFor D seed
              let st := mkStateFor D seed
              let (want, x) ← stage s!"ref {seed}"
                (fun (r : RuntimeResult) => r.outputs.size) (fun _ => interpretDesign D e i st)
              let (got, y) ← stage s!"step {seed}" outcomeDigest (fun _ => runStep sim D e i st)
              tRef := tRef + x; tRes := tRes + y
              let ok := Outcome.agree got (Outcome.ok want)
              IO.println s!"  seed {seed}: {got.tag}  matches interpretDesign {ok}"
              unless ok do bad := true
            let (ag, nst) ← traceBoth sim D 4 0
            IO.println s!"  trace: 4 cycles, independent states -- agrees {ag}  \
  states-reached {nst}  flops {D.flops.size}"
            unless ag do bad := true
            if D.flops.size > 0 && nst <= 2 then
              IO.println s!"  coverage: INCONCLUSIVE -- {nst} state(s); correct for a \
  toggler or hold test, and NOT evidence of unreachable behaviour"
            -- control: the variant INTERPRETER on the same stimulus
            let ctlW := encResult (interpretDesign D (allEdges D)
                          (mkInputFor D 0) (mkStateFor D 0))
            let (ctl, tCtl) ← stage "control interpreter"
              (fun (o : EvalResult) => match o with | .value _ => 1 | _ => 0)
              (fun _ => evalFuel 4000000 hwPVarT []
                (.call hwPVarT.entry
                  [.lit (encDesign D), .lit (encEdges (allEdges D)),
                   .lit (encInput (mkInputFor D 0)), .lit (encState (mkStateFor D 0))]))
            let ctlOk : Bool := match ctl with
              | .value v => v == ctlW
              | _        => false
            IO.println s!"  control: variant interpreter matches interpretDesign {ctlOk}"
            unless ctlOk do bad := true
            IO.println s!"  specialize {tms} ms  checkResidual {tchk} ms (ONE specialization; the checker result is kept from it, not recomputed)"
            IO.println s!"  STAGE TIMES ms: specialize+check {tms + tchk}  reference-runs {tRef} (6)  \
step-runs {tRes} (6)  control {tCtl}"
            return (if bad then 1 else 0)
    | _ => do
        IO.eprintln "usage: total_probe CERT.dcert STEPFUEL WORKFUEL"
        return 64
