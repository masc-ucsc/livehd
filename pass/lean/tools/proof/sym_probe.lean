/-
`sym_probe` -- build ONE symbolic step of a design on each symbolic route, without a solver,
and report whether it was built or which semantics refused it (diagnostics only: no verdict).

    sym_probe <cert.dcert> [sim:fp|ir|vc|all] [route:fused|var|plain] [sf:<n>] [wf:<n>]
              [census] [diag]

The step starts from a SYMBOLIC state (every flop and memory word a variable, `Sym.stateVar`),
symbolic inputs and every clock firing: the case a k-induction step obligation builds, where
every primitive and operator sees symbolic operands. (A base run from the all-zero state would
fold most of the design to constants and hide the gaps.)

One line per route:
    PROBE <route> OK defs=<cut definitions> setup_ms=<..> step_ms=<..>
    PROBE <route> REFUSED <class>: <message>
`census` adds `CENSUS <route> <key> <count>` lines: the fp residual's primitives, or the
certificate's operators for ir and vc (`Sym.Diag.programCensus`, `nodeCensus`).
`diag` (fp) re-runs the step counting how each primitive, `ite` and `caseT` was taken
(`Sym.Diag.diagStepFP`) and prints `DIAG fp <key> <count>`; `DRIFT fp ok|MISMATCH` compares it
with the real step. Exit 0 when every requested route built its step, 4 otherwise, 2 on usage.
-/
import HDLProof.Sym.Diag
import HDLProof.Kit.Driver
import HDLProof.Kit.SymRoute
import LeanSemanticPrimitives.Projection.CertLoadAny

open Compiler Projection HDLProof HDLProof.Sym HDLProof.Driver Projection.HwMem
open HDLProof.Kit (SymRoute mkSymRoute)

def printCounts (tag : String) (c : Sym.Diag.Counts) : IO Unit := do
  let rows := c.toList.toArray.qsort (fun a b => a.1 < b.1)
  for (k, v) in rows do say s!"{tag} {k} {v}"

def probeRoute (D : DesignCert) (sim route : String) (sf wf : Nat) (census diag : Bool) :
    IO Bool := do
  let t0 ← IO.monoMsNow
  let rt ← match ← IO.lazyPure (fun _ => mkSymRoute sim route sf wf D) with
    | .ok rt => pure rt
    | .error e => do
        say s!"PROBE {sim} REFUSED {Sym.Diag.refusalClass e}: {e}"; return false
  let t1 ← IO.monoMsNow
  let L := shapeOf D
  let inW := inputWidths D (inputCount D)
  let sS := stateVar L 0
  let sI := inputVar inW (layoutSize L)
  let sE : SVal := .conc (encEdges (edgesAll D))
  let st0 : SymSt := ⟨layoutSize L + inW.length, #[]⟩
  if census then
    match rt.fp with
    | some R => printCounts s!"CENSUS {sim}" (Sym.Diag.programCensus R)
    | none => printCounts s!"CENSUS {sim}" (Sym.Diag.nodeCensus D)
  let r ← IO.lazyPure (fun _ => symStep rt.S rt.fuel st0 sE sI sS)
  let t2 ← IO.monoMsNow
  let ok ← match r with
    | .ok (_, st) => do
        say s!"PROBE {sim} OK defs={st.defs.size} setup_ms={t1 - t0} step_ms={t2 - t1} ({rt.name})"
        pure true
    | .error m => do
        say s!"PROBE {sim} REFUSED {Sym.Diag.refusalClass m}: {m}"; pure false
  if diag then
    match rt.fp with
    | some R =>
        let (rd, c) ← Sym.Diag.diagStepFP R rt.fuel st0 sE sI sS
        printCounts s!"DIAG {sim}" c
        let same := match r, rd with
          | .ok (_, a), .ok (_, b) => a.defs.size == b.defs.size
          | .error m, .error m' => m == m'
          | _, _ => false
        say s!"DRIFT {sim} {if same then "ok" else "MISMATCH"}"
    | none => pure ()
  return ok

def main (args : List String) : IO UInt32 := do
  match args with
  | cert :: rest =>
    let opt (k : String) : Option String :=
      rest.findSome? (fun t => (t.dropPrefix? (k ++ ":")).map (·.toString))
    let sf := ((opt "sf").bind (·.toNat?)).getD 20000
    let wf := ((opt "wf").bind (·.toNat?)).getD 200
    let route := (opt "route").getD "fused"
    let sims := match (opt "sim").getD "all" with
      | "all" => ["fp", "ir", "vc"]
      | s => [s]
    let census := rest.contains "census"
    let diag := rest.contains "diag"
    let (_, D) ← Projection.CertLoadAny.loadAny cert
    say s!"{cert}"
    say s!"  shape: sources {D.sources.size} nodes {D.nodes.size} outputs {D.outputs.size} \
flops {D.flops.size} memories {D.memories.size} clocks {D.clocks.size}"
    unless HwMem.admitsWB D do
      say "PROBE all REFUSED setup: the memory route does not admit this certificate"; return 4
    let mut allOk := true
    for s in sims do
      let ok ← probeRoute D s route sf wf census diag
      allOk := allOk && ok
    return if allOk then 0 else 4
  | _ =>
    IO.eprintln "usage: sym_probe <cert.dcert> [sim:fp|ir|vc|all] [route:fused|var|plain] [sf:<n>] [wf:<n>] [census] [diag]"
    return 2
