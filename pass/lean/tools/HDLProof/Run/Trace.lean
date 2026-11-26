/-
# `cert_trace`'s open-loop trace, for a step given as a function

`cert_trace` runs routes `ir` and `vc`, which take any certificate. Routes compiled per design
(translation validation's reified step, `ReifyGen`) are their own programs; `traceMain` is the
main such a program calls, with its certificate and its step. It reads `cert_trace`'s
arguments and prints `cert_trace`'s lines, so the traces compare line for line:

    <cycles> <stim> [edges:all|edges:third|edges:file:<path>] [init:reset|init:zero]
        [ckpt-in:<file>] [ckpt-at:<cycle>:<file>] [ckpt-out:<file>] [compact:<cycles>]

* `STEP <k> <out0> <out1> ...`: each output's unsigned value, before the edge.
* The stimulus (`stim v1`), the edge schedule and the start states are `cert_trace`'s
  (`Run/Stim`: `StimTab`, `edgesAt`; `Direct.resetState`, `Direct.zeroState`).
* Checkpoints are `lhdcert-ckpt v1` (`Compiler/Ckpt`, `formats/ckpt.md`) with absolute cycle
  numbers: `ckpt-in:` starts at the checkpoint's cycle; `ckpt-at:` and `ckpt-out:` write the
  bytes every other route writes for the same state.
* Compaction is `cert_trace`'s: every 256 cycles on a run longer than that of a design with
  memories, when the certificate meets `compacted_outputs`' hypotheses (`compactBlocker`);
  `compact:<K>` sets the period, `compact:0` turns it off. It changes no output when the step
  is `interpretDesign` (`Run/Sound`, `compacted_outputs`), which a reified step is by its
  per-design theorem (`ReifyProof`: `prove_reified_chunked` for the chunked step).

`traceRun` is the loop for any state type (`TraceSim`): the legacy model keeps its own record
and is converted only where a checkpoint is taken or read
(`Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py --trace`); `traceMain` is it for a step
on `RuntimeState`; `TraceSim.ofEngine` is it for any route's engine (`Compiler/Engine`), which is
how `cert_trace` runs every route `EngineSelector` builds. The loop hands the state from one cycle
to the next as the argument of a recursive function (`traceLoop`), so each step holds the only
reference to it and an engine that keeps state (vc's tables and environment) updates it in place.
-/
import HDLProof.Run.Engine
import HDLProof.Run.Stim
import LeanSemanticPrimitives.Compiler.Ckpt
import LeanSemanticPrimitives.Compiler.DirectTrace
import LeanSemanticPrimitives.Compiler.MemAddr

namespace HDLProof
namespace Run

open Compiler

/-- `cert_trace`'s period of automatic compaction. -/
def traceAutoCompactK : Nat := 256

/-- Why `compacted_outputs` does not cover this certificate, or `none` (`cert_trace`'s check). -/
def traceCompactBlocker (D : DesignCert) : Option String :=
  match Compiler.Direct.checkDesign D with
  | .error e => some s!"checkDesign: {e.render}"
  | .ok _ => (MemAddr.addrError D).orElse fun _ =>
      (Ckpt.imgRootError D).orElse fun _ => Ckpt.wordError D

/-- A route compiled for one design, as `traceRun` drives it: its own state `σ`, and how a
checkpoint is taken of it and put back into it. -/
structure TraceSim (σ : Type) where
  /-- `init:zero` (true) or the power-on state (false) -/
  init    : Bool → σ
  /-- one step: the outputs before the edge and the next state, or why the step is refused -/
  step    : ClockEdges → RuntimeInput → σ → Except String (Array BV × σ)
  /-- the state a checkpoint is taken of -/
  view    : σ → RuntimeState
  /-- a checkpoint `Ckpt.validate` accepted, as this route's state -/
  restore : Ckpt.Ckpt → σ
  /-- replace the state by its restored checkpoint (design hash, cycle), when the route keeps
  memories as chains of functions; `none` turns compaction off -/
  compact : Option (Nat → Nat → σ → σ) := none
  /-- whether automatic compaction applies; `false` for a route that keeps its memories as
  tables (vc's default engine), where only an explicit `compact:<K>` compacts -/
  autoCompact : Bool := true
  /-- run once the start state is set and the checkpoints are checked, just before the first
  cycle, with the cycle count, the start cycle and the action that writes the checkpoints due at
  the start; `some code` ends the run with that exit code (a route that refused the design) -/
  atStart : Nat → Nat → IO Unit → IO (Option UInt32) := fun _ _ _ => pure none

/-- `traceRun`'s cycles `k .. stop - 1` from state `s`: the state goes from one cycle to the next as
this function's argument (module header). The final state and the number of compactions, or
`none` after a refused step, which is reported. -/
partial def traceLoop {σ : Type} (D : DesignCert) (sim : TraceSim σ) (edges : EdgeSpec) (tab : StimTab)
    (saveAt : Nat → σ → IO Unit) (start stop compactK hash : Nat) (k : Nat) (s : σ)
    (compactions : Nat) : IO (Option (σ × Nat)) := do
  if k ≥ stop then return some (s, compactions)
  saveAt k s
  let (s, compactions) :=
    if compactK > 0 && k > start && (k - start) % compactK == 0 then
      match sim.compact with
      | some f => (f hash k s, compactions + 1)
      | none => (s, compactions)
    else (s, compactions)
  match sim.step (edgesAt D edges k) (tab.inputAt k) s with
  | .error e =>
    (← IO.getStdout).flush
    IO.eprintln s!"step {k}: refused: {e}"
    return none
  | .ok (outs, s') =>
    IO.println s!"STEP {k} {" ".intercalate (outs.toList.map (fun o => toString (bv_uint o)))}"
    traceLoop D sim edges tab saveAt start stop compactK hash (k + 1) s' compactions

/-- `cert_trace`'s loop, for a route compiled for `D` (named `route` in messages). -/
def traceRun {σ : Type} (route : String) (D : DesignCert) (sim : TraceSim σ) (args : List String) :
    IO UInt32 := do
  match args with
  | cyc :: stimPath :: rest =>
    let some cycles := cyc.toNat? | IO.eprintln s!"cycles {repr cyc} is not a number"; return 2
    let mut edgeOpt := "edges:all"
    let mut zeroInit := false
    let mut ckptIn : Option String := none
    let mut ckptAt : Array (Nat × String) := #[]
    let mut ckptOut : Option String := none
    let mut compactK : Nat := 0
    let mut compactAuto := true
    let mut sched := false
    for opt in rest do
      if opt.startsWith "edges:file:" then
        edgeOpt := opt; sched := true
      else if opt.startsWith "ckpt-in:" then
        ckptIn := some (opt.replace "ckpt-in:" "")
      else if opt.startsWith "ckpt-out:" then
        ckptOut := some (opt.replace "ckpt-out:" "")
      else if opt.startsWith "compact:" then
        match (opt.replace "compact:" "").toNat? with
        | some k =>
          if k > 0 && sim.compact.isNone then
            IO.eprintln s!"compact: route {route} has no compaction"
            return 2
          compactK := k; compactAuto := false
        | none => do IO.eprintln s!"compact: {repr opt} is not a number of cycles"; return 2
      else if opt.startsWith "ckpt-at:" then
        match (opt.replace "ckpt-at:" "").splitOn ":" with
        | k :: p@(_ :: _) =>
          match k.toNat? with
          | some k => ckptAt := ckptAt.push (k, ":".intercalate p)
          | none => do IO.eprintln s!"ckpt-at: {repr k} is not a number"; return 2
        | _ => do IO.eprintln s!"ckpt-at expects <cycle>:<file>, got {repr opt}"; return 2
      else
        match opt with
        | "edges:all" => if !sched then edgeOpt := opt
        | "edges:third" => if !sched then edgeOpt := opt
        | "init:reset" => zeroInit := false
        | "init:zero" => zeroInit := true
        | _ => do IO.eprintln s!"unknown option {opt}"; return 2
    let st ← match parseStim (← IO.FS.readFile stimPath) with
      | .ok s => pure s
      | .error e => do IO.eprintln s!"stimulus: {e}"; return 2
    let edges ← match ← readEdgeSpec edgeOpt with
      | .ok e => pure e
      | .error e => do IO.eprintln s!"edges: {e}"; return 2
    let tab := StimTab.of D st
    if compactAuto && sim.compact.isSome && cycles > traceAutoCompactK && D.memories.size > 0 then
      match traceCompactBlocker D with
      | none => compactK := traceAutoCompactK
      | some why => IO.eprintln s!"automatic compaction off: {why}"
    let mut s := sim.init zeroInit
    let hash := if ckptIn.isSome || ckptOut.isSome || !ckptAt.isEmpty then Ckpt.designHash D else 0
    let mut start := 0
    if let some p := ckptIn then
      match Ckpt.parse (← IO.FS.readBinFile p) with
      | .error e => do IO.eprintln s!"{p}: {e}"; return 2
      | .ok c =>
        if let some e := Ckpt.validate D hash c then
          IO.eprintln s!"{p}: {e}"; return 1
        unless c.exts.isEmpty do
          IO.eprintln s!"note: {p}: {c.exts.size} ext sections not read"
        s := sim.restore c
        start := c.cycle
    for (k, p) in ckptAt do
      unless start ≤ k && k ≤ start + cycles do
        IO.eprintln s!"ckpt-at:{k}:{p}: cycle {k} is outside this run ({start} .. {start + cycles})"
        return 2
    let saveAt (k : Nat) (st : σ) : IO Unit := do
      for (j, p) in ckptAt do
        if j == k then IO.FS.writeBinFile p (Ckpt.render (Ckpt.save D hash k #[] (sim.view st)))
      if k == start + cycles then
        if let some p := ckptOut then IO.FS.writeBinFile p (Ckpt.render (Ckpt.save D hash k #[] (sim.view st)))
    if let some rc ← sim.atStart cycles start (saveAt start s) then return rc
    -- automatic compaction does not apply to a route that keeps its memories as tables
    if compactAuto && !sim.autoCompact then compactK := 0
    let some (sEnd, compactions) ← traceLoop D sim edges tab saveAt start (start + cycles) compactK hash
        start s 0
      | return 1
    saveAt (start + cycles) sEnd
    if compactK > 0 then
      (← IO.getStdout).flush
      IO.eprintln s!"compactions {compactions} (every {compactK} cycles{if compactAuto then ", automatic" else ""})"
    return 0
  | _ =>
    IO.eprintln s!"usage: <{route} trace> <cycles> <stim> [edges:all|edges:third|edges:file:<path>] \
[init:reset|init:zero] [ckpt-in:<file>] [ckpt-at:<cycle>:<file>] [ckpt-out:<file>] [compact:<cycles>]"
    return 2

/-- The trace main of a route compiled for `D` whose step works on `RuntimeState`, as
translation validation's reified step does (`interpretDesign`'s type): checkpoints are taken of
its state as it is, and it compacts as `cert_trace` does. -/
def traceMain (route : String) (D : DesignCert)
    (step : ClockEdges → RuntimeInput → RuntimeState → RuntimeResult) (args : List String) :
    IO UInt32 :=
  traceRun route D
    { init := fun z => if z then Direct.zeroState D else Direct.resetState D
      step := fun e i s => let r := step e i s; .ok (r.outputs, r.nextState)
      view := id
      restore := Ckpt.restore
      compact := some fun hash k s => Ckpt.restore (Ckpt.save D hash k #[] s) } args

/-- The trace main's view of a route's engine (`Compiler/Engine`): the start is the engine's
load of the power-on or all-zero state, a checkpoint is loaded from its restored state, compaction
reloads the restored checkpoint of the state the engine stands for (an engine keeps what stays
valid: vc its slot environment), and a refused step drops the state the engine hands back. -/
def TraceSim.ofEngine {σ : Type} (D : DesignCert) (E : Engine σ) (autoCompact : Bool := true)
    (atStart : Nat → Nat → IO Unit → IO (Option UInt32) := fun _ _ _ => pure none) : TraceSim σ :=
  { init := fun z => E.load (if z then Direct.zeroState D else Direct.resetState D)
    step := fun e i q => match E.step e i q with
      | .ok r => .ok r
      | .error (err, _) => .error err
    view := E.view
    restore := fun c => E.load (Ckpt.restore c)
    compact := some fun hash k q => E.reload q (Ckpt.restore (Ckpt.save D hash k #[] (E.view q)))
    autoCompact, atStart }

end Run
end HDLProof
