/-
S1 -- does promoting `PRes.val` change any residual, and what does it buy?

ONE source, built TWICE: against pristine HEAD (the two S1 files restored from
git) and against S1.  Each build prints, per fixture point, the PROVED host's
residual term count and a DIGEST of the residual's `repr`, plus the unchanged
fork as an in-process timing control.  Diffing the two outputs answers:

  * IDENTITY -- the design claim is that `PRes.ofPVal` keeps every residual
    byte-identical to the old host's.  Equal digests at every point is the
    evidence; equal term counts alone would not be (23.1).
  * SPEED    -- host time HEAD vs S1, with the fork column showing machine
    state did not move between the two runs.

Every residual is ALSO checked, outside the timed section, by the fragment
checker and against `interpretDesign` at its own checked bound.
-/
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport
import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorFast

open Compiler Projection Projection.Hw Projection.Runner Projection.ProtoVar
open Projection.Acceptance

def szOf (R : Program) : Nat := (R.funs.map (fun fd => CertLoad.tsize fd.body)).foldl (·+·) 0
def digest (R : Program) : UInt64 := hash (toString (repr R))

def timedF (name : String) (f : Unit → α) (dg : α → Nat) : IO (α × Nat) := do
  let t0 ← IO.monoMsNow
  let v ← IO.lazyPure f
  let d ← IO.lazyPure (fun _ => dg v)
  if d == 1234567891 then IO.println s!"{name} impossible"
  let t1 ← IO.monoMsNow
  return (v, t1 - t0)

def chainD (n : Nat) : DesignCert where
  sources  := #[.input 0 4, .const 4 12]
  nodes    := (List.range n).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := if i = 0 then #[0, 1] else #[2 + i - 1, 0] })
  outputs  := #[{ slot := 2 + (n - 1), width := 4 }]
  flops    := #[]
  memories := #[]

def gridD (nsrc nnode arity : Nat) (dynamic : Bool := false) : DesignCert where
  sources  := (List.range nsrc).toArray.map (fun i =>
                if dynamic || i < 2 then .input i 4 else .const 4 (Int.ofNat (i % 16)))
  nodes    := (List.range nnode).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := (List.range arity).toArray.map (fun j => (i + j) % nsrc) })
  outputs  := #[{ slot := nsrc + (nnode - 1), width := 4 }]
  flops    := #[]
  memories := #[]

def checkRun (D : DesignCert) (R : Program) : Bool × Bool :=
  let i    := mkInputFor D 1
  let st   := mkStateFor D 1
  let want := encResult (interpretDesign D (allEdges D) i st)
  match Hw.checkResidual R with
  | none   => (false, false)
  | some b => (true, match evalFuel b R []
                       (.call R.entry [.lit (encEdges (allEdges D)),
                                       .lit (encInput i), .lit (encState st)]) with
                     | .value v => (decResult v).map encResult == some want
                     | _        => false)

def point (label : String) (D : DesignCert) : IO Bool := do
  let (hres, th) ← timedF "h" (fun _ => mixDriver 200000 2000 hwAPVarT [encDesign D])
    (fun r => match r with | .ok p => szOf p | .error _ => 0)
  let (fres, tf) ← timedF "f" (fun _ => ProtoFast.mixDriver 200000 2000 hwAPVar [encDesign D])
    (fun r => match r with | .ok p => szOf p | .error _ => 0)
  match hres, fres with
  | .ok HR, .ok FR =>
    let (c, r) := checkRun D HR
    IO.println s!"  {label} | HOST terms {szOf HR} digest {digest HR} | host {th} ms | fork {tf} ms | chk {c} ref {r}"
    return (c && r)
  | .error e, _ => do IO.println s!"  {label} | HOST FAILED {repr e}"; return false
  | _, .error e => do IO.println s!"  {label} | fork FAILED {repr e}"; return false

def main : IO UInt32 := do
  let mut ok := true
  IO.println "[chainD] nodes vary, 2 sources"
  for n in [8, 16, 32, 64, 128] do
    unless (← point s!"chainD n={n}" (chainD n)) do ok := false
  IO.println "[srcD] nodes 64, sources vary (2 inputs, rest constant) -- the P1/P4 ladder"
  for s in [16, 64, 256, 1024, 4096] do
    unless (← point s!"srcD nsrc={s}" (gridD s 64 2)) do ok := false
  IO.println "[gridD] S = N, arity 2 -- S0.4 sweep D"
  for k in [64, 128, 256, 512] do
    unless (← point s!"gridD S=N={k}" (gridD k k 2)) do ok := false
  IO.println "[gridD] arity 8, S = N = 128 -- high fan-in"
  unless (← point "gridD S=N=128 a=8" (gridD 128 128 8)) do ok := false
  IO.println "[dynSrcD] EVERY source dynamic -- the shape where `lets` packages are live"
  for s in [16, 64, 128] do
    unless (← point s!"dynSrcD nsrc={s}" (gridD s 64 2 true)) do ok := false
  IO.println s!"ALL CHECKS PASSED: {ok}"
  return (if ok then 0 else 1)
