/-
`cert_trace` -- the per-step OUTPUTS of `interpretDesign` on a certificate
(computed by `directStep`, proved equal to it; the certificate check runs once; route:ir runs
its staged form, `Compiler/DirectStaged`, proved equal by `Staged.stepChecked_eq`),
under a supplied stimulus. The oracle side of an RTL differential: the same
`stim v1` file drives an event simulator on the original RTL, and the two
traces are compared line by line.

    lean --run Futamura_Projection_Scripts/cert_trace.lean <cert> <cycles> <stim>
         [edges:all|edges:third|edges:file:<path>] [init:reset|init:zero] [route:ir|route:vc]
         [opt:off] [opt:stats] [inc:off|inc:scan] [ckpt-in:<file>] [ckpt-at:<cycle>:<file>] [ckpt-out:<file>]
         [compact:<cycles>]

`stim v1` is the dcert2_memroute format, restricted to what an RTL testbench
can replay: `in <ordinal> <width> <value>` (every cycle) and
`at <cycle> in <ordinal> <width> <value>` (that cycle wins). Unlisted inputs
are 0 at their declared width. The initial state puts every flop at its reset
value and every memory word at 0 (`init:zero` starts the flops at 0 instead). Edges: `all` fires every declared clock on every step; `third` is
`edgeSchedule`'s one-clock pattern, every clock quiet on steps k % 3 = 2 --
the quiet steps are where an ASYNCHRONOUS reset and a synchronous one differ.
`file:<path>` reads an `edges v1` schedule, so independent clock domains can be
driven independently: one line per listed step, `at <cycle> <b0> <b1> ...`,
`b<k>` = 1 when clock ordinal k fires; a step not listed fires every clock.
The legacy driver (make_legacy_sim.py --stim) reads the same file.

Output: one line per step, `STEP <k> <out0> <out1> ...`, each output as its
unsigned value (`bv_uint`). Runs the SPEC, not a residual: the FP drivers
separately check their residual against this same `interpretDesign`.

`route:vc` runs the VERIFIED COMPILER instead (`route:tv` is the old name and is still
accepted; it is not D3's translation validation): `compileDesign`
once, then its straight-line program (`denoteResidual`) every step -- the
program `compileDesign_correct` proves equal to `interpretDesign`. Same stimulus,
same output format, so the two routes' traces compare line for line. A
certificate `compileDesign` refuses is reported and exits 1.

On `route:vc` the compiled program is OPTIMIZED once before the first step
(`ResidualOpt.optimize`: bindings whose operands are all constants folded into
constants, unreachable bindings and unread sources dropped, identical constants shared), and `compileOpt_correct` proves the optimized program equal to
`interpretDesign` too, so the trace is the same. `opt:off` runs the unoptimized program;
`opt:stats` prints, on stderr, the program's size before and after and whether the
optimizer's certificate was accepted (a refused one runs the unoptimized program).

On `route:vc` every step after the first is INCREMENTAL (`ResidualInc.incStep`): it starts
from the previous step's environment and recomputes only the bindings an operand of which
changed value, keeping the others' previous values. `runIncOpt_correct` proves such a run is
`interpretDesign`'s run, step for step, for a program in which every binding reads only
earlier slots (`ResidualInc.wellOrdered`, checked once here; a program that fails it runs the
full step every time). `inc:off` runs the full step every time.

The incremental step is EVENT-DRIVEN by default (`ResidualInc.evStep`, `Compiler/ResidualEv`):
a slot whose value changed marks the bindings that read it (`fanout`, built once per program)
pending, and only pending bindings are visited, so a step costs what changed, not the program's
size. `runEvOpt_correct` proves such a run is `interpretDesign`'s run (`fanout_ok`: the table
lists every reader). `inc:scan` runs the earlier incremental step (`incStep`), which visits
every binding to see whether an operand changed; both give the same lines.

Checkpoints (`lhdcert-ckpt v1`, `Compiler/Ckpt.lean`, `pass/lean/tools/formats/ckpt.md`):

    ckpt-in:<file>       start from this checkpoint instead of the power-on state. It is
                         validated against the certificate (exit 1 when refused). The run
                         starts at the checkpoint's cycle c and prints STEP c .. c+cycles-1:
                         cycle numbers are absolute, for the stimulus, the edges and the
                         output alike, so the lines equal the same lines of a run from 0.
                         The checkpoint's ext sections are not read (a note on stderr).
    ckpt-at:<k>:<file>   write the state reached after k steps (before step k), at absolute
                         cycle k, c <= k <= c+cycles; repeatable
    ckpt-out:<file>      write the final state, at cycle c+cycles

