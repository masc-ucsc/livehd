/-
`sym_refine` -- prove that an implementation design refines a specification
design up to stuttering, by k-induction on their projection residuals (M3 S6).

    sym_refine <impl.dcert> <spec.dcert> pair:<implOut>=<specOut>... [adv:[!]<implOut>]
               [map:<specInput>=<implInput>]... [asm:[!]<implOut>] [in:<implInput>=<value>]...
               [stateq] [rebuild] [route:fused|var|plain] [k:<n>[,<n>...]] [sf:<n>] [wf:<n>] [solver:<cadical>] [work:<dir>]

The implementation steps every cycle; the specification steps only on cycles
where the witness output `adv` fires (no `adv`: lockstep) and holds
otherwise. `stateq` also requires the two next states equal (normalized) on
every cycle -- the strengthening an identity refinement needs. Specification
input `s` is implementation input `i` for every `map:s=i` (unmapped
specification inputs default to the implementation input of the same NAME).
On every witnessed cycle each `pair:` must agree. Both designs start
all-zero. Both are specialized ONCE (one certificate file: once in all); each
k of the list is then tried in order until one proves. Verdict: the verified
check `Sym.runRefKOf` (through `runRefKOf_of_obls`, over the obligations
already built; `rebuild` re-builds them) against LRAT proofs from the
untrusted solver;
acceptance is `runRefKOf_sound_ref` (the refinement over `interpretDesign`)
given the specializer runs (the D1 trusted native run of every FP route).
-/
import HDLProof.Sym.Refine
import LeanSemanticPrimitives.Projection.CertLoadAny
import Lean.Data.Json
import HDLProof.Kit.Driver

open Compiler Projection HDLProof HDLProof.Sym HDLProof.Driver Projection.MemProjection Projection.HwMem
open Std.Tactic.BVDecide

/-! ## Counterexample replay

The BASE obligation's variables `j * nIn + t` are cycle `j`'s implementation
input `t` (the layout `refBaseRun` builds). A model is decoded into the input
sequence and REPLAYED on both residual simulators: the implementation every
cycle, the specification only on witnessed cycles (as `stutM`). -/

def decodeInputs (φ : BVLogicalExpr) (model : Std.HashMap Nat Bool) (inW : List Nat) (k : Nat) :
    Array RuntimeInput := Id.run do
  let entry := φ.bitblast
  let nodes := atomNodes entry.aig
  let nIn := inW.length
  let mut out := #[]
  for j in List.range k do
    out := out.push ((inW.zipIdx.map fun (w, t) =>
      mk_bv w (Int.ofNat (modelValue nodes model (j * nIn + t) w))).toArray)
  return out

