/-
S2a -- the restricted lookup summary (`Proto/PartialEvaluatorSummary.lean`)
against S1, the PROVED specializer.  Specification: certio/PHASE6_PERF.md 35.

  s2a_probe grid
      S1 and the prototype in ONE process on the S0.4 two-axis grid plus the
      all-dynamic fixture.  Per point: both times, residual term counts, a
      digest of each residual's `repr` (identity), and -- OUTSIDE the timed
      sections -- the fragment checker and `interpretDesign` at each residual's
      own checked bound.

  s2a_probe cert PATH (s1|proto) [sf wf]
      ONE backend per process, so `/usr/bin/time` RSS belongs to that backend.
      Specialize once (timed), print the digest, then the CHECKED SIMULATOR PATH
      exactly as `total_probe`: the checker must accept, six width-aware stimuli
      at the checked bound, a threaded four-cycle trace.  The control
      interpreter is omitted: it does not depend on the specializer.

Same interpreter (`hwAPVarT`), budgets and flags for both backends.
Exit: 0 pass / 1 disagreement or checker rejection / 2 setup / 3 out of fuel.
-/
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport
import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorSummary
import LeanSemanticPrimitives.Compiler.CertIO

open Compiler Projection Projection.Hw Projection.Runner Projection.ProtoVar

def szOf (R : Program) : Nat := (R.funs.map (fun fd => CertLoad.tsize fd.body)).foldl (·+·) 0
/-- A STRUCTURAL digest, linear in the residual.  An earlier version hashed
`toString (repr R)`: the residual's `let` chain nests ~16k deep, pretty-printing
indents every level, and that made the digest quadratic -- 81 GB peak RSS on
`decoder` -- which also polluted every RSS and wall figure taken in the same
process.  This fold touches each node once. -/
partial def digV : Val → UInt64
  | .int i      => mixHash 11 (hash i)
  | .bool b     => mixHash 12 (hash b)
  | .nil        => 13
  | .cons a b   => mixHash 14 (mixHash (digV a) (digV b))
  | .ctor k vs  => mixHash 15 (vs.foldl (fun h v => mixHash h (digV v)) (hash k))

partial def digT : Term → UInt64
  | .lit v       => mixHash 1 (digV v)
  | .var i       => mixHash 2 (hash i)
  | .letIn e b   => mixHash 3 (mixHash (digT e) (digT b))
  | .ite c a b   => mixHash 4 (mixHash (digT c) (mixHash (digT a) (digT b)))
  | .prim p ts   => mixHash 5 (ts.foldl (fun h t => mixHash h (digT t)) (hash (primCode p)))
  | .ctorT k ts  => mixHash 6 (ts.foldl (fun h t => mixHash h (digT t)) (hash k))
  | .caseT s as  => mixHash 7 (as.foldl (fun h (a, b, t) =>
                      mixHash h (mixHash (hash a) (mixHash (hash b) (digT t)))) (digT s))
  | .call f ts   => mixHash 8 (ts.foldl (fun h t => mixHash h (digT t)) (hash f))

def digest (R : Program) : UInt64 :=
  R.funs.foldl (fun h fd => mixHash h (mixHash (hash fd.arity) (digT fd.body))) (hash R.entry)

/-- VERBATIM from `scripts/total_probe.lean`. -/
def runStep (sim : ProjectedSimulator) (D : DesignCert) (e : ClockEdges)
    (i : RuntimeInput) (s : RuntimeState) : Outcome :=
  outcomeOfSim (stepOf sim D e i s)

/-- VERBATIM from `scripts/total_probe.lean`. -/
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

def specS1 (sf wf : Nat) (D : DesignCert) : Except MixError Program :=
  mixDriver sf wf hwAPVarT [encDesign D]
def specProto (sf wf : Nat) (D : DesignCert) : Except ProtoSum.MixError Program :=
  ProtoSum.mixDriver sf wf hwAPVarT [encDesign D]

/-- `srcD` generalised, as in `scripts/s0_grid.lean`; `dynamic` makes every
source an input. -/
def gridD (nsrc nnode arity : Nat) (dynamic : Bool := false) : DesignCert where
  sources  := (List.range nsrc).toArray.map (fun i =>
                if dynamic || i < 2 then .input i 4 else .const 4 (Int.ofNat (i % 16)))
  nodes    := (List.range nnode).toArray.map (fun i =>
                { op := .Op_And, width := 4
                , deps := (List.range arity).toArray.map (fun j => (i + j) % nsrc) })
  outputs  := #[{ slot := nsrc + (nnode - 1), width := 4 }]
  flops    := #[]
  memories := #[]

/-- Fragment checker, then a run at that residual's OWN checked bound against
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