Both routes write the same bytes for the same state, and each resumes from the other's file:
`ckpt-at:k` of one run, then `ckpt-in` of a second run, prints exactly the lines from k on.

    compact:<K>          every K cycles, replace the state by its restored checkpoint, in
                         memory (no file); the number made is printed on stderr. A memory is
                         a function that adds one layer per write, so a read walks back
                         through every write since the start (or since the last
                         compaction); a restored state holds each memory as one table.
                         The lines do not change: `HDLProof/Run/Sound.lean`'s
                         `compacted_outputs` (from `resume_exact`) for the reference trace,
                         and on route:vc `ResidualInc.incStep_correct` holds for any state,
                         so the incremental step continues from its environment.
                         `compact:0` turns compaction off.
    Default: automatic. A run longer than 256 cycles (`autoCompactK`) of a design with
    memories compacts every 256 cycles, when the certificate meets the theorem's hypotheses
    (`compactBlocker`: `checkDesign`, `addrError`, `imgRootError`, `wordError`; otherwise a
    note on stderr and no compaction). A shorter run, or a design without memories, never
    compacts, so its stderr is unchanged.

Each cycle's inputs come from a table built once (`StimTab`), with `inputAt`'s rules.
-/
import LeanSemanticPrimitives.Projection.CertLoadAny
import LeanSemanticPrimitives.Compiler.DesignSemantics
import LeanSemanticPrimitives.Compiler.DirectSemantics
import LeanSemanticPrimitives.Compiler.DirectStaged
import LeanSemanticPrimitives.Compiler.CompileDesignDefs
-- route:vc runs `denoteResidual`'s fast spelling (`@[csimp]`, equal by proof)
import LeanSemanticPrimitives.Compiler.ResidualFast
-- route:vc runs the optimized program (`compileOpt_correct`)
import LeanSemanticPrimitives.Compiler.ResidualOpt
-- checkpoints: lhdcert-ckpt v1
import LeanSemanticPrimitives.Compiler.Ckpt
-- route:vc steps incrementally (`runIncOpt_correct`)
import LeanSemanticPrimitives.Compiler.ResidualInc
-- route:vc's default incremental step is event-driven (`runEvOpt_correct`)
import LeanSemanticPrimitives.Compiler.ResidualEv
import Std.Data.HashMap

open Compiler

structure Stim where
  every : List (Nat × Nat × Int) := []
  at_   : List (Nat × Nat × Nat × Int) := []

def parseStim (txt : String) : Except String Stim := do
  let body := ((txt.splitOn "\n").map (fun l => (l.splitOn "\r").headD "")).filter
                (fun l => l != "" && !l.startsWith "#")
  match body with
  | [] => .error "empty stimulus file"
  | hdr :: rest =>
    if hdr != "stim v1" then .error s!"line 1: expected \"stim v1\", got {repr hdr}"
    -- in file order, pushed to arrays (appending to a list copied it: quadratic)
    let mut ats : Array (Nat × Nat × Nat × Int) := #[]
    let mut evs : Array (Nat × Nat × Int) := #[]
    for l in rest do
      match (l.splitOn " ").filter (· != "") with
      | ["at", c, "in", i, w, v] =>
          match c.toNat?, i.toNat?, w.toNat?, v.toInt? with
          | some c, some i, some w, some v => ats := ats.push (c, i, w, v)
          | _, _, _, _ => throw s!"unparseable {repr l}"
      | ["in", i, w, v] =>
          match i.toNat?, w.toNat?, v.toInt? with
          | some i, some w, some v => evs := evs.push (i, w, v)
          | _, _, _ => throw s!"unparseable {repr l}"
      | _ => throw s!"unrecognised {repr l}"
    pure { every := evs.toList, at_ := ats.toList }

/-- The declared width of input ordinal `i` (from its `SourceDesc.input`), or 1. -/
def inputWidth (D : DesignCert) (i : Nat) : Nat :=
  (D.sources.toList.findSome? fun
    | .input idx w => if idx == i then some w else none
    | _ => none).getD 1

def maxInput (D : DesignCert) : Nat :=
  D.sources.foldl (fun m s => match s with
    | .input idx _ => max m (idx + 1)
    | .flopQAsync _ _ ri _ _ => max m (ri + 1)
    | _ => m) 0