/-- The first cycle whose assumptions held so far and whose refinement verdict
fails, with each disagreeing pair `(implOut, specOut, implValue, specValue)`;
`stateDiff` when only the next states differ. -/
def replayRef (sy : RefSys) (RI RS : Program) (sp : RefSpec) (ins : Array RuntimeInput) :
    Option (Nat × Bool × List (Nat × Nat × Int × Int) × Bool) := Id.run do
  let some simI := mkMemSimOfW sy.DI RI | return none
  let some simS := mkMemSimOfW sy.DS RS | return none
  let mut stI := sy.sI0
  let mut stS := sy.sS0
  let mut asmOK := true
  for j in List.range ins.size do
    let some rI := runMemSim simI (edgesAll sy.DI) ins[j]! stI | return none
    let some rS := runMemSim simS (edgesAll sy.DS) (mapInputs ins[j]! sy.inMap) stS | return none
    asmOK := asmOK && asmBit rI.outputs ins[j]! sp.asmOut sp.asmIn
    let adv := advBit rI.outputs sy.adv
    let stS' := if adv then rS.nextState else stS
    let outsBad := !refBit rI.outputs rS.outputs sp.adv sp.pairs
    let stateBad := sp.stateEq && !veqB (StateM.encStateM rI.nextState) (StateM.encStateM stS')
    if asmOK && (outsBad || stateBad) then
      let diffs := sp.pairs.filterMap fun (a, b) =>
        match rI.outputs[a]?, rS.outputs[b]? with
        | some x, some y => if bv_uint x == bv_uint y then none else some (a, b, bv_uint x, bv_uint y)
        | _, _ => some (a, b, -1, -1)
      return some (j, adv, diffs, stateBad)
    stI := rI.nextState
    stS := stS'
  return none

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
    let ks := (((opt "k").getD "1").splitOn ",").filterMap (·.toNat?)
    if ks.isEmpty then IO.eprintln "k:<n>[,<n>...]"; return 2
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
    say s!"  pairs {pairs}, adv {repr adv}, input map {inMap}{if rest.contains "stateq" then ", next states equal" else ""}, k in {ks}; both start all-zero"
    unless HwMem.admitsWB DI && HwMem.admitsWB DS do
      say "  RESULT: REFUSED (the memory route does not admit a certificate)"; return 4
    let t0 ← IO.monoMsNow
    -- `MemFused.projectMemFDesignAt` (`MemResidual.of_projF`) unless `route:var`
    -- (`projectMemVarDesignAt`, `of_projVar`) or `route:plain` (`projectMemDesignAt`, `of_proj`)
    let route := (opt "route").getD "fused"
    unless ["fused", "var", "plain"].contains route do
      IO.eprintln s!"route:{route} is not one of fused | var | plain"; return 2
    let proj := fun D => if route == "plain" then projectMemDesignAt sf wf D
      else if route == "var" then MemVariant.projectMemVarDesignAt sf wf D
      else MemFused.projectMemFDesignAt sf wf D
    let RI ← match proj DI with
      | .ok R => pure R | .error e => do say s!"  RESULT: REFUSED (impl specialization: {repr e})"; return 4
    -- an identity refinement (one certificate file) specializes once
    let RS ← if ci == cs then pure RI else match proj DS with
      | .ok R => pure R | .error e => do say s!"  RESULT: REFUSED (spec specialization: {repr e})"; return 4
    let some bI := Hw.checkResidual RI | say "  RESULT: REFUSED (impl residual check)"; return 4
    let some bS := Hw.checkResidual RS | say "  RESULT: REFUSED (spec residual check)"; return 4
    let t1 ← IO.monoMsNow
    say s!"  residuals specialized (route {route}) and checked in {t1 - t0} ms"
    let sy : RefSys := { DI, DS, sI0 := zeroState DI, sS0 := zeroState DS, inMap, adv }
    let single := ks.length == 1
    let mut last : UInt32 := 2
    for kd in ks do
      let pre := if single then "" else s!"[k={kd}] "
      let sp : RefSpec :=
        { pairs, adv, inMap, asmOut, asmIn, k := kd, fuelI := bI + 16, fuelS := bS + 16
        , inW := inputWidths DI (inputCount DI), LI := shapeOf DI, LS := shapeOf DS
        , stateEq := rest.contains "stateq" }
      unless refSideOK sy sp do
        say s!"  {pre}RESULT: REFUSED (side checks)"; return 4
      let t2 ← IO.monoMsNow
      let φB ← match refBaseObl sy RI RS sp with
        | some φ => pure φ
        | none => do
            let why := match refBaseRun sy RI RS sp with | .error m => m | .ok _ => "definitions not scoped"
            say s!"  {pre}RESULT: REFUSED (base obligation: {why})"; return 4
      let φS ← match refStepObl sy RI RS sp with
        | some φ => pure φ
        | none => do
            let why := match refStepRun sy RI RS sp with
              | .error m => m | .ok _ => "scope or the next states leave the layouts"
            say s!"  {pre}RESULT: REFUSED (step obligation: {why})"; return 4
      let t3 ← IO.monoMsNow
      say s!"  {pre}obligations built in {t3 - t2} ms (symbolic evaluation of {kd} + {kd + 1} paired steps)"
      let (aB, bbB, satB, ncB) ← solve solver work s!"rbase_k{kd}" φB #["--unsat"]
      say s!"  {pre}base: {ncB} clauses; bit-blast+CNF+write {bbB} ms, solver {satB} ms"
      let pfB ← match aB with
        | .unsat p => pure p
        | .sat model => do
            let ins := decodeInputs φB (parseModel model) sp.inW kd
            let implOuts ← portNames ci "outputs"
            let specOuts ← portNames cs "outputs"
            let implIns ← portNames ci "inputs"
            let nm (l : List (String × Nat)) (k : Nat) : String :=
              ((l.find? (·.2 == k)).map (·.1)).getD (toString k)
            match replayRef sy RI RS sp ins with
            | some (j, adv, diffs, stateBad) =>
                say s!"  {pre}counterexample inputs (cycle: name=value):"
                for (row, c) in (ins.toList.take (j + 1)).zipIdx do
                  say s!"    cycle {c}: {", ".intercalate (row.toList.zipIdx.map fun (x, t) => s!"{nm implIns t}={bv_uint x}")}"
                say s!"  {pre}cycle {j} (witness {adv}): {", ".intercalate (diffs.map fun (a, b, x, y) =>
                  s!"{nm implOuts a}={x} vs {nm specOuts b}={y}")}{if stateBad then "; next states differ" else ""}"
                say s!"  {pre}RESULT: COUNTEREXAMPLE (base, replayed): the refinement fails at cycle {j} from the all-zero states with assumptions holding"
                return 1
            | none =>
                say s!"  {pre}RESULT: COUNTEREXAMPLE NOT REPLAYED (base): the solver's model did not reproduce on the simulators"
                return 7
        | .failed w => do say s!"  {pre}RESULT: SOLVER FAILED (base): {w}"; return 5
      let (aS, bbS, satS, ncS) ← solve solver work s!"rstep_k{kd}" φS #["--unsat"]
      say s!"  {pre}step: {ncS} clauses; bit-blast+CNF+write {bbS} ms, solver {satS} ms"
      let pfS ← match aS with
        | .unsat p => pure p
        | .sat _ => do
            say s!"  {pre}RESULT: NOT K-INDUCTIVE (step) at k = {kd}"
            last := 1
            continue
        | .failed w => do say s!"  {pre}RESULT: SOLVER FAILED (step): {w}"; return 5
      let t4 ← IO.monoMsNow
      -- through `runRefKOf_of_obls` over the obligations built above; `rebuild`
      -- evaluates `runRefKOf` itself (repeats the symbolic evaluation)
      let rebuild := rest.contains "rebuild"
      let ok ← if rebuild then IO.lazyPure (fun _ => runRefKOf sy RI RS sp pfB pfS)
               else IO.lazyPure (fun _ => refSideOK sy sp && checkUnsat φB pfB && checkUnsat φS pfS)
      let t5 ← IO.monoMsNow
      say s!"  {pre}verified check ({if rebuild then "runRefKOf: re-build, " else "runRefKOf via runRefKOf_of_obls: "}LRAT-check both) in {t5 - t4} ms"
      if ok then
        say s!"  {pre}RESULT: REFINEMENT PROVED (k={kd}) -- runRefKOf_sound_ref: on every run, every witnessed cycle relates the implementation's interpretDesign outputs to the stuttered specification's"
        return 0
      else
        say s!"  {pre}RESULT: CHECK REJECTED"; return 6
    return last
  | _ =>
    IO.eprintln "usage: sym_refine <impl.dcert> <spec.dcert> pair:<i>=<s>... [adv:[!]<o>] [map:<s>=<i>]... [asm:[!]<o>] [in:<i>=<v>]... [stateq] [k:<n>[,<n>...]]"
    return 2
