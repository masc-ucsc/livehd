/-
# `DirectSim` — the `lgraph-sim` library

Direction 2, phase 5.

A GENERIC simulator over `DesignCert`, not generated semantic code.  `simMain`
takes the certificate as a value, so a per-design launcher is a data import and
nothing more; if a runtime certificate loader lands later, the same `simMain`
consumes its output and neither `directStep` nor its theorem changes.

## The memory transport problem

`RuntimeState.mems : Array (Int → BV)` holds FUNCTIONS.  A CLI cannot serialise
a function, so the external form of a memory is a DEFAULT plus finitely many
address/value overrides (`MemImageExt`), decoded to a function on input and
sampled at requested addresses on output.  That asymmetry is deliberate: the
theorems compare memories extensionally, and the trace format never claims to
print one.

## Trace format

Deterministic and line-oriented, so two implementations' traces diff cleanly:

    # lgraph-sim <design> sources=.. nodes=.. outputs=.. flops=.. mems=..
    cycle <t> out <v> <v> ...
    cycle <t> flop <v> <v> ...        (with --show-state)
    cycle <t> mem <i>[<addr>] <v>     (with --watch-mem i:addr)
    final flop <v> <v> ...

Signal NAMES are metadata the certificate does not carry; they are a usability
feature, not a prerequisite for the evaluator, and are deliberately absent.
-/
import LeanSemanticPrimitives.Compiler.DirectTrace

namespace Compiler
namespace Direct

--------------------------------------------------------------------------------
-- External form of a memory image
--------------------------------------------------------------------------------

/-- A memory image as data: one default value plus finitely many overrides. -/
structure MemImageExt where
  dw        : Nat
  dfault    : Int := 0
  overrides : Array (Int × Int) := #[]
deriving Repr, Inhabited

/-- Decode to the function `RuntimeState` actually holds.  A linear scan of the
overrides per read — fine for the small initial images a CLI supplies, and the
only cost paid by a design whose memories start uniform. -/
def MemImageExt.decode (m : MemImageExt) : Int → BV :=
  fun x =>
    match m.overrides.find? (fun p => p.1 == x) with
    | some p => mk_bv m.dw p.2
    | none   => mk_bv m.dw m.dfault

--------------------------------------------------------------------------------
-- Number formatting and parsing
--------------------------------------------------------------------------------

def hexOfNat (n : Nat) : String :=
  if n == 0 then "0" else String.ofList (Nat.toDigits 16 n)

def fmtBV (hex : Bool) (b : BV) : String :=
  let u := bv_uint b
  if hex then "0x" ++ hexOfNat u.toNat else toString u

private def hexDigit? (c : Char) : Option Nat :=
  if c.isDigit then some (c.toNat - '0'.toNat)
  else
    let c := c.toLower
    if 'a' ≤ c && c ≤ 'f' then some (c.toNat - 'a'.toNat + 10) else none

private def decNat? (cs : List Char) : Option Nat :=
  if cs.isEmpty then none
  else cs.foldl (fun acc c =>
    match acc with
    | some n => if c.isDigit then some (n * 10 + (c.toNat - '0'.toNat)) else none
    | none   => none) (some 0)

private def hexNat? (cs : List Char) : Option Nat :=
  if cs.isEmpty then none
  else cs.foldl (fun acc c =>
    match acc, hexDigit? c with
    | some n, some d => some (n * 16 + d)
    | _, _           => none) (some 0)

/-- Decimal, or `0x`-prefixed hexadecimal, with an optional leading `-`.
Whitespace anywhere is ignored, so a CRLF trace parses like an LF one. -/
def parseInt? (s : String) : Option Int :=
  let cs := s.toList.filter (fun c => !c.isWhitespace)
  let neg := cs.head? == some '-'
  let body := if neg then cs.drop 1 else cs
  let mag : Option Nat :=
    match body with
    | '0' :: x :: hs => if x = 'x' || x = 'X' then hexNat? hs else decNat? body
    | _              => decNat? body
  mag.map fun n => if neg then -(Int.ofNat n) else Int.ofNat n

