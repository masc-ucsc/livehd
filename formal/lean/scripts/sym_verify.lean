/-
`sym_verify` -- prove an unbounded invariant of a design on its projection
residual (M3).

    sym_verify <cert> [!]<output> [asm:[!]<output>] [in:<input>=<value>]... [k:<n>]
               [sf:<n>] [wf:<n>] [solver:<cadical>] [work:<dir>]

Outputs and inputs are named by ordinal or by name (names come from the
`<top>_io.json` next to the certificate). The property holds at a cycle when
the output is NONZERO (or ZERO, with `!`). Assumptions restrict which cycles
count: an output (`asm:`), and inputs held at fixed values (`in:`, e.g.
`in:rst_ni=0` for "reset held"). The initial state is all-zero (flops, memory
words, defaults).

Steps: load and admit the certificate; specialize the memory interpreter to
it (`projectMemDesignAt`); build the BASE and STEP obligations of k-induction
(`Sym.baseObl`, `Sym.stepObl`); write each as DIMACS (`Sym.cnfOf`, the CNF
the checker checks); run the UNTRUSTED solver for a binary LRAT proof; then
evaluate the VERIFIED closed check `Sym.runInvK`, whose acceptance is
`Sym.runInvK_sound`. A satisfiable BASE obligation is a counterexample from
the initial state; a satisfiable STEP obligation only says the property is
not k-inductive.

RESULT lines: `INVARIANT PROVED (k=…)`, `COUNTEREXAMPLE (base)`,
`NOT K-INDUCTIVE (step)`, or a refusal with its reason.
-/
import LeanSemanticPrimitives.Projection.Sym.KInd
import LeanSemanticPrimitives.Projection.CertLoadAny
import Lean.Data.Json

open Compiler Projection Projection.Sym Projection.MemProjection Projection.HwMem
open Std.Tactic.BVDecide

def say (s : String) : IO Unit := IO.println s

/-- The declared width of input ordinal `i` (from its `SourceDesc.input`), or 1. -/
def inputWidthOf (D : DesignCert) (i : Nat) : Nat :=
  (D.sources.toList.findSome? fun
    | .input idx w => if idx == i then some w else none
    | _ => none).getD 1

def inputCount (D : DesignCert) : Nat :=
  D.sources.foldl (fun m s => match s with
    | .input idx _ => max m (idx + 1)
    | .flopQAsync _ _ ri _ _ => max m (ri + 1)
    | _ => m) 0

def shapeOf (D : DesignCert) : Shape :=
  ⟨D.flops.toList.map (·.width), D.memories.toList.map fun md => (md.aw, md.dw)⟩

def zeroState (D : DesignCert) : StateM :=
  { flops := D.flops.map (fun f => mk_bv f.width 0)
  , mems  := D.memories.map (fun md =>
      { words := List.replicate (2 ^ md.aw) (mk_bv md.dw 0), dflt := mk_bv md.dw 0 }) }

/-- A port by ordinal, or by name through the io.json beside the certificate. -/
def resolvePort (cert : String) (kind : String) (tok : String) : IO (Option Nat) := do
  if let some n := tok.toNat? then return some n
  let dir := (System.FilePath.mk cert).parent.getD "."
  let stem := (System.FilePath.mk cert).fileStem.getD ""
  let io := dir / s!"{stem}_io.json"
  unless ← io.pathExists do return none
  match Lean.Json.parse (← IO.FS.readFile io) with
  | .error _ => return none
  | .ok j =>
    match j.getObjValD kind |>.getArr? with
    | .error _ => return none
    | .ok outs =>
      for o in outs do
        if (o.getObjValD "name").getStr?.toOption == some tok then
          if let .ok n := (o.getObjValD "ordinal").getNat? then return some n
      return none

inductive SatAnswer | unsat (proof : ByteArray) | sat | failed (why : String)

/-- Write `φ`'s CNF, run the solver, return its LRAT proof bytes. -/
def solve (solver : String) (work : System.FilePath) (tag : String) (φ : BVLogicalExpr) :
    IO (SatAnswer × Nat × Nat × Nat) := do
  let t0 ← IO.monoMsNow
  let cnf := cnfOf φ
  let dimacs := Std.Sat.CNF.dimacs cnf
  let t1 ← IO.monoMsNow
  let cnfPath := work / s!"{tag}.cnf"
  let lratPath := work / s!"{tag}.lrat"
  IO.FS.writeFile cnfPath dimacs
  let out ← IO.Process.output
    { cmd := solver, args := #[cnfPath.toString, lratPath.toString, "--lrat", "--binary=true",
                               "--quiet", "--shrink=0", "--unsat"] }
  let t2 ← IO.monoMsNow
  let nClauses := cnf.clauses.size
  if out.stdout.startsWith "s UNSATISFIABLE" then
    return (.unsat (← IO.FS.readBinFile lratPath), t1 - t0, t2 - t1, nClauses)
  else if out.stdout.startsWith "s SATISFIABLE" then
    return (.sat, t1 - t0, t2 - t1, nClauses)
  else
    return (.failed s!"solver exit {out.exitCode}: {out.stdout.take 200}{out.stderr.take 200}",
            t1 - t0, t2 - t1, nClauses)

