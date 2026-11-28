/-
Conformance of the engines `EngineSelector` builds (`HDLProof/Run/Engine`).

  (cd formal/lean && LEAN_PATH=$PWD/.olean-dev lean --run ../../pass/lean/tools/tests/engine_check.lean \
     <cycles> <cert.dcert> [<cert.dcert> ...])

`EngineSelector_*_sound` and `EngineSelector_fp_agree` PROVE what these runs check. For each certificate,
every route and knob combination (ir staged and raw; ref; vc optimized or not, each with the
event-driven step on tables and on functions, the scan step and the full step; fp's encoded
engine with and without shared tails, and its earlier step `loop:full`) runs `<cycles>` cycles of pseudo-random inputs and clock edges (a fixed
generator, so every engine sees the same ticks) three ways:
- plain: one step per cycle;
- twice: each cycle first steps from the state with other inputs, drops that result, then steps
  from the SAME state with the real inputs (a closed loop's guess and final step without a peek);
- peeked: each cycle peeks with other inputs, peeks with the real inputs (both outputs checked
  against ref's), then steps (a closed loop with a peek; plain for an engine without one);
- reloaded: halfway, the state is replaced by the engine's reload of the restored checkpoint of
  what it stands for (compaction; vc keeps its environment).
Each run's outputs must be `ref`'s, cycle by cycle, and its final checkpoint `ref`'s, byte for
byte (fp's state agrees with `ref`'s below every memory's depth, which is all a checkpoint
reads). Every combination must build on every certificate given (a route that is not built is a
failure, so none drops out silently), and the run count must be combinations x 4 modes x
certificates. `Knobs.parse` must map
the drivers' options as the module header's table says. A control: `ref` with every clock
firing on every cycle must differ from `ref` on at least one certificate, or the ticks do not
exercise the edges and the comparison proves little.

With `--examples` in place of the certificates, the certificates are the in-Lean examples
lgraph-sim carries (`Compiler.Direct.Examples.registry`, all but the 200,000-node chain), so the
check needs no lhd. There a route may refuse an example only where `exampleRefusals` lists it,
with the start of the refusal's message (none today: every route, fp included, builds all 9);
every other combination must build, and the run count is the combinations built x 4 modes
(today 13 x 4 x 9 = 468). And each of the 14 malformed examples (`Examples.rejectRegistry`)
must be refused by `EngineSelector` on the ir route, with the direct simulator's message: one
that builds is a failure.

Prints `PASS engine: <n> runs` or one `FAIL` line per run or option list that went wrong.
-/
import HDLProof.Run.Engine
import HDLProof.Run.Stim
import LeanSemanticPrimitives.Compiler.CertIORoute
import LeanSemanticPrimitives.Compiler.Ckpt
import LeanSemanticPrimitives.Compiler.DirectExamples

open Compiler HDLProof HDLProof.Run

/-- A fixed 64-bit linear congruential generator. -/
def lcg (x : UInt64) : UInt64 := x * 6364136223846793005 + 1442695040888963407

/-- Cycle `k`'s inputs and edges, from seed `seed`: each input a random value of its width; each
clock fires on three cycles of four. -/
def tick (D : DesignCert) (seed : UInt64) (k : Nat) : ClockEdges × RuntimeInput := Id.run do
  let mut x := lcg (seed + UInt64.ofNat k * 0x9E3779B97F4A7C15)
  let mut ins : RuntimeInput := #[]
  for j in [0:maxInput D] do
    x := lcg x
    let w := inputWidth D j
    let v := (x >>> 11).toNat * 2 ^ 53 + (lcg x >>> 11).toNat
    ins := ins.push (mk_bv w (Int.ofNat (v % 2 ^ w)))
  let mut es : ClockEdges := #[]
  for _ in [0:D.clocks.size] do
    x := lcg x
    es := es.push ((x >>> 33) % 4 != 0)
  return (es, ins)

/-- A run's outputs (one line per cycle) and its final checkpoint. -/
structure Res where
  outs : Array String
  ckpt : ByteArray

inductive Mode where
  | plain | twice | peeked | reloaded
deriving BEq, Repr

def Mode.name : Mode → String
  | .plain => "plain" | .twice => "twice" | .peeked => "peeked" | .reloaded => "reloaded"

def outLine (o : Array BV) : String := " ".intercalate (o.toList.map fun b => toString (bv_uint b))

/-- Run engine `E` from `s0` for `n` cycles of `ticks` in mode `m`; `refOuts` (ref's lines, when
given) checks the peeks. -/
def runE {σ : Type} (D : DesignCert) (E : Engine σ) (s0 : RuntimeState) (n : Nat)
    (ticks : Array (ClockEdges × RuntimeInput)) (m : Mode) (refOuts : Array String := #[]) :
    Except String Res := do
  let mut q := E.load s0
  let mut outs : Array String := #[]
  for k in [0:n] do
    if m == .reloaded && k == n / 2 then
      q := E.reload q (Ckpt.restore (Ckpt.save D 0 k #[] (E.view q)))
    let (e, i) := ticks[k]!
    -- the guess: every input inverted
    let g := i.map fun b => mk_bv b.width (Int.ofNat (2 ^ b.width - 1) - bv_uint b)
    if m == .twice then
      -- its result is dropped
      match E.step e g q with
      | .error (err, _) => throw s!"cycle {k} (guess): {err}"
      | .ok _ => pure ()
    if m == .peeked then
      if let some pk := E.peek? then
        match pk e g q with
        | .error (err, _) => throw s!"cycle {k} (peek): {err}"
        | .ok (_, q1) =>
          match pk e i q1 with
          | .error (err, _) => throw s!"cycle {k} (peek): {err}"
          | .ok (o, q2) =>
            if let some r := refOuts[k]? then
              if outLine o != r then throw s!"cycle {k}: peek {outLine o} vs ref {r}"
            q := q2
    match E.step e i q with
    | .error (err, _) => throw s!"cycle {k}: {err}"
    | .ok (o, q') =>
      outs := outs.push (outLine o)
      q := q'
  return { outs, ckpt := Ckpt.render (Ckpt.save D 0 n #[] (E.view q)) }

/-- The combinations every certificate runs. -/
def combos : List (String × Knobs) :=
  [("ir", { route := .ir }), ("ir raw", { route := .ir, irStaged := false })] ++
  ([true, false].flatMap fun opt =>
    let o := if opt then "" else " opt:off"
    [(s!"vc{o}", { route := .vc, vcOpt := opt }),
     (s!"vc{o} mem:fun", { route := .vc, vcOpt := opt, vcMem := .func }),
     (s!"vc{o} inc:scan", { route := .vc, vcOpt := opt, vcInc := .scan }),
     (s!"vc{o} inc:off", { route := .vc, vcOpt := opt, vcInc := .off })]) ++
  [("fp", { route := .fp }), ("fp share:off", { route := .fp, fpShare := false }),
   ("fp loop:full", { route := .fp, fpLoopFull := true })]

def knobKey (k : Knobs) : String :=
  s!"{k.route.name} opt={k.vcOpt} inc={repr k.vcInc} mem={repr k.vcMem} staged={k.irStaged} \
    share={k.fpShare} compact={k.compact}"

/-- `Knobs.parse` on the drivers' options: the expected knobs (from the defaults) and leftovers,
or an error. -/
def parseCases : List (List String × Option (Knobs × List String)) :=
  [([], some ({}, [])),
   (["route:ir", "opt:off"], some ({ route := .ir, vcOpt := false, fpShare := false }, [])),
   (["route:tv"], some ({ route := .vc }, [])),
   (["route:fp", "share:off"], some ({ route := .fp, fpShare := false }, [])),
   (["inc:scan"], some ({ vcInc := .scan }, [])),
   (["inc:off", "inc:scan"], some ({ vcInc := .off }, [])),
   (["inc:scan", "inc:off"], some ({ vcInc := .off }, [])),
   (["max:5", "mem:fun", "trace:d", "ir:raw"], some ({ vcMem := .func, irStaged := false }, ["max:5", "trace:d"])),
   (["compact:0"], some ({ compact := some 0 }, [])),
   (["compact:7", "compact:256"], some ({ compact := some 256 }, [])),
   (["compact:x"], none),
   (["route:zz"], none)]

/-- `--examples`: the routes that may refuse an example, as (example, combination, the start of
the message `EngineSelector` gives); every other combination must build. None today. -/
def exampleRefusals : List (String × String × String) := []

/-- The certificates of a run: the files given, or the in-Lean examples (`--examples`). -/
def certsOf (args : List String) : IO (Except String (List (String × DesignCert))) := do
  if args == ["--examples"] then
    return .ok (Compiler.Direct.Examples.registry.filter (·.1 != "chain-200k"))
  let mut out := []
  for cert in args do
    match CertIO.admitAny (← IO.FS.readBinFile cert) with
    | .ok (_, D) => out := out ++ [(cert, D)]
    | .error e => return .error s!"{cert}: {e}"
  return .ok out

def main (args : List String) : IO UInt32 := do
  let mut fails := 0
  let mut runs := 0
  let mut wantRuns := 0
  let mut controlHit := false
  for (opts, want) in parseCases do
    let got := (Knobs.parse {} opts).toOption.map fun (k, r) => (knobKey k, r)
    let want' := want.map fun (k, r) => (knobKey k, r)
    unless got == want' do
      IO.println s!"FAIL parse {opts}: got {got}, want {want'}"
      fails := fails + 1
  match args with
  | cyc :: certArgs@(_ :: _) =>
    let some n := cyc.toNat? | IO.eprintln s!"cycles {repr cyc} is not a number"; return 2
    let examples := certArgs == ["--examples"]
    let certs ← match ← certsOf certArgs with
      | .ok cs => pure cs
      | .error e => do IO.println s!"FAIL {e}"; return 1
    for (cert, D) in certs do
      let ticks := (Array.range n).map (tick D 12345)
      let s0 := Direct.resetState D
      let ref ← match EngineSelector D { route := .ref } with
        | .error e => do IO.println s!"FAIL {cert}: ref: {e.message}"; return 1
        | .ok b => match runE D b.engine s0 n ticks .plain with
          | .ok r => pure r
          | .error e => do IO.println s!"FAIL {cert}: ref: {e}"; return 1
      for (name, k) in combos do
        let allowed := if examples then
            exampleRefusals.find? fun (c, r, _) => c == cert && r == name
          else none
        match EngineSelector D k with
        | .error e =>
          -- every route and knob combination must build on these certificates: a route that is
          -- not built would otherwise drop out of the run count and the run still pass
          match allowed with
          | some (_, _, msg) =>
            unless e.message.startsWith msg do
              IO.println s!"FAIL {cert}: {name}: refused for another reason: {e.message}"; fails := fails + 1
          | none => IO.println s!"FAIL {cert}: {name}: not built: {e.message}"; fails := fails + 1
        | .ok b =>
          if allowed.isSome then
            IO.println s!"FAIL {cert}: {name}: built, but the list says the route refuses it"; fails := fails + 1
          wantRuns := wantRuns + 4
          for m in [Mode.plain, .twice, .peeked, .reloaded] do
            runs := runs + 1
            match runE D b.engine s0 n ticks m ref.outs with
            | .error e =>
              IO.println s!"FAIL {cert}: {name} {m.name}: {e}"; fails := fails + 1
            | .ok r =>
              match (List.range n).find? fun c => r.outs[c]! != ref.outs[c]! with
              | some c =>
                IO.println s!"FAIL {cert}: {name} {m.name}: cycle {c}: {r.outs[c]!} vs ref {ref.outs[c]!}"
                fails := fails + 1
              | none =>
                unless r.ckpt.data == ref.ckpt.data do
                  IO.println s!"FAIL {cert}: {name} {m.name}: final checkpoint differs from ref's"
                  fails := fails + 1
      -- the control: every clock fires
      let allE : Engine RuntimeState :=
        Engine.ofStep "all-clocks" fun e i s => .ok (interpretDesign D (e.map fun _ => true) i s)
      match runE D allE s0 n ticks .plain with
      | .ok r => if r.outs != ref.outs || r.ckpt.data != ref.ckpt.data then controlHit := true
      | .error _ => controlHit := true
      let distinct := (ref.outs.foldl (fun (acc : Std.HashSet String) o => acc.insert o) {}).size
      IO.println s!"ok {cert}: {n} cycles, {distinct} distinct output vectors"
    -- the malformed examples: the ir route refuses each, with the direct simulator's message
    if examples then
      for (name, D) in Compiler.Direct.Examples.rejectRegistry do
        match EngineSelector D { route := .ir } with
        | .ok _ => IO.println s!"FAIL {name}: the ir route built a malformed certificate"; fails := fails + 1
        | .error e =>
          unless e.message.startsWith "ir: the direct simulator refuses the certificate:" do
            IO.println s!"FAIL {name}: refused with another message: {e.message}"; fails := fails + 1
      IO.println s!"ok: the ir route refuses all {Compiler.Direct.Examples.rejectRegistry.length} malformed examples"
    -- every combination, in every mode, on every certificate (less the listed refusals)
    if !examples then wantRuns := combos.length * 4 * certs.length
  | _ =>
    IO.eprintln "usage: engine_check <cycles> (<cert.dcert> [<cert.dcert> ...] | --examples)"
    return 2
  unless controlHit do
    IO.println "FAIL control: firing every clock changed nothing on any certificate"
    fails := fails + 1
  unless runs == wantRuns do
    IO.println s!"FAIL engine: {runs} runs, expected {wantRuns} ({combos.length} combinations x 4 modes per certificate, less the listed refusals)"
    fails := fails + 1
  if fails == 0 then
    IO.println s!"PASS engine: {runs} runs"
    return 0
  return 1
