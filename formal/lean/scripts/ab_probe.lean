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
import LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorFast

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

/-- HYPOTHESIS (PERFORMANCE_INVESTIGATION_PLAN H3): on the PROVED `mixDriver`,
repeated preparation / environment shifting / rebuilding dominates, and the
fork's `PRes.val` + zero-shift changes remove exactly that.  PREDICTION: the
fork is several times faster at every ladder size, and the gap WIDENS with n
because the per-node spine work grows with the spine.

CONTROL: both backends run in ONE process, from the SAME source, build flags,
input and budgets.  The only documented difference is the interpreter --
host uses `hwAPVarT` (built from the TOTAL `rewrittenT`), fork uses `hwAPVar`
(built from the PARTIAL `goInline`).  `proto_probe --rewrite-agree` shows the
two transforms produce the SAME `main` body by exact `BEq`, so the interpreters
agree on this input; that is evidence, not a theorem. -/
def ladder : IO Bool := do
  let mut ok := true
  IO.println "  n | HOST mixDriver+hwAPVarT | FORK ProtoFast+hwAPVar | ratio | terms"
  for n in [8, 16, 32, 64, 128] do
    let D := chainD n
    let (hres, th) ← timedF "host"
      (fun _ => mixDriver 200000 2000 hwAPVarT [encDesign D])
      (fun r => match r with | .ok p => szOf p | .error _ => 0)
    let (fres, tf) ← timedF "fork"
      (fun _ => ProtoFast.mixDriver 200000 2000 hwAPVar [encDesign D])
      (fun r => match r with | .ok p => szOf p | .error _ => 0)
    match hres, fres with
    | .ok HR, .ok FR => do
        let sameSize := szOf HR == szOf FR
        -- both must still agree with the reference semantics
        let i := mkInputFor D 1
        let st := mkStateFor D 1
        let want := encResult (interpretDesign D (allEdges D) i st)
        let runIt := fun (R : Program) =>
          match Hw.checkResidual R with
          | none   => false
          | some b => match evalFuel b R []
                        (.call R.entry [.lit (encEdges (allEdges D)), .lit (encInput i),
                                        .lit (encState st)]) with
                      | .value v => (decResult v).map encResult == some want
                      | _        => false
        let hok := runIt HR
        let fok := runIt FR
        IO.println s!"  {n} | host {th} ms | fork {tf} ms | terms {szOf HR}/{szOf FR} \
same-size {sameSize} | host-ref {hok} fork-ref {fok}"
        unless sameSize && hok && fok do ok := false
    | a, b => do
        IO.println s!"  {n}: host-ok {a.toOption.isSome} fork-ok {b.toOption.isSome}"
        ok := false
  return ok

/-- SOURCE ladder.  `chainD` holds sources at 2, so it varies only node count.
Section 7 of PHASE6_PERF measured that SOURCE count, not node count, drives the
environment-spine cost, and the real designs have thousands of sources.  This
holds nodes at 64 and varies sources, which is the axis the chain ladder cannot
see. -/
def srcD (nsrc nnode : Nat) : DesignCert where
  sources  := (List.range nsrc).toArray.map (fun i =>
                if i < 2 then .input i 4 else .const 4 (Int.ofNat (i % 16)))
  nodes    := (List.range nnode).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := #[i % nsrc, (i + 1) % nsrc] })
  outputs  := #[{ slot := nsrc + (nnode - 1), width := 4 }]
  flops    := #[]
  memories := #[]

def srcLadder : IO Bool := do
  let mut ok := true
  IO.println "  nsrc (nodes=64) | HOST | FORK | terms"
  for ns in [16, 64, 256, 1024, 4096] do
    let D := srcD ns 64
    let (hres, th) ← timedF "h" (fun _ => mixDriver 200000 2000 hwAPVarT [encDesign D])
      (fun r => match r with | .ok p => szOf p | .error _ => 0)
    let (fres, tf) ← timedF "f" (fun _ => ProtoFast.mixDriver 200000 2000 hwAPVar [encDesign D])
      (fun r => match r with | .ok p => szOf p | .error _ => 0)
    match hres, fres with
    | .ok HR, .ok FR => do
        -- Semantic checks OUTSIDE the timed sections, for BOTH outputs.
        -- Identical residual SIZE is not agreement: two different programs can
        -- have the same term count.  Each residual must pass the fragment
        -- checker and then match `interpretDesign` at its own checked bound.
        let i := mkInputFor D 1
        let st := mkStateFor D 1
        let want := encResult (interpretDesign D (allEdges D) i st)
        let checkRun := fun (R : Program) =>
          match Hw.checkResidual R with
          | none   => (false, false)
          | some b => (true, match evalFuel b R []
                               (.call R.entry [.lit (encEdges (allEdges D)),
                                               .lit (encInput i), .lit (encState st)]) with
                             | .value v => (decResult v).map encResult == some want
                             | _        => false)
        let (hchk, href) := checkRun HR
        let (fchk, fref) := checkRun FR
        IO.println s!"  {ns} | host {th} ms | fork {tf} ms | terms {szOf HR}/{szOf FR} \
same-size {szOf HR == szOf FR} | host chk {hchk} ref {href} | fork chk {fchk} ref {fref}"
        unless szOf HR == szOf FR && hchk && href && fchk && fref do ok := false
    | a, b => do
        IO.println s!"  {ns}: host-ok {a.toOption.isSome} fork-ok {b.toOption.isSome}"; ok := false
  return ok

/-- P2: ONE case -- the PROVED path at 4,096 sources -- so a `perf` sample is
not diluted by the other ladder points or by the fork runs. -/
def p2Only : IO UInt32 := do
  let D := srcD 4096 64
  IO.println "P2: PROVED mixDriver + hwAPVarT, srcD 4096 sources / 64 nodes"
  let t0 ← IO.monoMsNow
  let r ← IO.lazyPure (fun _ => mixDriver 200000 2000 hwAPVarT [encDesign D])
  let d ← IO.lazyPure (fun _ => match r with | .ok p => szOf p | .error _ => 0)
  IO.println s!"  residual terms {d}"
  let t1 ← IO.monoMsNow
  IO.println s!"  specialize {t1 - t0} ms"
  match r with
  | .error e => do IO.println s!"  FAILED {repr e}"; return 1
  | .ok R => do
      let i := mkInputFor D 1
      let st := mkStateFor D 1
      let want := encResult (interpretDesign D (allEdges D) i st)
      match Hw.checkResidual R with
      | none => do IO.println "  checker REJECTED"; return 1
      | some b =>
          let ok := match evalFuel b R []
                      (.call R.entry [.lit (encEdges (allEdges D)), .lit (encInput i),
                                      .lit (encState st)]) with
                    | .value v => (decResult v).map encResult == some want
                    | _        => false
          IO.println s!"  checker bound {b}  reference-equal {ok}"
          return (if ok then 0 else 1)

def main (args : List String) : IO UInt32 := do
  if args.contains "p2" then p2Only else do
  IO.println "backend: PROVED mixDriver + hwAPVarT (TOTAL variant), fixtures only"
  let a ← seqChecks
  let b ← ladder
  let c ← srcLadder
  IO.println s!"RESULT: sequential {a}  node-ladder {b}  source-ladder {c}"
  return (if a && b && c then 0 else 1)
