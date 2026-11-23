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
on `RuntimeState`.
-/
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
    let mut compactions := 0
    for k in List.range' start cycles do
      saveAt k s
      if compactK > 0 && k > start && (k - start) % compactK == 0 then
        if let some f := sim.compact then
          s := f hash k s
          compactions := compactions + 1
      match sim.step (edgesAt D edges k) (tab.inputAt k) s with
      | .error e =>
        (← IO.getStdout).flush
        IO.eprintln s!"step {k}: refused: {e}"
        return 1
      | .ok (outs, s') =>
        IO.println s!"STEP {k} {" ".intercalate (outs.toList.map (fun o => toString (bv_uint o)))}"
        s := s'
    saveAt (start + cycles) s
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

end Run
end HDLProof