def point (label : String) (s n a : Nat) (dyn : Bool := false) : IO Bool := do
  let D := gridD s n a dyn
  let (r1, t1) ← stage "s1" (fun (r : Except MixError Program) =>
                    match r with | .ok p => szOf p | .error _ => 0) (fun _ => specS1 200000 2000 D)
  let (r2, t2) ← stage "proto" (fun (r : Except ProtoSum.MixError Program) =>
                    match r with | .ok p => szOf p | .error _ => 0) (fun _ => specProto 200000 2000 D)
  match r1, r2 with
  | .ok A, .ok B =>
    let (c1, f1) := checkRun D A
    let (c2, f2) := checkRun D B
    let same := digest A == digest B
    IO.println s!"  {label} | S {s} N {n} E {n * a} | S1 {t1} ms | proto {t2} ms | terms {szOf A}/{szOf B} \
identical {same} | S1 chk {c1} ref {f1} | proto chk {c2} ref {f2}"
    return (same && c1 && f1 && c2 && f2)
  | _, _ => do IO.println s!"  {label} | a backend FAILED"; return false

def grid : IO UInt32 := do
  IO.println "S2a grid: S1 (proved) vs prototype (summary), same process, same budgets"
  let mut ok := true
  IO.println "[A] S varies, N=256, arity 2"
  for s in [64, 128, 256, 512, 1024] do unless (← point "A" s 256 2) do ok := false
  IO.println "[B] N varies, S=256, arity 2"
  for n in [64, 128, 256, 512, 1024] do unless (← point "B" 256 n 2) do ok := false
  IO.println "[C] E varies, S=N=256"
  for a in [1, 2, 4, 8, 16] do unless (← point "C" 256 256 a) do ok := false
  IO.println "[D] S = N, arity 2"
  for k in [64, 128, 256, 512, 1024, 2048] do unless (← point "D" k k 2) do ok := false
  IO.println "[dyn] every source dynamic, N=64"
  for s in [16, 64, 128, 256] do unless (← point "dyn" s 64 2 true) do ok := false
  IO.println s!"ALL CHECKS PASSED: {ok}"
  return (if ok then 0 else 1)

def accept (D : DesignCert) (R : Program) (tms : Nat) : IO UInt32 := do
  let (chk, tchk) ← stage "checkResidual" (fun (o : Option Nat) => o.getD 0) (fun _ => Hw.checkResidual R)
  match chk with
  | none => do IO.eprintln "  checkResidual REJECTED -- failing closed"; return 1
  | some b => do
    let sim : ProjectedSimulator := ⟨D, R, b⟩
    IO.println s!"  residual {szOf R} terms, digest {digest R}, checker bound {b}"
    let mut bad := false
    for seed in [0, 1, 2, 3, 4, 7] do
      let e := allEdges D
      let i := mkInputFor D seed
      let st := mkStateFor D seed
      let want := interpretDesign D e i st
      let got := runStep sim D e i st
      let ok := Outcome.agree got (Outcome.ok want)
      IO.println s!"  seed {seed}: {got.tag}  matches interpretDesign {ok}"
      unless ok do bad := true
    let (ag, nst) ← traceBoth sim D 4 0
    IO.println s!"  trace: 4 cycles -- agrees {ag}  states-reached {nst}  flops {D.flops.size}"
    unless ag do bad := true
    IO.println s!"  STAGE TIMES ms: specialize {tms}  checkResidual {tchk}"
    return (if bad then 1 else 0)

def cert (path backend : String) (sf wf : Nat) : IO UInt32 := do
  let D ← CertIO.loadCert path
  IO.println s!"{path}"
  IO.println s!"  sources {D.sources.size} nodes {D.nodes.size} flops {D.flops.size}"
  let sr := CertLoad.supportReport D
  unless sr.allOK do IO.eprintln "  NOT SupportedByProjection"; return 2
  IO.println s!"  backend: {backend}  fuel {sf}/{wf}  interpreter hwAPVarT"
  (← IO.getStdout).flush
  if backend == "s1" then
    let (r, t) ← stage "specialize" (fun (r : Except MixError Program) =>
                    match r with | .ok p => szOf p | .error _ => 0) (fun _ => specS1 sf wf D)
    match r with
    | .ok R => accept D R t
    | .error .outOfFuel => do IO.eprintln "  OUT OF FUEL"; return 3
    | .error e => do IO.eprintln s!"  FAILED {repr e}"; return 2
  else
    let (r, t) ← stage "specialize" (fun (r : Except ProtoSum.MixError Program) =>
                    match r with | .ok p => szOf p | .error _ => 0) (fun _ => specProto sf wf D)
    match r with
    | .ok R => accept D R t
    | .error .outOfFuel => do IO.eprintln "  OUT OF FUEL"; return 3
    | .error e => do IO.eprintln s!"  FAILED {repr e}"; return 2

def main (args : List String) : IO UInt32 := do
  match args with
  | ["grid"] => grid
  | ["cert", p, b] => cert p b 200000 2000
  | ["cert", p, b, a, c] => cert p b ((a.toNat?).getD 200000) ((c.toNat?).getD 2000)
  | _ => do IO.eprintln "usage: s2a_probe grid | cert PATH (s1|proto) [sf wf]"; return 2