def parseNat? (s : String) : Option Nat :=
  match parseInt? s with
  | some v => if 0 ≤ v then some v.toNat else none
  | none   => none

private def fields (line : String) : List String :=
  (line.splitOn " ").flatMap (fun s => s.splitOn "\t") |>.filter (· ≠ "")

private def stripComment (line : String) : String :=
  match (line.splitOn "#").head? with
  | some s => s
  | none   => line

--------------------------------------------------------------------------------
-- Building runtime values from external data
--------------------------------------------------------------------------------

/-- Build the primary-input vector for one cycle.

Each ordinal gets the width the CERTIFICATE declares for it.  An ordinal read
only as an asynchronous reset has no `input` source and therefore no declared
width; it is built at 64 bits so a supplied value is not truncated away — the
semantics only tests such a value for nonzero-ness.  This is stimulus
construction, outside the cycle semantics. -/
def buildInput (D : DesignCert) (vals : Array Int) : RuntimeInput :=
  (Array.range (inputArity D)).map fun idx =>
    mk_bv ((inputWidthOf D idx).getD 64) (vals[idx]?.getD 0)

/-- External initial state: flop values by ordinal, memory images by ordinal. -/
structure StateExt where
  flops : Array (Nat × Int)         := #[]
  mems  : Array (Nat × MemImageExt) := #[]
deriving Inhabited

def StateExt.build (D : DesignCert) (st : StateExt) : RuntimeState :=
  { flops := (Array.range D.flops.size).map fun k =>
      let w := (D.flops[k]?).elim 1 FlopDesc.width
      match st.flops.find? (fun p => p.1 == k) with
      | some p => mk_bv w p.2
      | none   => mk_bv w 0
    mems := (Array.range D.memories.size).map fun k =>
      let dw := (D.memories[k]?).elim 1 MemoryDesc.dw
      match st.mems.find? (fun p => p.1 == k) with
      | some p => ({ p.2 with dw := dw } : MemImageExt).decode
      | none   => fun _ => mk_bv dw 0 }

theorem StateExt.build_flops (D : DesignCert) (st : StateExt) :
    (st.build D).flops.size = D.flops.size := by simp [StateExt.build]

theorem StateExt.build_mems (D : DesignCert) (st : StateExt) :
    (st.build D).mems.size = D.memories.size := by simp [StateExt.build]

--------------------------------------------------------------------------------
-- File formats
--------------------------------------------------------------------------------

/-- Input trace: one cycle per non-empty, non-comment line; whitespace-separated
values, ordinal 0 first.  Missing trailing values are zero. -/
def parseInputTrace (text : String) : Except String (Array (Array Int)) := do
  let mut out : Array (Array Int) := #[]
  for line in text.splitOn "\n" do
    let fs := fields (stripComment line)
    if fs.isEmpty then continue
    let mut row : Array Int := #[]
    for f in fs do
      match parseInt? f with
      | some v => row := row.push v
      | none   => throw s!"not a number: '{f}'"
    out := out.push row
  return out

/-- Initial state file:

    flop <ordinal> <value>
    mem  <ordinal> default <value>
    mem  <ordinal> <address> <value>
-/
def parseStateFile (text : String) : Except String StateExt := do
  let mut flops : Array (Nat × Int) := #[]
  let mut mems  : Array (Nat × MemImageExt) := #[]
  for line in text.splitOn "\n" do
    match fields (stripComment line) with
    | [] => pure ()
    | ["flop", o, v] =>
      match parseNat? o, parseInt? v with
      | some o, some v => flops := flops.push (o, v)
      | _, _ => throw s!"bad flop line: '{line}'"
    | ["mem", o, a, v] =>
      match parseNat? o, parseInt? v with
      | some o, some v =>
        let cur : MemImageExt :=
          match mems.find? (fun p => p.1 == o) with
          | some p => p.2
          | none   => { dw := 1 }
        let cur :=
          if a == "default" then { cur with dfault := v }
          else match parseInt? a with
            | some addr => { cur with overrides := cur.overrides.push (addr, v) }
            | none      => cur
        mems := (mems.filter (fun p => p.1 != o)).push (o, cur)
      | _, _ => throw s!"bad mem line: '{line}'"
    | fs => throw s!"unrecognised line: '{String.intercalate " " fs}'"
  return { flops := flops, mems := mems }