def main (args : List String) : IO UInt32 := do
  match args with
  | cert :: prop :: rest =>
    let opt (k : String) : Option String := rest.findSome? (fun t => (t.dropPrefix? (k ++ ":")).map (·.toString))
    let kd := ((opt "k").bind (·.toNat?)).getD 1
    let sf := ((opt "sf").bind (·.toNat?)).getD 20000
    let wf := ((opt "wf").bind (·.toNat?)).getD 200
    let solver ← match opt "solver" with
      | some s => pure s
      | none => do
          let sys ← IO.appDir
          pure (sys / "cadical").toString
    let work := System.FilePath.mk ((opt "work").getD "sym_work")
    IO.FS.createDirAll work
    let (_, D) ← Projection.CertLoadAny.loadAny cert
    let propOfTok (tok : String) : IO (Option PropSpec) := do
      let (nz, name) := if tok.startsWith "!" then (false, (tok.drop 1).toString) else (true, tok)
      return (← resolvePort cert "outputs" name).map fun n => { out := n, nz }
    let some prop ← propOfTok prop
      | IO.eprintln s!"no output {repr prop}"; return 2
    let asmOut ← match opt "asm" with
      | none => pure none
      | some a => do
          let some p ← propOfTok a | IO.eprintln s!"no output {repr a}"; return 2
          pure (some p)
    let mut asmIn : List (Nat × Int) := []
    for tok in rest.filterMap (fun t => (t.dropPrefix? "in:").map (·.toString)) do
      match tok.splitOn "=" with
      | [nm, vs] =>
          let some k ← resolvePort cert "inputs" nm | IO.eprintln s!"no input {repr nm}"; return 2
          let some v := vs.toInt? | IO.eprintln s!"bad value {repr vs}"; return 2
          asmIn := asmIn ++ [(k, v)]
      | _ => IO.eprintln s!"in:<input>=<value>, got {repr tok}"; return 2
    say s!"{cert}"
    say s!"  shape: sources {D.sources.size} nodes {D.nodes.size} outputs {D.outputs.size} \
flops {D.flops.size} memories {D.memories.size} clocks {D.clocks.size}"
    let pstr (p : PropSpec) : String := s!"output {p.out} {if p.nz then "nonzero" else "zero"}"
    say s!"  property: {pstr prop}{match asmOut with | some a => s!", assuming {pstr a}" | none => ""}\
{if asmIn.isEmpty then "" else s!", assuming inputs {asmIn}"}; k = {kd}; initial state all-zero"
    unless admitsB D do
      say "  RESULT: REFUSED (the memory route does not admit this certificate)"; return 4
    let t0 ← IO.monoMsNow
    let res ← IO.lazyPure (fun _ => projectMemDesignAt sf wf D)
    let R ← match res with
      | .ok R => pure R
      | .error e => do say s!"  RESULT: REFUSED (specialization failed: {repr e})"; return 4
    let some bound := Hw.checkResidual R
      | say "  RESULT: REFUSED (checkResidual rejected the residual)"; return 4
    let t1 ← IO.monoMsNow
    say s!"  residual specialized and checked in {t1 - t0} ms (bound {bound})"
    let nIn := inputCount D
    let sp : InvSpec :=
      { prop, asmOut, asmIn, k := kd, fuel := bound + 16
      , inW := (List.range nIn).map (inputWidthOf D), L := shapeOf D }
    let s0 := zeroState D
    unless sideOK D sp s0 do
      say "  RESULT: REFUSED (side checks: shape, input bound, flop bound, edges, or an input value out of range)"; return 4
    -- base
    let t2 ← IO.monoMsNow
    let φB ← match baseObl D R sp s0 with
      | some φ => pure φ
      | none => do
          let why := match baseRun D R sp s0 with
            | .error m => m | .ok (_, st) => if scopedB st then "?" else "definitions not scoped"
          say s!"  RESULT: REFUSED (base obligation: {why})"; return 4
    let φS ← match stepObl D R sp with
      | some φ => pure φ
      | none => do
          let why := match stepRun D R sp with
            | .error m => m
            | .ok ([], _) => "no step"
            | .ok (s :: _, st) =>
                if !scopedB st then "definitions not scoped"
                else if stateShapeOf s.2.2 != some sp.L then "the next state leaves the layout" else "?"
          say s!"  RESULT: REFUSED (step obligation: {why})"; return 4
    let t3 ← IO.monoMsNow
    say s!"  obligations built in {t3 - t2} ms (symbolic evaluation of {kd} + {kd + 1} steps)"
    let (aB, bbB, satB, ncB) ← solve solver work "base" φB
    say s!"  base: {ncB} clauses; bit-blast+CNF {bbB} ms, solver {satB} ms"
    let pfB ← match aB with
      | .unsat p => pure p
      | .sat => do say s!"  RESULT: COUNTEREXAMPLE (base): the property fails within {kd} cycle(s) of the initial state"; return 1
      | .failed w => do say s!"  RESULT: SOLVER FAILED (base): {w}"; return 5
    let (aS, bbS, satS, ncS) ← solve solver work "step" φS
    say s!"  step: {ncS} clauses; bit-blast+CNF {bbS} ms, solver {satS} ms"
    let pfS ← match aS with
      | .unsat p => pure p
      | .sat => do say s!"  RESULT: NOT K-INDUCTIVE (step) at k = {kd}: a larger k or a stronger property is needed"; return 1
      | .failed w => do say s!"  RESULT: SOLVER FAILED (step): {w}"; return 5
    let t4 ← IO.monoMsNow
    let ok ← IO.lazyPure (fun _ => runInvK sf wf D sp s0 pfB pfS)
    let t5 ← IO.monoMsNow
    say s!"  verified check (runInvK: re-specialize, re-build, LRAT-check both) in {t5 - t4} ms"
    if ok then
      say s!"  RESULT: INVARIANT PROVED (k={kd}) -- runInvK_sound: on every run from the all-zero state, the property holds at every cycle whose assumptions held"
      return 0
    else
      say "  RESULT: CHECK REJECTED (the verified checker did not accept the proofs)"
      return 6
  | _ =>
    IO.eprintln "usage: sym_verify <cert> <property> [asm:<output>] [k:<n>] [sf:<n>] [wf:<n>] [solver:<cadical>] [work:<dir>]"
    return 2
