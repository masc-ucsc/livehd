/-
`Kit.ConjRun` -- a conjunctive invariant (`Sym.ConjT`) from its spec to its proofs, for
`sym_verify conj:` and the `inv` phase of `sym_refmap refmap`: ONE builder that reads the spec's
lines into conjuncts (`buildConj`), and ONE loop that builds the symbolic runs (the base run, the
plain step run, one run per case), solves the base obligation and every (target, case) step
obligation, and writes a counterexample report for each target that is not preserved
(`proveConj`).
Untrusted: the verdict is the caller's check over what this returns (`sideOKC && checkT` over the
runs, which is `runConjTOf` by definition, or `runConjTOf` itself). No theorems.
-/
import HDLProof.Kit.Driver

open Compiler Projection HDLProof HDLProof.Sym Projection.MemProjection Projection.HwMem
open Std.Tactic.BVDecide

namespace HDLProof.Driver

/-- A symbolic run of the conjunctive checker. -/
abbrev ConjSymRun := Except String (List StepO × SymSt)

/-- What `proveConj` was given. -/
structure ConjJob where
  D         : DesignCert
  /-- the route's symbolic step (`Kit.SymRoute`) -/
  S         : Stepper
  sp        : ConjTSpec
  s0        : StateM
  /-- the conjuncts' names, for reports -/
  names     : Array String
  /-- the design's flop and input names (`_io.json`), for reports -/
  flopNames : Array String
  inNames   : Array String
  solver    : String
  work      : System.FilePath
  /-- obligations solved at a time -/
  jobs      : Nat
  /-- case runs built at a time (each holds its symbolic state until the final check) -/
  runJobs   : Nat
  /-- reuse an LRAT proof whose CNF in `work` is byte-identical (`Driver.solve`) -/
  cache     : Bool := false
  /-- the obligations' files in `work`: `<filePre>_base`, `<filePre>_<target>_<case>` -/
  filePre   : String
  /-- a target's counterexample report: `<work>/cti_<ctiPre><target>.txt` -/
  ctiPre    : String := ""
  /-- report the runs, every obligation and the totals; otherwise only the counterexamples -/
  verbose   : Bool := true
  /-- re-run each counterexample's two cycles on fp's simulator: its residual (built when first
  needed) and the fuel it was specialized at, for the report -/
  replay    : Option ((Unit → Except String Program) × String) := none

/-- How `proveConj` ended. -/
inductive ConjEnd where
  /-- every obligation is UNSAT: the base proof, the step proofs (`pfS[t][m]`, target `t` in its
  `m`-th case), the runs they were built over (base, plain step, one per case in `allCases`
  order), and the clauses of every obligation -/
  | proved (pfB : ByteArray) (pfS : List (List ByteArray)) (rb r0 : ConjSymRun)
      (runs : List (Case × ConjSymRun)) (clauses : Nat)
  /-- these targets (`<name>` or `<name> | <case>`) are not preserved under their hypotheses -/
  | notInductive (failing : List String)
  /-- a side condition, or a run's shape or scope (`REFUSED`, exit 4) -/
  | refused (why : String)
  /-- the base obligation is satisfiable: a conjunct fails from the start state (exit 1) -/
  | baseCex
  /-- the solver gave no answer: on the base obligation, on a target, or its task failed (exit 5) -/
  | solverBase (why : String)
  | solverStep (target why : String)
  | solverTask (why : String)

