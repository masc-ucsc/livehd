/-
`sym_refine` -- prove that an implementation design refines a specification
design up to stuttering, by k-induction on their projection residuals (M3 S6).

    sym_refine <impl.dcert> <spec.dcert> pair:<implOut>=<specOut>... [adv:[!]<implOut>]
               [map:<specInput>=<implInput>]... [asm:[!]<implOut>] [in:<implInput>=<value>]...
               [k:<n>] [sf:<n>] [wf:<n>] [solver:<cadical>] [work:<dir>]

The implementation steps every cycle; the specification steps only on cycles
where the witness output `adv` fires (no `adv`: lockstep) and holds
otherwise. Specification input `s` is implementation input `i` for every
`map:s=i` (unmapped specification inputs default to the implementation input
of the same NAME). On every witnessed cycle each `pair:` must agree. Both
designs start all-zero. Verdict: the verified closed check `Sym.runRefKOf`
(`runRefKOf_sound`) against LRAT proofs from the untrusted solver.
-/
import LeanSemanticPrimitives.Projection.Sym.Refine
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


def portNames (cert : String) (kind : String) : IO (List (String × Nat)) := do
  let dir := (System.FilePath.mk cert).parent.getD "."
  let stem := (System.FilePath.mk cert).fileStem.getD ""
  let io := dir / s!"{stem}_io.json"
  unless ← io.pathExists do return []
  match Lean.Json.parse (← IO.FS.readFile io) with
  | .error _ => return []
  | .ok j =>
    match j.getObjValD kind |>.getArr? with
    | .error _ => return []
    | .ok arr => return arr.toList.filterMap fun o =>
        match (o.getObjValD "name").getStr?, (o.getObjValD "ordinal").getNat? with
        | .ok n, .ok k => some (n, k)
        | _, _ => none