--------------------------------------------------------------------------------
-- Options
--------------------------------------------------------------------------------

structure SimOptions where
  design    : String                := ""
  inputs    : Option String         := none
  state     : Option String         := none
  cycles    : Option Nat            := none
  showState : Bool                  := false
  watchMem  : Array (Nat × Int)     := #[]
  checkOnly : Bool                  := false
  hex       : Bool                  := false
  list      : Bool                  := false
deriving Inhabited

def usage : String :=
  "usage: lgraph-sim DESIGN [options]\n" ++
  "  --list                 list the certificates this binary carries\n" ++
  "  --inputs FILE          input trace, one cycle per line (default: all zeros)\n" ++
  "  --state FILE           initial state (default: all zeros)\n" ++
  "  --cycles N             cycles to run (default: the input trace's length, else 1)\n" ++
  "  --show-state           also print the flop state each cycle\n" ++
  "  --watch-mem IDX:ADDR   print memory IDX at ADDR each cycle (repeatable)\n" ++
  "  --check-only           run the certificate and runtime checks, then exit\n" ++
  "  --hex                  print values in hexadecimal\n" ++
  "exit status: 0 ok, 1 refused certificate or runtime shape, 2 usage, 3 I/O"

def parseArgs : List String → SimOptions → Except String SimOptions
  | [], o => .ok o
  | "--list" :: rest, o => parseArgs rest { o with list := true }
  | "--show-state" :: rest, o => parseArgs rest { o with showState := true }
  | "--check-only" :: rest, o => parseArgs rest { o with checkOnly := true }
  | "--hex" :: rest, o => parseArgs rest { o with hex := true }
  | "--inputs" :: f :: rest, o => parseArgs rest { o with inputs := some f }
  | "--state" :: f :: rest, o => parseArgs rest { o with state := some f }
  | "--cycles" :: n :: rest, o =>
      match parseNat? n with
      | some n => parseArgs rest { o with cycles := some n }
      | none   => .error s!"--cycles expects a number, got '{n}'"
  | "--watch-mem" :: spec :: rest, o =>
      match spec.splitOn ":" with
      | [i, a] =>
          match parseNat? i, parseInt? a with
          | some i, some a => parseArgs rest { o with watchMem := o.watchMem.push (i, a) }
          | _, _ => .error s!"--watch-mem expects IDX:ADDR, got '{spec}'"
      | _ => .error s!"--watch-mem expects IDX:ADDR, got '{spec}'"
  | a :: rest, o =>
      if a.startsWith "--" then .error s!"unknown option '{a}'"
      else if o.design.isEmpty then parseArgs rest { o with design := a }
      else .error s!"more than one design named ('{o.design}' and '{a}')"

--------------------------------------------------------------------------------
-- The driver
--------------------------------------------------------------------------------

def header (name : String) (D : DesignCert) : String :=
  s!"# lgraph-sim {name} sources={D.sources.size} nodes={D.nodes.size} " ++
  s!"outputs={D.outputs.size} flops={D.flops.size} mems={D.memories.size} " ++
  s!"inputs={inputArity D}"

def renderCycle (o : SimOptions) (t : Nat) (r : RuntimeResult) : List String :=
  let outs := s!"cycle {t} out " ++ String.intercalate " " (r.outputs.toList.map (fmtBV o.hex))
  let st := if o.showState then
      [s!"cycle {t} flop " ++
        String.intercalate " " (r.nextState.flops.toList.map (fmtBV o.hex))]
    else []
  let mw := o.watchMem.toList.map fun (i, a) =>
    match r.nextState.mems[i]? with
    | some f => s!"cycle {t} mem {i}[{a}] " ++ fmtBV o.hex (f a)
    | none   => s!"cycle {t} mem {i}[{a}] -"
  outs :: (st ++ mw)