/-- Build the runs, solve every obligation, report every target that is not preserved. -/
def proveConj (J : ConjJob) : IO ConjEnd := do
  let sp := J.sp
  let D := J.D
  let n := sp.conjs.length
  unless sideOKC D sp.plain J.s0 do return .refused "side conditions"
  let t1 ← IO.monoMsNow
  -- the base run, the plain step run (layout), and one run per case, in parallel
  let tb ← IO.asTask (prio := .dedicated) (IO.lazyPure fun _ => baseRunC D J.S sp.plain J.s0)
  let tp ← IO.asTask (prio := .dedicated) (IO.lazyPure fun _ => stepRunC D J.S sp.plain)
  let allC := sp.allCases
  -- case runs, `runJobs` at a time (each holds a symbolic run in memory until the final check)
  let mut runList : List (Case × ConjSymRun) := []
  for batch in chunksOf (List.range allC.length) J.runJobs do
    let mut tcs := #[]
    for j in batch do
      let c := allC.getD j []
      tcs := tcs.push (c, ← IO.asTask (prio := .dedicated) (IO.lazyPure fun _ => stepRunT D J.S sp c))
    for (c, task) in tcs.toList do
      match ← IO.wait task with
      | .ok r => runList := runList ++ [(c, r)]
      | .error e => return .refused s!"{e}"
  let rb ← match ← IO.wait tb with | .ok r => pure r | .error e => return .refused s!"{e}"
  let r0 ← match ← IO.wait tp with | .ok r => pure r | .error e => return .refused s!"{e}"
  let t2 ← IO.monoMsNow
  if J.verbose then
    say s!"  symbolic runs built in {t2 - t1} ms (base, plain step, {allC.length} case run(s))"
  let (sB, stB) ← match rb with
    | .ok ([s], st) => pure (s, st)
    | .ok _ => return .refused "base run shape"
    | .error e => return .refused s!"base run: {e}"
  match r0 with
  | .ok (a :: _, st) =>
      unless scopedB st && decide (stateShapeOf a.2.2 = some sp.L) do
        return .refused "the step leaves the layout"
  | .ok _ => return .refused "plain step run shape"
  | .error e => return .refused s!"plain step run: {e}"
  for (c, r) in runList do
    match r with
    | .ok ([_, _], st) =>
        unless scopedB st do return .refused s!"case {showCase J.flopNames c}: definitions not scoped"
    | .ok _ => return .refused "case run shape"
    | .error e => return .refused s!"case {showCase J.flopNames c}: {e}"
  unless scopedB stB do return .refused "base definitions not scoped"
  let some φB := baseOblT sp J.s0 stB sB | return .refused "a conjunct could not be read"
  let (aB, bbB, satB, ncB) ← solve J.solver J.work s!"{J.filePre}_base" φB (cache := J.cache)
  if J.verbose then say s!"  base: {ncB} clauses; bit-blast+CNF+write {bbB} ms, solver {satB} ms"
  let pfB ← match aB with
    | .unsat p => pure p
    | .sat _ => return .baseCex
    | .failed w => return .solverBase w
  -- every (target, case) obligation, `jobs` at a time
  let mut items : Array (Nat × Nat × Case) := #[]
  for t in List.range n do
    for (c, m) in (sp.casesOf t).zipIdx do
      items := items.push (t, m, c)
  let mut pfs : Array (Array ByteArray) :=
    (List.range n).toArray.map fun t => Array.replicate (sp.casesOf t).length ByteArray.empty
  let mut failing : List String := []
  let mut totalC := 0
  let mut maxC := 0
  for batch in chunksOf (List.range items.size) J.jobs do
    let mut tasks := #[]
    for j in batch do
      let (t, m, c) := items[j]!
      let task ← IO.asTask (prio := .dedicated) (do
        let some (.ok ([a, b], st)) := findRun runList c
          | return (j, SatAnswer.failed "no run for the case", 0, 0, 0, none)
        let some φ := stepOblT sp c st a b t
          | return (j, SatAnswer.failed "the obligation could not be built", 0, 0, 0, none)
        let (ans, bb, sat, nc) ← solve J.solver J.work s!"{J.filePre}_{t}_{m}" φ (cache := J.cache)
        return (j, ans, bb, sat, nc, some φ))
      tasks := tasks.push task
    for task in tasks do
      match ← IO.wait task with
      | .error e => return .solverTask s!"{e}"
      | .ok (j, ans, bb, sat, nc, φo) =>
        let (t, m, c) := items[j]!
        let cname := J.names.getD t s!"conj{t}"
        let tag := if (sp.casesOf t).length == 1 then cname else s!"{cname} | {showCase J.flopNames c}"
        totalC := totalC + nc
        maxC := max maxC nc
        match ans with
        | .unsat p =>
            pfs := pfs.modify t (·.set! m p)
            if J.verbose then say s!"  [{tag}] {nc} clauses; bit-blast+CNF+write {bb} ms, solver {sat} ms: UNSAT"
        | .sat model =>
            failing := failing ++ [tag]
            if J.verbose then
              say s!"  [{tag}] {nc} clauses; bit-blast+CNF+write {bb} ms, solver {sat} ms: SAT (not preserved)"
            if let some φ := φo then
              if let some (.ok ([a, b], st)) := findRun runList c then
                -- the model's primary values: the cycle-0 state (through the case's
                -- template) and both cycles' inputs; every cut variable then follows
                -- from the run's definitions (extendSt, as in the soundness proofs), so
                -- every conjunct and register is evaluated exactly, without simulating
                let (st0, ins) := decodeStepT φ (parseModel model) sp.L sp.inW (sp.tmplOf c)
                let xs := (stateVals st0 ++ inputVals ins[0]! ++ inputVals ins[1]!).toArray
                let σa := extendD xs st
                let get := fun i => σa.getD i ⟨0#0⟩
                let s0V := stateVarT sp.L (sp.tmplOf c) 0
                let falseAt (sv so : SVal) : List String := (List.range n).filterMap fun i =>
                  let nm := J.names.getD i s!"conj{i}"
                  match holdsE sv so (sp.conjs.getD i default) with
                  | some e => if evalLD get e then none else some nm
                  | none => some s!"{nm}(unreadable)"
                let f0 := falseAt s0V a.2.1
                let f1 := falseAt a.2.2 b.2.1
                let fl1 : Array BV := (List.range sp.L.fws.length).toArray.map fun f =>
                  match flopE a.2.2 f with
                  | some ⟨w, e⟩ => mk_bv w (Int.ofNat (evalD get e).toNat)
                  | none => mk_bv 1 0
                let regs0 := groupedState J.flopNames st0
                let regs1 := groupedState J.flopNames { flops := fl1, mems := #[] }
                let changed := (regs0.zip regs1).filterMap fun ((r, v0), (_, v1)) =>
                  if v0 == v1 then none else some s!"  {r}: {v0} -> {v1}"
                let safe := tag.map fun ch => if ch.isAlphanum || ch == '_' then ch else '_'
                let f := J.work / s!"cti_{J.ctiPre}{safe}.txt"
                let inLine (i : RuntimeInput) : String := " ".intercalate ((List.range i.size).filterMap fun k =>
                  let v := bv_uint i[k]!
                  if v == 0 then none else some s!"{J.inNames.getD k s!"in{k}"}={v}")
                IO.FS.writeFile f s!"counterexample to induction for {tag}\n\
conjuncts FALSE at cycle 0: {f0}\nconjuncts FALSE at cycle 1: {f1}\n\
nonzero inputs, cycle 0: {inLine ins[0]!}\nnonzero inputs, cycle 1: {inLine ins[1]!}\n\
registers that change from cycle 0 to cycle 1:\n{"\n".intercalate changed}\n\
state at cycle 0:\n{"\n".intercalate (regs0.map fun (r, v) => s!"  {r} = {v}")}\n"
                say s!"  [{tag}] counterexample: false at cycle 0 {f0}, at cycle 1 {f1}; state, inputs and changes in {f}"
                -- optional concrete cross-check on the projection's simulator (slow on big designs)
                if let some (prog, fuel) := J.replay then
                  match (prog ()).map (fun R => replay2 D R st0 ins) with
                  | .error e => say s!"  [{tag}] replay: no simulator ({e} at {fuel}; rerun with more fp fuel)"
                  | .ok none => say s!"  [{tag}] replay: the simulator refused the residual or the decoded state"
                  | .ok (some (o0, st1, o1)) =>
                    let broke := !(sp.conjs.getD t default).holds st1.flops o1
                    let hs := (sp.hypsOf t).all fun i => (sp.conjs.getD i default).holds st0.flops o0
                    say s!"  [{tag}] replay: hypotheses at cycle 0 {hs}, broken at cycle 1 {broke}"
        | .failed w => return .solverStep tag w
  let t3 ← IO.monoMsNow
  if J.verbose then
    say s!"  steps: {items.size} obligations, {totalC} clauses in total, largest {maxC}; {t3 - t2} ms"
  unless failing.isEmpty do return .notInductive failing
  return .proved pfB (pfs.toList.map (·.toList)) rb r0 runList (ncB + totalC)

/-! ## The conj spec

`sym_verify conj:<spec>` and a `.refmap`'s `inv <file>` read one format (`sym_verify.lean`, M8):
one conjunct (or one group) per line, `<name> [!]<output>`, `<name> eq <reg> <reg>`, `<name> eqif
<case> <reg> <reg>`, `<name> oneof <case>;<case>;...`, each with optional `hyps: <name> ...` and
`split: <name of a oneof>`. `buildConj` turns its lines (`parseSpec`) into the conjuncts, ONE
builder for both tools. -/

/-- A conj spec's conjuncts: their names (`<line>` or `<line>[<bit>]` for a register pair), the
conjuncts each target's step assumes (`none`: all), and the `oneof` it is split on. -/
structure ConjParts where
  conjs : Array ConjK := #[]
  names : Array String := #[]
  hyps  : Array (Option (List Nat)) := #[]
  split : Array (Option Nat) := #[]

/-- The conjuncts of a conj spec's lines. `fidx` names the design's flops (`resolveReg`), `fws`
their widths; `outProp` reads an output token `[!]<output>` (each tool names outputs its own
way). An error names the line it is about. -/
def buildConj (lines : List SpecLine) (fidx : Std.HashMap String Nat) (fws : List Nat)
    (outProp : String → IO (Except String PropSpec)) : IO (Except String ConjParts) := do
  let mut conjs : Array ConjK := #[]
  let mut cname : Array String := #[]
  let mut groups : Std.HashMap String (List Nat) := {}
  for l in lines do
    let mut ks : List ConjK := []
    match l.kind with
    | ["eq", a, b] =>
        let some ba := resolveReg fidx a | return .error s!"{l.name}: no register {repr a}"
        let some bb := resolveReg fidx b | return .error s!"{l.name}: no register {repr b}"
        unless ba.length == bb.length do return .error s!"{l.name}: {a} and {b} differ in width"
        ks := (ba.zip bb).map fun (f, g) => .eq f g
    | ["eqif", c, a, b] =>
        let cas ← match parseCase fidx fws c with
          | .ok x => pure x
          | .error e => return .error s!"{l.name}: {e}"
        let some ba := resolveReg fidx a | return .error s!"{l.name}: no register {repr a}"
        let some bb := resolveReg fidx b | return .error s!"{l.name}: no register {repr b}"
        unless ba.length == bb.length do return .error s!"{l.name}: {a} and {b} differ in width"
        ks := (ba.zip bb).map fun (f, g) => .eqIf cas f g
    | ["oneof", cs] =>
        let mut cases : List Case := []
        for c in (cs.splitOn ";").filter (fun w => !w.isEmpty) do
          match parseCase fidx fws c with
          | .ok x => cases := cases ++ [x]
          | .error e => return .error s!"{l.name}: {e}"
        ks := [.oneOf cases]
    | [tok] =>
        match ← outProp tok with
        | .ok p => ks := [.out p]
        | .error e => return .error s!"{l.name}: {e}"
    | k => return .error s!"{l.name}: cannot read {k}"
    let start := conjs.size
    for (k, i) in ks.zipIdx do
      conjs := conjs.push k
      cname := cname.push (if ks.length == 1 then l.name else s!"{l.name}[{i}]")
    groups := groups.insert l.name ((List.range ks.length).map (start + ·))
  let n := conjs.size
  let mut hyps : Array (Option (List Nat)) := Array.replicate n none
  let mut split : Array (Option Nat) := Array.replicate n none
  for l in lines do
    let some mine := groups[l.name]? | continue
    if let some hs := l.hyps then
      let mut idx : List Nat := []
      for h in hs do
        let some g := groups[h]? | return .error s!"{l.name}: no conjunct named {repr h}"
        idx := idx ++ g
      for t in mine do hyps := hyps.set! t (some idx)
    if let some sname := l.split then
      match groups[sname]? with
      | some [s] =>
          match conjs[s]? with
          | some (.oneOf _) => for t in mine do split := split.set! t (some s)
          | _ => return .error s!"{l.name}: split {sname} is not a oneof"
      | _ => return .error s!"{l.name}: split {sname} is not a oneof"
  return .ok { conjs, names := cname, hyps, split }

end HDLProof.Driver
