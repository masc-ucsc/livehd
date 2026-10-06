/-
S0.4 -- is the FORK quadratic in DESIGN SIZE?

Every ladder before this one pins an axis.  `chainD` holds sources at 2 and
varies nodes; `srcD` holds nodes at 64 and varies sources.  Neither can see a
cost that depends on the PRODUCT, and neither varies the number of dependency
READS independently of the node count.  The real designs move all three at
once: `rt_alu_gate` is 6,137 sources, 6,597 nodes, 15 operators up to arity 65.

So this probe reports S (sources), N (nodes) and E (dependency reads) as
SEPARATE columns and sweeps them one at a time, plus a combined sweep where
S = N.  `Op_And` has `RequiredArity = none` (HardwareInterpreter.lean:966), so
arity -- and therefore E = N * arity -- is free to vary on its own.

The FORK is the subject: it is what we would ship if `PRes.val` were ever
promoted, and its fitted exponent on the source axis alone is 1.17, not 1.0.
The host is run only at the three smallest points of each sweep, as an anchor;
running it across the grid would cost hours and answer a question P1/P4 already
answered.

EVERY residual, host and fork, is checked OUTSIDE the timed sections: the
fragment checker, and then a run at its OWN checked bound against
`interpretDesign`.  Identical term counts are NOT agreement (23.1).
-/
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport
import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorFast

open Compiler Projection Projection.Hw Projection.Runner Projection.ProtoVar
open Projection.Acceptance

def szOf (R : Program) : Nat := (R.funs.map (fun fd => CertLoad.tsize fd.body)).foldl (·+·) 0

def timedF (name : String) (f : Unit → α) (digest : α → Nat) : IO (α × Nat) := do
  let t0 ← IO.monoMsNow
  let v ← IO.lazyPure f
  let d ← IO.lazyPure (fun _ => digest v)
  if d == 1234567891 then IO.println s!"{name} impossible"
  let t1 ← IO.monoMsNow
  return (v, t1 - t0)

/-- `srcD` generalized so arity -- and hence the dependency-read count E -- is
an independent knob.  Two inputs, the rest constants, exactly as `srcD`, so the
arity-2 column is comparable with the P1/P4 ladders. -/
def gridD (nsrc nnode arity : Nat) : DesignCert where
  sources  := (List.range nsrc).toArray.map (fun i =>
                if i < 2 then .input i 4 else .const 4 (Int.ofNat (i % 16)))
  nodes    := (List.range nnode).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := (List.range arity).toArray.map (fun j => (i + j) % nsrc) })
  outputs  := #[{ slot := nsrc + (nnode - 1), width := 4 }]
  flops    := #[]
  memories := #[]

/-- Fragment checker, then a run at the checker's OWN bound against
`interpretDesign`.  Never inside a timed section. -/
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

/-- One grid point.  `withHost` is false on the larger points: the host is
quadratic in sources (P1, exponent 2.02) and would dominate the wall clock
while telling us nothing new. -/
def point (label : String) (s n a : Nat) (withHost : Bool) : IO Bool := do
  let D := gridD s n a
  let E := n * a
  let (fres, tf) ← timedF "f" (fun _ => ProtoFast.mixDriver 200000 2000 hwAPVar [encDesign D])
    (fun r => match r with | .ok p => szOf p | .error _ => 0)
  match fres with
  | .error e => do
      IO.println s!"  {label} S {s} N {n} E {E} | fork FAILED {repr e}"; return false
  | .ok FR =>
    let (fchk, fref) := checkRun D FR
    if withHost then
      let (hres, th) ← timedF "h" (fun _ => mixDriver 200000 2000 hwAPVarT [encDesign D])
        (fun r => match r with | .ok p => szOf p | .error _ => 0)
      match hres with
      | .error e => do
          IO.println s!"  {label} S {s} N {n} E {E} | host FAILED {repr e}"; return false
      | .ok HR =>
        let (hchk, href) := checkRun D HR
        IO.println s!"  {label} | S {s} | N {n} | E {E} | fork {tf} ms | host {th} ms \
| terms {szOf FR}/{szOf HR} same {szOf FR == szOf HR} \
| fork chk {fchk} ref {fref} | host chk {hchk} ref {href}"
        return (fchk && fref && hchk && href && szOf FR == szOf HR)
    else do
      IO.println s!"  {label} | S {s} | N {n} | E {E} | fork {tf} ms | host -- \
| terms {szOf FR} | fork chk {fchk} ref {fref}"
      return (fchk && fref)

def main : IO UInt32 := do
  IO.println "S0.4 -- two-axis grid.  FORK is the subject; host anchors the small points."
  IO.println "gridD: 2 inputs + (S-2) constants, N Op_And nodes of arity a, E = N*a"
  let mut ok := true
  IO.println "\n[A] SOURCES alone (N=256, arity 2) -- comparable with the P1 source ladder"
  for s in [64, 128, 256, 512, 1024] do
    unless (← point "A" s 256 2 (s <= 256)) do ok := false
  IO.println "\n[B] NODES alone (S=256, arity 2)"
  for n in [64, 128, 256, 512, 1024] do
    unless (← point "B" 256 n 2 (n <= 256)) do ok := false
  IO.println "\n[C] READS alone (S=256, N=256, arity varies) -- E moves, S and N do not"
  for a in [1, 2, 4, 8, 16] do
    unless (← point "C" 256 256 a (a <= 4)) do ok := false
  IO.println "\n[D] COMBINED (S = N, arity 2) -- the axis the real designs move"
  for k in [64, 128, 256, 512, 1024] do
    unless (← point "D" k k 2 (k <= 128)) do ok := false
  IO.println s!"\nALL CHECKS PASSED: {ok}"
  return (if ok then 0 else 1)
