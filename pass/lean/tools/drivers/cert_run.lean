/-
# `cert_run` — run a program on a design

```
cert_run <cert> [bind:<file>] [stim:<file>] [edges:all|edges:third|edges:file:<path>]
         [route:ir|vc|ref|fp] [opt:off] [init:reset|init:zero] [ckpt-in:<file>]
         [load:<image>[@<addr>][:mem<k>]] [vaddr] [word:<bytes>]
         [max:<cycles>] [stop:<output>=<value>]
         [trace:<dir>] [vcd:<file>] [lockstep:<route>]
         [ckpt-at:<cycle>:<file>] [ckpt-out:<file>] [compact:<cycles>]
         [inc:off|inc:scan] [mem:fun] [share:off] [ir:raw]
```

A run steps the design cycle by cycle on a proved simulator (`route:ir`, the default; `vc`;
`ref`, `interpretDesign` itself; `fp`, the Futamura projection's residual) until a stop
condition. `pass/lean/tools/formats/run.md`
documents every option; in short:

* Inputs come from a `stim v1` file (`stim:`, absolute cycles) or are 0, then from the bind
  file's constants and reset. A bind file (`bind:`, `formats/bind.md`) adds a host: memory
  regions served on valid/ready buses, a console, an `exit` register, named registers.
* `load:` places a program image (ELF, `.bin` at `@<addr>`, `$readmemh` `.hex`) into the host
  memory, or with `:mem<k>` into the design's own memory `k` (byte `a` into word
  `(a - addr) / (dw / 8)`, little-endian; a `.hex` file's words go to its word addresses).
* The run stops on: `exit` (a write to the exit register; its data is the exit code),
  `stop-output` (a bind `stop-output` or `stop:<output>=<value>`, sampled before the edge),
  `limit` (`max:` cycles, default 1,000,000), `breakpoint`, `watchpoint`, `protocol` (a bus
  request changed before it completed), `mealy` (an output depended on `ready`/`rdata` in the
  same cycle), `bus-error`, `rom-write`, `lockstep` (the second route's outputs differed), or
  `refused` (the simulator refused a step).
* `trace:<dir>` writes `steps.trace` (`STEP <k> <outputs>`, `cert_trace`'s format),
  `inputs.stim` (every applied input, `stim v1`), `edges.edges` (when not every clock fires
  every cycle), `bus.trace` and `console.txt`. `cert_trace <cert> <n> inputs.stim` replays the
  run open-loop and prints the same `steps.trace`; `HDLProof/Run/Sound.lean`'s
  `runCycles_sound` proves that a run is the reference trace of the ticks it applied.
* Checkpoints are `lhdcert-ckpt v1` (`formats/ckpt.md`); a bind run keeps its host in the ext
  section `cert_run host v1`, so `ckpt-in:` resumes the whole machine.
* `compact:<K>` replaces the simulator state by its restored checkpoint every `K` cycles, in
  memory (no file). The `ir`, `vc` and `ref` routes keep each memory as a function built from
  every earlier cycle, so a run's memory use and the cost of a memory read grow with its length;
  a restored state holds each memory as one table. The outputs do not change:
  `Projection/CkptResume.lean`'s `resume_exact` proves that a run from the restored checkpoint
  of a state prints what a run from the state prints, and `HDLProof/Run/Sound.lean`'s
  `compacted_outputs` chains it over the segments of a run. `compact:0` turns it off.
  Default: automatic. A run whose limit passes 256 cycles (`autoCompactK`), of a design with
  memories, compacts every 256 cycles when the certificate meets the theorem's hypotheses
  (`compactBlocker`: `checkDesign`, `addrError`, `imgRootError`, `wordError`; otherwise the
  summary says why not, and nothing is compacted). The summary's `compactions` line then ends
  in `automatic`.
* A stimulus file's inputs come from a table built once (`StimTab`), with `inputAt`'s rules.
* The route runs as an engine (`HDLProof/Run/Engine`'s `EngineSelector`, from `Knobs`): ir staged, vc
  optimized and event-driven with its memories as tables, fp with shared tails. `inc:off` runs
  vc's full step every cycle (this driver's earlier vc step), `inc:scan` and `mem:fun` its other
  modes, `share:off` fp without shared tails, `ir:raw` the unstaged direct step. Each cycle uses
  the engine's state once (`cycleStep`: vc peeks with the guess, then takes one step), and the
  loop is a recursive function, so vc's environment and tables are updated in place.

The summary ends with `RESULT: <status>`. Exit code: 0 for `exit` with code 0, `stop-output`,
`limit`, `breakpoint` and `watchpoint`; 1 for `exit` with another code and the failure
statuses; 2 for usage; 3 when the design, an image or a checkpoint is refused.
-/
import HDLProof.Run.Engine
import HDLProof.Run.Vcd
import LeanSemanticPrimitives.Compiler.Ckpt
import LeanSemanticPrimitives.Compiler.CertIORoute

open Compiler Compiler.Direct HDLProof HDLProof.Run

def say (s : String) : IO Unit := do IO.println s; (← IO.getStdout).flush

def usage : String :=
  "usage: cert_run <cert> [bind:<file>] [stim:<file>] [edges:all|edges:third|edges:file:<path>]\n" ++
  "       [route:ir|vc|ref|fp] [opt:off] [init:reset|init:zero] [ckpt-in:<file>]\n" ++
  "       [load:<image>[@<addr>][:mem<k>]] [vaddr] [word:<bytes>] [max:<cycles>] [stop:<output>=<value>]\n" ++
  "       [trace:<dir>] [vcd:<file>] [lockstep:<route>] [ckpt-at:<cycle>:<file>] [ckpt-out:<file>]\n" ++
  "       [compact:<cycles>] [inc:off|inc:scan] [mem:fun] [share:off] [ir:raw]"

structure Opts where
  bind     : Option String := none
  stim     : Option String := none
  edges    : String := "edges:all"
  route    : Route := .ir
  opt      : Bool := true
  zero     : Bool := false
  ckptIn   : Option String := none
  loads    : Array String := #[]
  vaddr    : Bool := false
  word     : Nat := 4
  max      : Nat := 1000000
  stops    : Array String := #[]
  trace    : Option String := none
  vcd      : Option String := none
  lockstep : Option Route := none
  ckptAt   : Array (Nat × String) := #[]
  ckptOut  : Option String := none
  compact  : Nat := 0
  /-- no `compact:` option: compact every `autoCompactK` cycles when the run and design allow -/
  compactAuto : Bool := true
  /-- the route's other knobs (`inc:`, `mem:`, `share:`, `ir:raw`); route and `opt:off` are above -/
  knobs    : Knobs := {}

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

def parseOpts (args : List String) : Except String Opts := do
  let mut o : Opts := {}
  for a in args do
    let drop (p : String) : String := (a.drop p.length).toString
    if a.startsWith "bind:" then o := { o with bind := some (drop "bind:") }
    else if a.startsWith "stim:" then o := { o with stim := some (drop "stim:") }
    else if a.startsWith "edges:" then o := { o with edges := a }
    else if a.startsWith "route:" then
      match Route.parse? (drop "route:") with
      | some r => o := { o with route := r }
      | none => throw s!"unknown route {a}"
    else if a == "opt:off" then o := { o with opt := false }
    else if a == "init:zero" then o := { o with zero := true }
    else if a == "init:reset" then o := { o with zero := false }
    else if a.startsWith "ckpt-in:" then o := { o with ckptIn := some (drop "ckpt-in:") }
    else if a.startsWith "load:" then o := { o with loads := o.loads.push (drop "load:") }
    else if a == "vaddr" then o := { o with vaddr := true }
    else if a.startsWith "word:" then
      match (drop "word:").toNat? with
      | some n => o := { o with word := n }
      | none => throw s!"bad {a}"
    else if a.startsWith "max:" then
      match (drop "max:").toNat? with
      | some n => o := { o with max := n }
      | none => throw s!"bad {a}"
    else if a.startsWith "stop:" then o := { o with stops := o.stops.push (drop "stop:") }
    else if a.startsWith "trace:" then o := { o with trace := some (drop "trace:") }
    else if a.startsWith "vcd:" then o := { o with vcd := some (drop "vcd:") }
    else if a.startsWith "lockstep:" then
      match Route.parse? (drop "lockstep:") with
      | some r => o := { o with lockstep := some r }
      | none => throw s!"unknown route {a}"
    else if a.startsWith "ckpt-at:" then
      match (drop "ckpt-at:").splitOn ":" with
      | k :: p@(_ :: _) => match k.toNat? with
        | some k => o := { o with ckptAt := o.ckptAt.push (k, ":".intercalate p) }
        | none => throw s!"bad {a}"
      | _ => throw s!"ckpt-at expects <cycle>:<file>, got {a}"
    else if a.startsWith "ckpt-out:" then o := { o with ckptOut := some (drop "ckpt-out:") }
    else if a.startsWith "compact:" then
      match (drop "compact:").toNat? with
      | some n => o := { o with compact := n, compactAuto := false }
      | none => throw s!"bad {a}"
    else if ["inc:", "mem:", "share:", "ir:"].any (fun (p : String) => a.startsWith p) then
      match o.knobs.apply? a with
      | some (.ok k) => o := { o with knobs := k }
      | some (.error e) => throw e
      | none => throw s!"unknown option {a}"
    else throw s!"unknown option {a}"
  return o

/-- `<path>[@<addr>][:mem<k>]` -/
structure LoadSpec where
  path : String
  addr : Option Nat
  mem  : Option Nat

def parseLoad (s : String) : Except String LoadSpec := do
  let (rest, mem) ← match s.splitOn ":mem" with
    | [p] => pure (p, none)
    | [p, k] => match k.toNat? with
      | some k => pure (p, some k)
      | none => throw s!"load: bad memory ordinal in `{s}`"
    | _ => throw s!"load: bad `{s}`"
  match rest.splitOn "@" with
  | [p] => return { path := p, addr := none, mem }
  | [p, a] => match parseNum? a with
    | some a => return { path := p, addr := some a, mem }
    | none => throw s!"load: bad address in `{s}`"
  | _ => throw s!"load: bad `{s}`"

/-- Words `ws` (index, value) into memory `k` of state `s`, below its depth. -/
def loadWords (D : DesignCert) (s : RuntimeState) (k : Nat) (ws : Array (Nat × Nat)) :
    Except String RuntimeState := do
  let some md := D.memories[k]? | throw s!"the design has no memory {k} ({D.memories.size} memories)"
  let m0 := Ckpt.saveMem md (s.mems[k]?.getD fun _ => mk_bv md.dw 0)
  for (a, _) in ws do
    if a ≥ md.depth then throw s!"word {a} is past memory {k}'s depth {md.depth}"
  let m := Ckpt.normalizeMem { m0 with words := m0.words ++ ws.map fun (a, v) => (a, v % 2 ^ md.dw) }
  -- the table is built here, once (`Ckpt.tableImage`), not on every read
  let t := Ckpt.memTable m
  return { s with mems := s.mems.set! k (Ckpt.tableImage m.dw m.dflt t) }

/-- The knobs of the run's route (`route:`, `opt:off` and the knob options). -/
def Opts.runKnobs (o : Opts) : Knobs :=
  { o.knobs with route := o.route, vcOpt := o.opt, fpShare := o.opt && o.knobs.fpShare }

def escapeBytes (b : ByteArray) : String :=
  b.foldl (fun s c =>
    if c == 10 then s ++ "\\n" else if c == 92 then s ++ "\\\\"
    else if 32 ≤ c && c < 127 then s.push (Char.ofNat c.toNat)
    else s ++ s!"\\x{String.ofList (Nat.toDigits 16 c.toNat)}") ""

def hexS (n : Nat) : String := String.ofList (Nat.toDigits 16 n)

/-- The loop's counters: the status that stopped it, applied cycles, compactions. -/
structure Tally where
  status      : String := "limit"
  applied     : Nat := 0
  compactions : Nat := 0

/-- The trace files: steps, inputs, bus transfers, and the edges when not every clock fires. -/
abbrev TraceH := IO.FS.Handle × IO.FS.Handle × IO.FS.Handle × Option IO.FS.Handle

/-- Cycles from `st` until a status stops the run: checkpoints due, the limit, compaction, one
`cycleStep`, the traces, lockstep (engine `L` on state `s2`). A recursive function rather than a
loop over a mutable variable, so each cycle holds the only reference to the engine's state
(`Compiler/Engine`'s loop rule); a self tail call, compiled as a jump. -/
partial def runLoop {σ σ2 : Type} (o : Opts) (ctx : Ctx) (io : IoMeta) (hash c0 : Nat)
    (E : Engine σ) (L : Option (Engine σ2)) (traceH : Option TraceH) (vcd : Option Vcd)
    (saveTo : String → LoopState σ → IO Unit) (st : LoopState σ) (s2 : Option σ2) (t : Tally) :
    IO (LoopState σ × Tally) := do
  let k := st.cycle
  for (j, p) in o.ckptAt do
    if j == k then saveTo p st
  if k ≥ c0 + o.max then return (st, { t with status := "limit" })
  let compactNow := o.compact > 0 && k > c0 && (k - c0) % o.compact == 0
  let st := if compactNow then
      { st with s := E.reload st.s (Ckpt.restore (Ckpt.save ctx.D hash k #[] (E.view st.s))) }
    else st
  let s2 := if compactNow then
      match L, s2 with
      | some l, some q2 => some (l.reload q2 (Ckpt.restore (Ckpt.save ctx.D hash k #[] (l.view q2))))
      | _, s2 => s2
    else s2
  let t := if compactNow then { t with compactions := t.compactions + 1 } else t
  match cycleStep ctx E st with
  | .error (e, st) =>
    IO.eprintln s!"cycle {k}: {e}"
    return (st, { t with status := "refused" })
  | .ok co =>
    let t := if co.applied then { t with applied := t.applied + 1 } else t
    if co.applied then
      if let some (sh, ih, bh, eh) := traceH then
        sh.putStrLn s!"STEP {k} {" ".intercalate (co.outs.toList.map fun b => toString (bv_uint b))}"
        for i in [0:co.tick.input.size] do
          let v := (bv_uint co.tick.input[i]!).toNat
          if v != 0 then ih.putStrLn s!"at {k} in {i} {co.tick.input[i]!.width} {v}"
        for ev in co.events do
          bh.putStrLn s!"{ev.arrival} {ev.done} {ev.kind} {hexS ev.addr} {hexS ev.data} {hexS ev.strb}"
        if let some h := eh then
          h.putStrLn s!"at {k} {" ".intercalate (co.tick.edges.toList.map fun b => if b then "1" else "0")}"
      if let some v := vcd then
        v.sample k (co.tick.input.map (fun b => (bv_uint b).toNat) ++ co.outs.map (fun b => (bv_uint b).toNat))
    -- lockstep: the second engine on the applied tick
    let lock : Except Unit (Option σ2) ← match L, s2, co.applied with
      | some l, some q2, true =>
        match l.step co.tick.edges co.tick.input q2 with
        | .error (e, _) => do
          IO.eprintln s!"lockstep {l.name}, cycle {k}: {e}"
          pure (.error ())
        | .ok (o2, q2') =>
          if o2 != co.outs then do
            let bad := (List.range co.outs.size).find? (fun j => o2[j]? != co.outs[j]?)
            IO.eprintln s!"lockstep: cycle {k}: {l.name} output {(bad.map io.outputName).getD "?"} differs"
            pure (.error ())
          else pure (.ok (some q2'))
      | _, s2, _ => pure (.ok s2)
    match lock with
    | .error () => return (co.next, { t with status := "lockstep" })
    | .ok s2 =>
      if let some why := co.status then return (co.next, { t with status := why })
      runLoop o ctx io hash c0 E L traceH vcd saveTo co.next s2 t

/-- The run proper on engine `E` (and lockstep engine `L`): the sinks, the loop, the summary. -/
def runWith {σ σ2 : Type} (o : Opts) (D : DesignCert) (io : IoMeta) (bind : Bind)
    (edges : EdgeSpec) (hash : Nat) (ctx : Ctx) (s0 : RuntimeState) (h0 : HostState) (c0 : Nat)
    (E : Engine σ) (L : Option (Engine σ2)) : IO UInt32 := do
  -- sinks
  let traceH ← match o.trace with
    | none => pure none
    | some dir => do
      IO.FS.createDirAll dir
      let mk (f : String) : IO IO.FS.Handle := IO.FS.Handle.mk (System.FilePath.mk dir / f) .write
      let st ← mk "steps.trace"; let inp ← mk "inputs.stim"; let bus ← mk "bus.trace"
      inp.putStrLn "stim v1"
      let ed ← match edges with
        | .all => pure none
        | _ => do let h ← mk "edges.edges"; h.putStrLn "edges v1"; pure (some h)
      pure (some (st, inp, bus, ed))
  let vcdPorts := (Array.range ctx.inW.size).map (fun k => (io.inputName k, ctx.inW[k]!)) ++
    (Array.range D.outputs.size).map (fun k => (io.outputName k, D.outputs[k]!.width))
  let vcd : Option Vcd ← match o.vcd with
    | none => pure none
    | some p => do pure (some (← Vcd.create p vcdPorts))
  let saveTo (p : String) (st : LoopState σ) : IO Unit := do
    let exts := if bind.regions.isEmpty && bind.buses.isEmpty then #[]
      else #[{ name := extName, bytes := (st.host.encode).toList : Ckpt.Ext }]
    IO.FS.writeBinFile p (Ckpt.render (Ckpt.save D hash st.cycle exts (E.view st.s)))
  say s!"cert_run: route {E.name}, {D.flops.size} flops, {D.memories.size} memories, \
{bind.buses.size} buses, {bind.regions.size} regions; start cycle {c0}"
  let t0 ← IO.monoNanosNow
  let (st, t) ← runLoop o ctx io hash c0 E L traceH vcd saveTo
    { s := E.load s0, host := h0, cycle := c0 } (L.map fun l => l.load s0) {}
  let t1 ← IO.monoNanosNow
  if let some p := o.ckptOut then saveTo p st
  if let some (sh, ih, bh, eh) := traceH then
    sh.flush; ih.flush; bh.flush
    if let some h := eh then h.flush
    IO.FS.writeBinFile (System.FilePath.mk o.trace.get! / "console.txt") st.host.console
  if let some v := vcd then v.h.flush
  let ns := t1 - t0
  let status := t.status
  let applied := t.applied
  let compactions := t.compactions
  say s!"status {status}"
  say s!"cycles {st.cycle}"
  say s!"applied {applied}"
  if let some c := st.host.exitCode then say s!"exit_code {c}"
  unless st.host.console.isEmpty do say s!"console {escapeBytes st.host.console}"
  for (a, kind) in bind.devices do
    if let .word nm := kind then
      say s!"word {nm} {match st.host.words.find? (·.1 == a) with | some (_, v) => hexS v | none => "-"}"
  unless bind.buses.isEmpty do
    say s!"bus reads {st.host.stats.reads} writes {st.host.stats.writes} fetches {st.host.stats.fetches} wait_cycles {st.host.stats.waits}"
  if o.compact > 0 then
    say s!"compactions {compactions} (every {o.compact} cycles{if o.compactAuto then ", automatic" else ""})"
  say s!"wall_ms {ns / 1000000} cycles_per_s {if ns == 0 then 0 else applied * 1000000000 / ns}"
  say s!"RESULT: {status}"
  let ok := ["stop-output", "limit", "breakpoint", "watchpoint"].contains status ||
    (status == "exit" && st.host.exitCode == some 0)
  return (if ok then 0 else 1)

/-- The run on engine `E`: the start state (power-on, zero, or a checkpoint), the images, the
checkpoint cycles, the compaction policy and the lockstep engine; then `runWith`. -/
def runFrom {σ : Type} (o : Opts) (D : DesignCert) (io : IoMeta) (bind : Bind) (stim : Option Stim)
    (edges : EdgeSpec) (E : Engine σ) : IO UInt32 := do
  -- the start: power-on (or zero), or a checkpoint (with its host section)
  let hash := Ckpt.designHash D
  let mut s0 := if o.zero then zeroState D else resetState D
  let mut h0 := HostState.fresh bind
  let mut c0 := 0
  if let some p := o.ckptIn then
    match Ckpt.parse (← IO.FS.readBinFile p) with
    | .error e => IO.eprintln s!"{p}: {e}"; say "RESULT: refused"; return 3
    | .ok c =>
      if let some e := Ckpt.validate D hash c then
        IO.eprintln s!"{p}: {e}"; say "RESULT: refused"; return 3
      s0 := Ckpt.restore c
      c0 := c.cycle
      if let some ext := c.exts.find? (·.name == extName) then
        match HostState.decode ⟨ext.bytes.toArray⟩ with
        | .ok h =>
          unless h.fits bind do
            IO.eprintln s!"{p}: the host section does not fit the bind file (regions or buses differ)"
            return 3
          h0 := h
        | .error e => IO.eprintln s!"{p}: {e}"; return 3
  -- program images
  for l in o.loads do
    let spec ← match parseLoad l with
      | .ok s => pure s
      | .error e => do IO.eprintln e; return 2
    match spec.mem with
    | some k =>
      let some md := D.memories[k]? | do IO.eprintln s!"load: the design has no memory {k}"; return 3
      let ws ← if (System.FilePath.mk spec.path).extension == some "hex" then
          match parseHexWords (← IO.FS.readFile spec.path) with
          | .ok ws => pure (ws.map fun (a, v) => (a + spec.addr.getD 0, v))
          | .error e => do IO.eprintln s!"{spec.path}: {e}"; return 3
        else
          let img ← loadImage spec.path spec.addr (md.dw / 8) o.vaddr
          match toWords img (spec.addr.getD 0) md.dw md.depth with
          | .ok ws => pure ws
          | .error e => do IO.eprintln s!"{spec.path}: {e}"; return 3
      match loadWords D s0 k ws with
      | .ok s => s0 := s
      | .error e => IO.eprintln s!"{spec.path}: {e}"; return 3
      say s!"loaded {spec.path}: {ws.size} words into memory {k}"
    | none =>
      if bind.regions.isEmpty then
        IO.eprintln s!"load: {spec.path}: no host memory (give a bind file with ram/rom regions, or :mem<k>)"
        return 2
      let img ← loadImage spec.path spec.addr o.word o.vaddr
      match h0.load bind img with
      | .ok h => h0 := h
      | .error e => IO.eprintln s!"{spec.path}: {e}"; return 3
      say s!"loaded {spec.path}: {img.segments.size} segments, entry 0x{hexS img.entry}"
  for (k, p) in o.ckptAt do
    unless c0 ≤ k && k ≤ c0 + o.max do
      IO.eprintln s!"ckpt-at:{k}:{p}: cycle {k} is outside this run ({c0} .. {c0 + o.max})"; return 2
  let ctx := Ctx.mk' D bind stim edges
  -- automatic compaction: on a run that may reach it, for a design with memories whose
  -- certificate meets `compacted_outputs`' hypotheses
  let mut o := o
  if o.compactAuto && o.max > autoCompactK && D.memories.size > 0 then
    match compactBlocker D with
    | none => o := { o with compact := autoCompactK }
    | some why => say s!"automatic compaction off: {why}"
  match o.lockstep with
  | none => runWith o D io bind edges hash ctx s0 h0 c0 E (none : Option (Engine Unit))
  | some r =>
    match EngineSelector D { o.runKnobs with route := r } with
    | .error e => IO.eprintln s!"lockstep: {e.message}"; return 3
    | .ok b2 => runWith o D io bind edges hash ctx s0 h0 c0 E (some b2.engine)

def main (args : List String) : IO UInt32 := do
  match args with
  | [] => IO.eprintln usage; return 2
  | cert :: rest =>
  let o ← match parseOpts rest with
    | .ok o => pure o
    | .error e => do IO.eprintln s!"{e}\n{usage}"; return 2
  try
    let D ← match CertIO.admitAny (← IO.FS.readBinFile cert) with
      | .ok (_, D) => pure D
      | .error e => do IO.eprintln s!"{cert}: {e}"; say "RESULT: refused"; return 3
    let io ← IoMeta.load cert
    let inW (k : Nat) : Nat := inputWidth D k
    let bind ← match o.bind with
      | none => pure ({} : Bind)
      | some p => match Bind.parse io inW (← IO.FS.readFile p) with
        | .ok b => pure b
        | .error e => do IO.eprintln s!"{p}: {e}"; return 2
    let bind := { bind with stops := bind.stops ++ (← o.stops.mapM fun t => do
      match t.splitOn "=" with
      | [nm, v] => match io.outputOrd nm, parseNum? v with
        | .ok k, some v => pure (k, v)
        | .error e, _ => throw (IO.userError e)
        | _, none => throw (IO.userError s!"stop:{t}: bad value")
      | _ => throw (IO.userError s!"stop:{t}: expected <output>=<value>")) }
    let stim ← match o.stim with
      | none => pure none
      | some p => match parseStim (← IO.FS.readFile p) with
        | .ok s => pure (some s)
        | .error e => do IO.eprintln s!"{p}: {e}"; return 2
    let edges ← match ← readEdgeSpec o.edges with
      | .ok e => pure e
      | .error e => do IO.eprintln e; return 2
    match EngineSelector D o.runKnobs with
    | .error e => IO.eprintln e.message; say "RESULT: refused"; return 3
    | .ok b => runFrom o D io bind stim edges b.engine
  catch e =>
    IO.eprintln s!"cert_run: {e}"
    return 3
