/-
GENERATION-ONLY runner for the covered backend: the PROVED `mixDriver` with
`hwAPVarT`.  It stops after GENERATION AND CHECKING and never touches reference
stimuli, the threaded trace or the control interpreter.

WHAT IT IS NOT.  Success here is `GENERATION OK`, a DIFFERENT and WEAKER
outcome than `total_probe`'s full benchmark pass, and it must never be entered
as one.  `total_probe` and its admission contract are untouched.

WHAT IS MEASURED, and what is deliberately outside the measured interval:

  load        CertIO loadCert -- certificate parse
  support     CertLoad.supportReport, all six SupportedByProjection fields
  specialize  mixDriver sf wf hwAPVarT, forced only to WHNF
  check       Hw.checkResidual, which traverses the residual
  generation end-to-end = load + support + specialize + check

  AFTER the interval, and reported separately, NEVER added to generation:
  `szOf`, the full term-count traversal.  `total_probe`'s `stage` helper calls
  its digest function INSIDE the timed region, so every "specialize" number it
  has ever printed includes a full szOf traversal of the residual.  That is the
  single most important difference between this driver's numbers and those.

THE ARTIFACT.  What generation produces is an IN-MEMORY `Program` -- a Lean
value in this process.  Nothing is serialized and no source file is written.
It is NOT raw native-emitter source and must not be reported as such.

PROOF ACCOUNTING.  `specializeDesign_varT_correct` and `simSound_varT` are
universal conditional results for supported designs and valid runtime shapes,
given the hproj, support and checker premises.  This run CHECKS support and the
fragment checker, and TRUSTS the concrete hproj: `mixDriver` does not
kernel-reduce at nontrivial fuel, so the residual it returns carries no
kernel-checked hproj evidence.  Nothing here discharges a universal premise and
no proof flag is claimed.

Exit: 0 generation and checking OK / 1 checker rejection / 2 setup or
unsupported / 3 out of fuel at the requested budget.
-/
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport
import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Compiler.CertIOV1   -- ADAPTATION(master): DCERT1 loader

open Compiler Projection Projection.Hw Projection.Runner Projection.ProtoVar

def szOf (R : Program) : Nat := (R.funs.map (fun fd => CertLoad.tsize fd.body)).foldl (·+·) 0

def main (args : List String) : IO UInt32 := do
  match args with
  | path :: a :: b :: _ => do
      let sf := (a.toNat?).getD 20000
      let wf := (b.toNat?).getD 200
      let t0 ← IO.monoMsNow
      let D ← CertIO.V1.loadCert path                -- ADAPTATION(master): DCERT1 loader
      let t1 ← IO.monoMsNow
      let sr := CertLoad.supportReport D
      let okAll := sr.wf && sr.memFree && sr.sources && sr.ops && sr.arities && sr.flopClocks
      let t2 ← IO.monoMsNow
      IO.println s!"{path}"
      IO.println s!"  sources {D.sources.size} nodes {D.nodes.size} \
flops {D.flops.size} outputs {D.outputs.size} clocks {D.clocks.size}"
      IO.println s!"  support: wf {sr.wf} memFree {sr.memFree} sources {sr.sources} \
ops {sr.ops} arities {sr.arities} flopClocks {sr.flopClocks} | ALL {okAll}"
      unless okAll do
        IO.eprintln "  NOT SupportedByProjection -- generation refused"
        IO.println s!"  GEN ms: load {t1 - t0}  support {t2 - t1}  TOTAL {t2 - t0}"
        IO.println "  RESULT: GENERATION REFUSED (unsupported)"
        return 2
      IO.println s!"  backend: PROVED mixDriver + hwAPVarT (TOTAL variant), fuel {sf}/{wf}"
      IO.println "  GENERATION ONLY: no reference stimuli, no trace, no control interpreter."
      IO.println "  artifact: an IN-MEMORY Program; nothing is serialized and no source is written."
      (← IO.getStdout).flush
      let t3 ← IO.monoMsNow
      let res ← IO.lazyPure (fun _ => mixDriver sf wf hwAPVarT [encDesign D])
      let t4 ← IO.monoMsNow
      match res with
      | .error .outOfFuel => do
          let t5 ← IO.monoMsNow
          IO.eprintln s!"  projection OUT OF FUEL at {sf}/{wf} -- a failure at THIS budget"
          IO.println s!"  GEN ms: load {t1 - t0}  support {t2 - t1}  specialize {t5 - t3}  TOTAL {t5 - t0}"
          IO.println "  RESULT: GENERATION OUT OF FUEL"
          return 3
      | .error e => do
          let t5 ← IO.monoMsNow
          IO.eprintln s!"  projection FAILED ({repr e})"
          IO.println s!"  GEN ms: load {t1 - t0}  support {t2 - t1}  specialize {t5 - t3}  TOTAL {t5 - t0}"
          IO.println "  RESULT: GENERATION FAILED"
          return 2
      | .ok R => do
          let t5 ← IO.monoMsNow
          let chk := Hw.checkResidual R
          let accepted := chk.isSome
          let bound := chk.getD 0
          let t6 ← IO.monoMsNow
          -- END OF THE MEASURED GENERATION INTERVAL.  Everything below is
          -- reporting and is excluded from it.
          unless accepted do
            IO.eprintln "  checkResidual REJECTED -- failing closed"
            IO.println s!"  GEN ms: load {t1 - t0}  support {t2 - t1}  \
specialize-whnf {t4 - t3}  specialize-force {t5 - t4}  check {t6 - t5}  TOTAL {t6 - t0}"
            IO.println "  RESULT: GENERATION CHECK REJECTED"
            return 1
          IO.println s!"  GEN ms: load {t1 - t0}  support {t2 - t1}  \
specialize-whnf {t4 - t3}  specialize-force {t5 - t4}  check {t6 - t5}  TOTAL {t6 - t0}"
          IO.println s!"  checker bound {bound} (height; proved sufficient, not a minimum)"
          let t7 ← IO.monoMsNow
          let terms := szOf R
          let t8 ← IO.monoMsNow
          IO.println s!"  EXCLUDED FROM GENERATION -- profiling: szOf {terms} terms in {t8 - t7} ms"
          IO.println "  RESULT: GENERATION OK (NOT a full benchmark pass: no stimuli, no trace, no control)"
          return 0
  | _ => do
      IO.eprintln "usage: gen_probe <cert.dcert> <stepFuel> <workFuel>"
      return 2