/-- Run a whole trace and render it.  Uses `runDirect`, so a refusal at ANY
cycle aborts with a diagnostic rather than producing a partial trace that looks
like a result. -/
def runAndRender (o : SimOptions) (name : String) (D : DesignCert)
    (s0 : RuntimeState) (ins : List RuntimeInput) : Except SimError (List String) :=
  match runDirect D s0 ins with
  | .error e => .error e
  | .ok t =>
      let body := (t.steps.zipIdx.map fun (r, k) => renderCycle o k r).flatten
      let fin := s!"final flop " ++
        String.intercalate " " (t.finalState.flops.toList.map (fmtBV o.hex))
      .ok (header name D :: (body ++ [fin]))

/-- What one invocation produced. -/
inductive SimOutcome where
  | ok      (lines : List String)
  | refused (e : SimError)

/-- Everything after the files are read is PURE, so the simulator's behaviour is
a function of the certificate and the trace, not of the process. -/
def simRun (o : SimOptions) (name : String) (D : DesignCert)
    (stExt : StateExt) (rows : Array (Array Int)) : SimOutcome :=
  let s0 := stExt.build D
  let n := match o.cycles with
    | some n => n
    | none   => if rows.isEmpty then 1 else rows.size
  let ins := (List.range n).map fun k => buildInput D (rows[k]?.getD #[])
  if o.checkOnly then
    match checkDesign D with
    | .error e => .refused e
    | .ok _ =>
      match ins.head? with
      | none    => .ok [header name D, "ok"]
      | some i0 =>
        match checkRuntime D i0 s0 with
        | .error e => .refused e
        | .ok _    => .ok [header name D, "ok"]
  else
    match runAndRender o name D s0 ins with
    | .error e     => .refused e
    | .ok lines    => .ok lines

def loadState : Option String → IO (Except String StateExt)
  | none   => pure (.ok {})
  | some f => do
      try
        let text ← IO.FS.readFile f
        match parseStateFile text with
        | .error m => pure (.error s!"{f}: {m}")
        | .ok st   => pure (.ok st)
      catch e => pure (.error s!"cannot read {f}: {e}")

def loadInputs : Option String → IO (Except String (Array (Array Int)))
  | none   => pure (.ok #[])
  | some f => do
      try
        let text ← IO.FS.readFile f
        match parseInputTrace text with
        | .error m => pure (.error s!"{f}: {m}")
        | .ok r    => pure (.ok r)
      catch e => pure (.error s!"cannot read {f}: {e}")

/-- The generic entry point.  `registry` maps a name to an elaborated
certificate; a per-design launcher supplies a one-element list.

Exit status: 0 success, 1 the certificate or the runtime shape was REFUSED,
2 a usage error, 3 an I/O or parse error. -/
def simMain (registry : List (String × DesignCert)) (args : List String) : IO UInt32 := do
  match parseArgs args {} with
  | .error msg => do
      IO.eprintln s!"lgraph-sim: {msg}"
      IO.eprintln usage
      return 2
  | .ok o =>
    if o.list then do
      for (n, D) in registry do
        IO.println s!"{n} sources={D.sources.size} nodes={D.nodes.size} outputs={D.outputs.size} flops={D.flops.size} mems={D.memories.size}"
      return 0
    else if o.design.isEmpty then do
      IO.eprintln usage
      return 2
    else
      match registry.find? (fun p => p.1 == o.design) with
      | none => do
          IO.eprintln s!"lgraph-sim: no certificate named '{o.design}' in this binary"
          IO.eprintln s!"  available: {String.intercalate ", " (registry.map Prod.fst)}"
          return 2
      | some (name, D) => do
          match ← loadState o.state with
          | .error m => do IO.eprintln s!"lgraph-sim: {m}"; return 3
          | .ok stExt =>
            match ← loadInputs o.inputs with
            | .error m => do IO.eprintln s!"lgraph-sim: {m}"; return 3
            | .ok rows =>
              match simRun o name D stExt rows with
              | .refused e => do
                  IO.eprintln s!"lgraph-sim: refused: {e.render}"
                  return 1
              | .ok lines => do
                  for l in lines do IO.println l
                  return 0

end Direct
end Compiler
