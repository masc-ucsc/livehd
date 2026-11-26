/-
`cert_trace` -- the per-step OUTPUTS of `interpretDesign` on a certificate
(computed by `directStep`, proved equal to it; the certificate check runs once; route:ir runs
its staged form, `Compiler/DirectStaged`, proved equal by `Staged.stepChecked_eq`),
under a supplied stimulus. The oracle side of an RTL differential: the same
`stim v1` file drives an event simulator on the original RTL, and the two
traces are compared line by line.

    lean --run Futamura_Projection_Scripts/cert_trace.lean <cert> <cycles> <stim>
         [edges:all|edges:third|edges:file:<path>] [init:reset|init:zero] [route:ir|vc|ref|fp]
         [opt:off] [opt:stats] [inc:off|inc:scan] [mem:fun] [share:off] [loop:full] [ir:raw]
         [ckpt-in:<file>] [ckpt-at:<cycle>:<file>] [ckpt-out:<file>] [compact:<cycles>]

The route runs as the engine `EngineSelector` builds from the route options (`HDLProof/Run/Engine`;
route:ir is this driver's default), and the loop is `HDLProof/Run/Trace`'s `traceRun`, the one
per-design TV and legacy drivers run too: the same stimulus table, edge schedules, start states,
checkpoints and compaction for every route. `route:ref` runs `interpretDesign` itself;
`route:fp` the Futamura projection's residual on its encoded state (`Run/FpEngine`; `share:off`
without shared tails, `loop:full` its earlier step); `ir:raw` the unstaged direct step.

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

The event-driven step keeps each memory as a TABLE (`Compiler/ResidualFlat`): a write copies
one chunk instead of adding a closure in front of the image, so a read costs the same at any
age. `runEvFlatOpt_correct` proves such a run is `interpretDesign`'s run from the state the
tables stand for (`absS_ofState`: tables built from the start state stand for it). Automatic
compaction is not needed then and is skipped; an explicit `compact:<K>` still compacts. The
pass keeps one more mark per chunk of 16 bindings and jumps over a chunk with nothing pending
(`Compiler/ResidualWork`, `evEnvFW_eq`: equal to the pass for every input; `@[csimp]`).
`mem:fun` runs the event-driven step with memories as functions (the closure chains).

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
import HDLProof.Run.Trace
import LeanSemanticPrimitives.Projection.CertLoadAny

open Compiler HDLProof.Run

def usage : String :=
  "usage: cert_trace <cert> <cycles> <stim> [edges:all|edges:third|edges:file:<path>] " ++
  "[init:reset|init:zero] [route:ir|vc|ref|fp] [opt:off] [opt:stats] [inc:off|inc:scan] [mem:fun] " ++
  "[share:off] [loop:full] [ir:raw] [ckpt-in:<file>] [ckpt-at:<cycle>:<file>] [ckpt-out:<file>] " ++
  "[compact:<cycles>]"

def main (args : List String) : IO UInt32 := do
  match args with
  | cert :: cyc :: stimPath :: rest =>
    let some _ := cyc.toNat? | IO.eprintln s!"cycles {repr cyc} is not a number"; return 2
    -- the route options set the knobs (route:ir by default); `opt:stats` is this driver's; the
    -- rest (edges, start, checkpoints, compaction) are the trace loop's
    let mut knobs : Knobs := { route := .ir }
    let mut optStats := false
    let mut traceOpts : Array String := #[]
    for opt in rest do
      if opt == "opt:stats" then optStats := true
      else if opt.startsWith "compact:" then traceOpts := traceOpts.push opt
      else
        match knobs.apply? opt with
        | some (.ok k) => knobs := k
        | some (.error _) => do IO.eprintln s!"unknown option {opt}"; return 2
        | none => traceOpts := traceOpts.push opt
    let (_, D) ← Projection.CertLoadAny.loadAny cert
    let targs := cyc :: stimPath :: traceOpts.toList
    match EngineSelector D knobs with
    | .ok sel =>
      -- `opt:stats`: what was prepared (vc's optimizer and incremental step), on stderr
      let atStart (_ _ : Nat) (_ : IO Unit) : IO (Option UInt32) := do
        if optStats then
          for n in sel.notes do IO.eprintln n
        return none
      traceRun knobs.route.name D (TraceSim.ofEngine D sel.engine sel.autoCompact atStart) targs
    | .error e =>
      -- the route refuses the design: ir reports it at the first cycle (a run of 0 cycles still
      -- writes the checkpoints due at its start), vc and the others at once
      let atStart (cycles start : Nat) (saveStart : IO Unit) : IO (Option UInt32) := do
        match e.route with
        | .ir =>
          if cycles > 0 then
            IO.eprintln s!"step {start}: refused: {e.detail}"
            return some 1
          saveStart
          return some 0
        | .vc =>
          IO.eprintln s!"compileDesign refused: {e.detail}"
          return some 1
        | _ =>
          IO.eprintln e.message
          return some 1
      traceRun knobs.route.name D (TraceSim.ofEngine D (refEngine D) true atStart) targs
  | _ =>
    IO.eprintln usage
    return 2
