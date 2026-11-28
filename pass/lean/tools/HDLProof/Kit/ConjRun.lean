/-
`Kit.ConjRun` -- a conjunctive invariant (`Sym.ConjT`) from its spec to its proofs, ONE loop for
`sym_verify conj:` and the `inv` phase of `sym_refmap refmap`: build the symbolic runs (the base
run, the plain step run, one run per case), solve the base obligation and every (target, case)
step obligation, and write a counterexample report for each target that is not preserved.
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

end HDLProof.Driver
