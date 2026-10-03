/-
Bounded -O0 vs -O2 A/B probe.  ONE binary, built twice from IDENTICAL generated
C with every object compiled consistently at an explicit optimization level.

It exercises only the host + TOTAL-variant backend (`mixDriver` + `hwAPVarT`)
on fixtures: the sequential reset/enable/hold/refusal/trace checks, and a tiny
size ladder.  No real design.

Output is one line per case so the two builds can be diffed directly; results
and shapes must be IDENTICAL across builds, only times may differ.
-/
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport
import LeanSemanticPrimitives.Projection.CertLoad

open Compiler Projection Projection.Hw Projection.Runner Projection.ProtoVar
open Projection.Acceptance

def szOf (R : Program) : Nat := (R.funs.map (fun fd => CertLoad.tsize fd.body)).foldl (·+·) 0

/-- A combinational chain: 2 sources, `n` `Op_And` nodes, each reading its
predecessor and source 0. -/
def chainD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := if i = 0 then #[0, 1] else #[2 + i - 1, 0] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := #[]
  memories := #[]


/-- Force and time: the digest is CONSUMED by an IO action inside the interval,
because Lean reorders pure work across `IO.monoMsNow`. -/
def timedF (name : String) (f : Unit → α) (digest : α → Nat) : IO (α × Nat) := do
  let t0 ← IO.monoMsNow
  let v ← IO.lazyPure f
  let d ← IO.lazyPure (fun _ => digest v)
  if d == 1234567891 then IO.println s!"{name} impossible"
  let t1 ← IO.monoMsNow
  return (v, t1 - t0)

def seqChecks : IO Bool := do
  let mut ok := true
  match mkSimVarT 20000 200 seqD with
  | none     => do IO.println "  seq: NO BUNDLE"; return false
  | some sim => do
      for (nm, d, en, rst, q, want) in
          [("reset", (13:Int), (1:Int), (1:Int), (3:Int), (0:Int)),
           ("enabled", 13, 1, 0, 3, 12),
           ("held",    13, 0, 0, 3, 3)] do
        match stepOf sim seqD (allEdges seqD) (seqIn d en rst) (seqSt q) with
        | .ok r =>
            let gotNext := r.nextState.flops == #[mk_bv 4 want]
            let gotRef  := encResult r ==
              encResult (interpretDesign seqD (allEdges seqD) (seqIn d en rst) (seqSt q))
            IO.println s!"  seq {nm}: next=={want} {gotNext}  ref-equal {gotRef}"
            unless gotNext && gotRef do ok := false
        | .error _ => do IO.println s!"  seq {nm}: ERROR"; ok := false
      -- refusal, before the residual runs
      let refused := match stepOf sim seqD (allEdges seqD) (seqIn 13 1 0)
                             { flops := #[], mems := #[] } with
                     | .error .runtimeShape => true
                     | _ => false
      IO.println s!"  seq refusal: runtimeShape {refused}"
      unless refused do ok := false
      -- threaded trace
      let stim := [(allEdges seqD, seqIn 13 1 1), (allEdges seqD, seqIn 13 1 0),
                   (allEdges seqD, seqIn 13 0 0)]
      match stepTrace (stepOf sim) seqD (seqSt 3) stim with
      | .ok rs =>
          let shapes   := rs.map (fun r => r.nextState.flops)
          let shapeOK  := shapes == [#[mk_bv 4 0], #[mk_bv 4 12], #[mk_bv 4 12]]
          let lenOK    := rs.length == 3
          let refEq    := rs.map encResult == (refTrace seqD (seqSt 3) stim).map encResult
          IO.println s!"  seq trace: len3 {lenOK} next-states-ok {shapeOK}  ref-equal {refEq}"
          -- all THREE gate the exit.  Previously only `refEq` did, so a wrong
          -- length or a wrong next-state shape would have printed `false` and
          -- still exited 0.
          unless lenOK && shapeOK && refEq do ok := false
      | .error _ => do IO.println "  seq trace: ERROR"; ok := false
  return ok

def ladder : IO Bool := do
  let mut ok := true
  for n in [8, 16, 32, 64] do
    let D := chainD n
    let (res, tspec) ← timedF "spec"
      (fun _ => mixDriver 200000 2000 hwAPVarT [encDesign D])
      (fun r => match r with | .ok p => szOf p | .error _ => 0)
    match res with
    | .error e => do IO.println s!"  chain {n}: SPECIALIZE FAILED {repr e} ({tspec} ms)"; ok := false
    | .ok R => do
        let (chk, tchk) ← timedF "chk" (fun _ => checkResidual R) (fun o => o.getD 0)
        match chk with
        | none => do IO.println s!"  chain {n}: CHECKER REJECTED"; ok := false
        | some b => do
            let sim : ProjectedSimulator := ⟨D, R, b⟩
            let i := mkInputFor D 1
            let st := mkStateFor D 1
            let (out, texe) ← timedF "exe"
              (fun _ => stepOf sim D (allEdges D) i st)
              (fun o => match o with | .ok r => r.outputs.size | .error _ => 0)
            let refEq := match out with
              | .ok r => encResult r == encResult (interpretDesign D (allEdges D) i st)
              | .error _ => false
            IO.println s!"  chain {n}: terms {szOf R} bound {b} ref-equal {refEq} | \
spec {tspec} ms check {tchk} ms exec {texe} ms"
            unless refEq do ok := false
  return ok

def main : IO UInt32 := do
  IO.println "backend: PROVED mixDriver + hwAPVarT (TOTAL variant), fixtures only"
  let a ← seqChecks
  let b ← ladder
  IO.println s!"RESULT: sequential {a}  ladder {b}"
  return (if a && b then 0 else 1)