def inputAt (D : DesignCert) (st : Stim) (k : Nat) : RuntimeInput :=
  (List.range (maxInput D)).toArray.map fun i =>
    match st.at_.find? (fun t => t.1 == k && t.2.1 == i) with
    | some (_, _, w, v) => mk_bv w v
    | none =>
      match st.every.find? (fun t => t.1 == i) with
      | some (_, w, v) => mk_bv w v
      | none           => mk_bv (inputWidth D i) 0

/-- The stimulus as tables, built once per run: each input's value on a cycle no `at` line
names it (its first `in` line, else 0 at its declared width), and the `at` lines grouped by
cycle, in file order. `StimTab.inputAt` gives `inputAt`'s inputs (the same first-match rules)
without scanning the stimulus and the design's sources on every cycle. Keep it in step with
`HDLProof/Run/Stim.lean`'s copy, which `tests/stim_tab_check.lean` checks against `inputAt`. -/
structure StimTab where
  base : RuntimeInput
  at_  : Std.HashMap Nat (Array (Nat × Nat × Int))

def StimTab.of (D : DesignCert) (st : Stim) : StimTab := Id.run do
  let n := maxInput D
  -- each ordinal's width: its first `SourceDesc.input` (else 1)
  let mut width : Array (Option Nat) := Array.replicate n none
  for s in D.sources do
    match s with
    | .input idx w => if idx < n && width[idx]!.isNone then width := width.set! idx (some w)
    | _ => pure ()
  -- each ordinal's value when no `at` line names it: its first `in` line
  let mut every : Array (Option (Nat × Int)) := Array.replicate n none
  for (i, w, v) in st.every do
    if i < n && every[i]!.isNone then every := every.set! i (some (w, v))
  let base := (Array.range n).map fun i =>
    match every[i]! with
    | some (w, v) => mk_bv w v
    | none => mk_bv (width[i]!.getD 1) 0
  let mut at_ : Std.HashMap Nat (Array (Nat × Nat × Int)) := {}
  for (c, i, w, v) in st.at_ do
    at_ := at_.insert c ((at_.getD c #[]).push (i, w, v))
  return { base, at_ }

/-- Cycle `k`'s inputs: an input's first `at` line for cycle `k` wins, else its base value. -/
def StimTab.inputAt (t : StimTab) (k : Nat) : RuntimeInput :=
  match t.at_.get? k with
  | none => t.base
  | some ls => ls.foldr (fun (i, w, v) acc => if i < acc.size then acc.set! i (mk_bv w v) else acc) t.base

/-- The period of automatic compaction, in cycles. Measured on the census designs: a
compaction costs 14-70 ms, 3-15 cycles' worth, so every 256 cycles it adds 1.4-5% and keeps
the time per cycle flat. -/
def autoCompactK : Nat := 256

/-- Why compaction is not covered by `compacted_outputs` on this certificate, or `none`: the
theorem's hypotheses, checked (`checkDesign_sound` gives `DesignSemWF`). -/
def compactBlocker (D : DesignCert) : Option String :=
  match Compiler.Direct.checkDesign D with
  | .error e => some s!"checkDesign: {e.render}"
  | .ok _ => (MemAddr.addrError D).orElse fun _ =>
      (Ckpt.imgRootError D).orElse fun _ => Ckpt.wordError D

def zeroState (D : DesignCert) : RuntimeState :=
  { flops := D.flops.map (fun f => mk_bv f.width 0)
  , mems  := D.memories.map (fun m => fun _ => mk_bv m.dw 0) }

/-- Every flop at its reset value, every memory at its power-on image (DCERT3
`MemoryDesc.init`, 0 past its words): the default start. A flop with a Verilog
power-on value and no reset carries that value as `resetValue`, so this is the
start that matches the RTL; it is also the Futamura runner's (`initStateOf`).
`init:zero` keeps the old all-zero start. -/
def resetState (D : DesignCert) : RuntimeState :=
  { flops := D.flops.map (fun f => mk_bv f.width f.resetValue)
  , mems  := D.memories.map (fun m => m.powerOnImg) }

/-- An `edges v1` schedule: `at <cycle> <b0> <b1> ...`, one bit per clock ordinal. -/
def parseEdges (txt : String) : Except String (List (Nat × Array Bool)) := do
  let body := ((txt.splitOn "\n").map (fun l => (l.splitOn "\r").headD "")).filter
                (fun l => l != "" && !l.startsWith "#")
  match body with
  | [] => .error "empty edges file"
  | hdr :: rest =>
    if hdr != "edges v1" then .error s!"line 1: expected \"edges v1\", got {repr hdr}"
    rest.mapM fun l =>
      match (l.splitOn " ").filter (· != "") with
      | "at" :: c :: bits =>
          match c.toNat?, bits.mapM (fun b => match b with | "0" => some false | "1" => some true | _ => none) with
          | some c, some bs => pure (c, bs.toArray)
          | _, _ => throw s!"unparseable {repr l}"
      | _ => throw s!"unrecognised {repr l}"

def edgesAt (D : DesignCert) (third : Bool) (sched : Option (List (Nat × Array Bool))) (k : Nat) : ClockEdges :=
  match sched with
  | some rows =>
    match rows.find? (fun r => r.1 == k) with
    | some (_, bs) => (Array.range D.clocks.size).map (fun c => bs.getD c true)
    | none => Array.replicate D.clocks.size true
  | none =>
    if third && k % 3 == 2 then Array.replicate D.clocks.size false
    else Array.replicate D.clocks.size true

def main (args : List String) : IO UInt32 := do
  match args with
  | cert :: cyc :: stimPath :: rest =>
    let some cycles := cyc.toNat? | IO.eprintln s!"cycles {repr cyc} is not a number"; return 2
    let mut third := false
    let mut zeroInit := false
    let mut vc := false
    let mut optOn := true
    let mut optStats := false
    let mut incOn := true
    let mut incScan := false
    let mut schedPath : Option String := none
    let mut ckptIn : Option String := none
    let mut ckptAt : Array (Nat × String) := #[]
    let mut ckptOut : Option String := none
    let mut compactK : Nat := 0
    let mut compactAuto := true
    for opt in rest do
      if opt.startsWith "edges:file:" then
        schedPath := some (opt.replace "edges:file:" "")
      else if opt.startsWith "ckpt-in:" then
        ckptIn := some (opt.replace "ckpt-in:" "")
      else if opt.startsWith "ckpt-out:" then
        ckptOut := some (opt.replace "ckpt-out:" "")
      else if opt.startsWith "compact:" then
        match (opt.replace "compact:" "").toNat? with
        | some k => compactK := k; compactAuto := false
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
        | "edges:all" => third := false
        | "edges:third" => third := true
        | "init:reset" => zeroInit := false
        | "init:zero" => zeroInit := true
        | "route:ir" => vc := false
        | "route:vc" => vc := true
        | "route:tv" => vc := true   -- the old name of route:vc
        | "opt:off" => optOn := false
        | "opt:stats" => optStats := true
        | "inc:off" => incOn := false
        | "inc:scan" => incScan := true
        | _ => do IO.eprintln s!"unknown option {opt}"; return 2
    let (_, D) ← Projection.CertLoadAny.loadAny cert
    let st ← match parseStim (← IO.FS.readFile stimPath) with
      | .ok s => pure s
      | .error e => do IO.eprintln s!"stimulus: {e}"; return 2
    let sched ← match schedPath with
      | none => pure none
      | some path => match parseEdges (← IO.FS.readFile path) with
        | .ok rows => pure (some rows)
        | .error e => do IO.eprintln s!"edges: {e}"; return 2
    let tab := StimTab.of D st
    -- automatic compaction: on a run long enough to reach it, for a design with memories whose
    -- certificate meets `compacted_outputs`' hypotheses
    if compactAuto && cycles > autoCompactK && D.memories.size > 0 then
      match compactBlocker D with
      | none => compactK := autoCompactK
      | some why => IO.eprintln s!"automatic compaction off: {why}"
    let mut s := if zeroInit then zeroState D else resetState D
    -- checkpoints: the design hash is computed only when one is read or written
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
        s := Ckpt.restore c
        start := c.cycle
    for (k, p) in ckptAt do
      unless start ≤ k && k ≤ start + cycles do
        IO.eprintln s!"ckpt-at:{k}:{p}: cycle {k} is outside this run ({start} .. {start + cycles})"
        return 2
    -- write every checkpoint due at cycle `k`, state `st`
    let saveAt (k : Nat) (st : RuntimeState) : IO Unit := do
      for (j, p) in ckptAt do
        if j == k then IO.FS.writeBinFile p (Ckpt.render (Ckpt.save D hash k #[] st))
      if k == start + cycles then
        if let some p := ckptOut then IO.FS.writeBinFile p (Ckpt.render (Ckpt.save D hash k #[] st))
    if vc then
      match compileDesign D with
      | .error e => do IO.eprintln s!"compileDesign refused: {repr e}"; return 1
      | .ok R0 =>
        let (R, accepted) := if optOn then ResidualOpt.optimizeWith R0 else (R0, false)
        if optStats then
          let verdict := if !optOn then "off" else if accepted then "accepted" else "REFUSED (unoptimized)"
          IO.eprintln s!"vc optimize: {verdict}; sources {R0.sources.size} -> {R.sources.size}, \
            bindings {R0.bindings.size} -> {R.bindings.size}"
        let inc := incOn && ResidualInc.wellOrdered R
        if optStats then
          IO.eprintln s!"vc incremental: {if inc then (if incScan then "on (scan)" else "on (event-driven)") else if incOn then "off (not well-ordered)" else "off"}"
        -- the steps of `ResidualInc.runEv` (or `runInc`, inc:scan): the first (also the first
        -- after a checkpoint) full, the others from the last step's environment (`env`, updated
        -- in place); the fanout table built once
        let fan := if inc && !incScan then ResidualInc.fanout R else #[]
        let mut env : SlotEnv := #[]
        let mut compactions := 0
        for k in List.range' start cycles do
          saveAt k s
          if compactK > 0 && k > start && (k - start) % compactK == 0 then
            s := Ckpt.restore (Ckpt.save D hash k #[] s)
            compactions := compactions + 1
          let e := edgesAt D third sched k
          let i := tab.inputAt k
          let r ← if !inc then pure (denoteResidual R e i s)
            else if k == start then do
              let env0 := ResidualInc.fullEnv R i s
              env := env0
              pure (ResidualInc.stepOfEnv R env0 e s)
            else do
              let p := if incScan then ResidualInc.incStep R env e i s
                else ResidualInc.evStep R fan env e i s
              env := p.2
              pure p.1
          IO.println s!"STEP {k} {" ".intercalate (r.outputs.toList.map (fun o => toString (bv_uint o)))}"
          s := r.nextState
        saveAt (start + cycles) s
        if compactK > 0 then
          (← IO.getStdout).flush  -- after every STEP line, also when both go to one file
          IO.eprintln s!"compactions {compactions} (every {compactK} cycles{if compactAuto then ", automatic" else ""})"
        return 0
    -- `directStep` is proved equal to `interpretDesign` on every input it
    -- accepts (`directStep_correct`; it refuses a certificate or runtime shape
    -- outside the checked fragment instead of stepping it). Executing
    -- `interpretDesign` itself is exponential in the number of steps on a
    -- design with a memory: measured on a 16x4 register file, 7 s for 10
    -- steps and about 2.3x per extra step, against 0.6 s for 200 steps here.
    -- `directStep` checks the certificate (`checkDesign`) and then the step's
    -- shapes; the certificate check reads only the certificate, so it runs once
    -- here, and each step runs `directStepChecked`, which is `directStep` on
    -- every step once the check has passed (`directStepChecked_eq`). A refused
    -- certificate is reported at the first step, as `directStep` reports it.
    -- Each step runs the STAGED form (`Compiler/DirectStaged`): the design is prepared once
    -- (every node specialized, constants folded), and `Staged.stepChecked_eq` proves the staged
    -- step equal to `directStepChecked` for a certificate `checkDesign` accepts.
    match Compiler.Direct.checkDesign D with
    | .error e =>
      if cycles > 0 then
        IO.eprintln s!"step {start}: refused: {e.render}"
        return 1
      saveAt start s
      return 0
    | .ok _ =>
      let P := Compiler.Staged.prepare D
      let mut compactions := 0
      for k in List.range' start cycles do
        saveAt k s
        if compactK > 0 && k > start && (k - start) % compactK == 0 then
          s := Ckpt.restore (Ckpt.save D hash k #[] s)
          compactions := compactions + 1
        let r ← match Compiler.Staged.stepChecked P (edgesAt D third sched k) (tab.inputAt k) s with
          | .ok r => pure r
          | .error e => do IO.eprintln s!"step {k}: refused: {e.render}"; return 1
        IO.println s!"STEP {k} {" ".intercalate (r.outputs.toList.map (fun o => toString (bv_uint o)))}"
        s := r.nextState
      saveAt (start + cycles) s
      if compactK > 0 then
        (← IO.getStdout).flush  -- after every STEP line, also when both go to one file
        IO.eprintln s!"compactions {compactions} (every {compactK} cycles{if compactAuto then ", automatic" else ""})"
      return 0
  | _ =>
    IO.eprintln "usage: cert_trace <cert> <cycles> <stim> [edges:all|edges:third] [init:reset|init:zero] [route:ir|route:vc] [opt:off] [opt:stats] [inc:off|inc:scan] [ckpt-in:<file>] [ckpt-at:<cycle>:<file>] [ckpt-out:<file>] [compact:<cycles>]"
    return 2