def main (args : List String) : IO UInt32 := do
  match args with
  | ci :: cs :: rest =>
    let opt (k : String) : Option String := rest.findSome? (fun t => (t.dropPrefix? (k ++ ":")).map (·.toString))
    let opts (k : String) : List String := rest.filterMap (fun t => (t.dropPrefix? (k ++ ":")).map (·.toString))
    let kd := ((opt "k").bind (·.toNat?)).getD 1
    let sf := ((opt "sf").bind (·.toNat?)).getD 20000
    let wf := ((opt "wf").bind (·.toNat?)).getD 200
    let solver ← match opt "solver" with
      | some s => pure s
      | none => do pure ((← IO.appDir) / "cadical").toString
    let work := System.FilePath.mk ((opt "work").getD "sym_work")
    IO.FS.createDirAll work
    let (_, DI) ← Projection.CertLoadAny.loadAny ci
    let (_, DS) ← Projection.CertLoadAny.loadAny cs
    let propOfTok (cert tok : String) : IO (Option PropSpec) := do
      let (nz, name) := if tok.startsWith "!" then (false, (tok.drop 1).toString) else (true, tok)
      return (← resolvePort cert "outputs" name).map fun n => { out := n, nz }
    let mut pairs : List (Nat × Nat) := []
    for tok in opts "pair" do
      match tok.splitOn "=" with
      | [a, b] =>
          let some ia ← resolvePort ci "outputs" a | IO.eprintln s!"no impl output {a}"; return 2
          let some ib ← resolvePort cs "outputs" b | IO.eprintln s!"no spec output {b}"; return 2
          pairs := pairs ++ [(ia, ib)]
      | _ => IO.eprintln s!"pair:<implOut>=<specOut>, got {tok}"; return 2
    let adv ← match opt "adv" with
      | none => pure none
      | some a => do
          let some p ← propOfTok ci a | IO.eprintln s!"no impl output {a}"; return 2
          pure (some p)
    let asmOut ← match opt "asm" with
      | none => pure none
      | some a => do
          let some p ← propOfTok ci a | IO.eprintln s!"no impl output {a}"; return 2
          pure (some p)
    let mut asmIn : List (Nat × Int) := []
    for tok in opts "in" do
      match tok.splitOn "=" with
      | [nm, vs] =>
          let some k ← resolvePort ci "inputs" nm | IO.eprintln s!"no impl input {nm}"; return 2
          let some v := vs.toInt? | IO.eprintln s!"bad value {vs}"; return 2
          asmIn := asmIn ++ [(k, v)]
      | _ => IO.eprintln s!"in:<input>=<value>, got {tok}"; return 2
    -- the input map: explicit map: entries, else the impl input of the same name
    let specIns ← portNames cs "inputs"
    let implIns ← portNames ci "inputs"
    let nS := inputCount DS
    let mut inMap : List Nat := []
    for s in List.range nS do
      let sname := (specIns.find? (·.2 == s)).map (·.1)
      let explicit := (opts "map").findSome? fun tok => match tok.splitOn "=" with
        | [a, b] => if sname == some a || a == toString s then some b else none
        | _ => none
      let target := explicit.orElse (fun _ => sname)
      let some tn := target | IO.eprintln s!"spec input {s} has no name and no map:"; return 2
      let some ti ← resolvePort ci "inputs" tn | IO.eprintln s!"spec input {s}: no impl input {tn}"; return 2
      inMap := inMap ++ [ti]
    let _ := implIns
    say s!"impl {ci}\nspec {cs}"
    say s!"  pairs {pairs}, adv {repr adv}, input map {inMap}, k = {kd}; both start all-zero"
    unless admitsB DI && admitsB DS do
      say "  RESULT: REFUSED (the memory route does not admit a certificate)"; return 4
    let t0 ← IO.monoMsNow
    let RI ← match projectMemDesignAt sf wf DI with
      | .ok R => pure R | .error e => do say s!"  RESULT: REFUSED (impl specialization: {repr e})"; return 4
    let RS ← match projectMemDesignAt sf wf DS with
      | .ok R => pure R | .error e => do say s!"  RESULT: REFUSED (spec specialization: {repr e})"; return 4
    let some bI := Hw.checkResidual RI | say "  RESULT: REFUSED (impl residual check)"; return 4
    let some bS := Hw.checkResidual RS | say "  RESULT: REFUSED (spec residual check)"; return 4
    let t1 ← IO.monoMsNow
    say s!"  residuals specialized and checked in {t1 - t0} ms"
    let sy : RefSys := { DI, DS, sI0 := zeroState DI, sS0 := zeroState DS, inMap, adv }
    let sp : RefSpec :=
      { pairs, adv, inMap, asmOut, asmIn, k := kd, fuelI := bI + 16, fuelS := bS + 16
      , inW := (List.range (inputCount DI)).map (inputWidthOf DI), LI := shapeOf DI, LS := shapeOf DS }
    unless refSideOK sy sp do
      say "  RESULT: REFUSED (side checks)"; return 4
    let φB ← match refBaseObl sy RI RS sp with
      | some φ => pure φ
      | none => do
          let why := match refBaseRun sy RI RS sp with | .error m => m | .ok _ => "definitions not scoped"
          say s!"  RESULT: REFUSED (base obligation: {why})"; return 4
    let φS ← match refStepObl sy RI RS sp with
      | some φ => pure φ
      | none => do
          let why := match refStepRun sy RI RS sp with
            | .error m => m | .ok _ => "scope or the next states leave the layouts"
          say s!"  RESULT: REFUSED (step obligation: {why})"; return 4
    let (aB, _, satB, ncB) ← solve solver work "rbase" φB
    say s!"  base: {ncB} clauses, solver {satB} ms"
    let pfB ← match aB with
      | .unsat p => pure p
      | .sat => do say s!"  RESULT: COUNTEREXAMPLE (base): refinement fails within {kd} cycle(s)"; return 1
      | .failed w => do say s!"  RESULT: SOLVER FAILED (base): {w}"; return 5
    let (aS, _, satS, ncS) ← solve solver work "rstep" φS
    say s!"  step: {ncS} clauses, solver {satS} ms"
    let pfS ← match aS with
      | .unsat p => pure p
      | .sat => do say s!"  RESULT: NOT K-INDUCTIVE (step) at k = {kd}"; return 1
      | .failed w => do say s!"  RESULT: SOLVER FAILED (step): {w}"; return 5
    let t2 ← IO.monoMsNow
    let ok ← IO.lazyPure (fun _ => runRefKOf sy RI RS sp pfB pfS)
    let t3 ← IO.monoMsNow
    say s!"  verified check (runRefKOf) in {t3 - t2} ms"
    if ok then
      say s!"  RESULT: REFINEMENT PROVED (k={kd}) -- runRefKOf_sound: on every run, every witnessed cycle relates the outputs"
      return 0
    else
      say "  RESULT: CHECK REJECTED"; return 6
  | _ =>
    IO.eprintln "usage: sym_refine <impl.dcert> <spec.dcert> pair:<i>=<s>... [adv:[!]<o>] [map:<s>=<i>]... [k:<n>]"
    return 2
