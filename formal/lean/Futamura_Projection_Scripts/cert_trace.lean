/-
`cert_trace` -- the per-step OUTPUTS of `interpretDesign` on a certificate
(computed by `directStep`, proved equal to it),
under a supplied stimulus. The oracle side of an RTL differential: the same
`stim v1` file drives an event simulator on the original RTL, and the two
traces are compared line by line.

    lean --run Futamura_Projection_Scripts/cert_trace.lean <cert> <cycles> <stim>
         [edges:all|edges:third|edges:file:<path>] [init:reset|init:zero] [route:ir|route:tv]

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

`route:tv` runs the translation-validation compiler instead: `compileDesign`
once, then its straight-line program (`denoteResidual`) every step -- the
program `compileDesign_correct` proves equal to `interpretDesign`. Same stimulus,
same output format, so the two routes' traces compare line for line. A
certificate `compileDesign` refuses is reported and exits 1.
-/
import LeanSemanticPrimitives.Projection.CertLoadAny
import LeanSemanticPrimitives.Compiler.DesignSemantics
import LeanSemanticPrimitives.Compiler.DirectSemantics
import LeanSemanticPrimitives.Compiler.CompileDesignDefs

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
    let mut st : Stim := {}
    for l in rest do
      match (l.splitOn " ").filter (· != "") with
      | ["at", c, "in", i, w, v] =>
          match c.toNat?, i.toNat?, w.toNat?, v.toInt? with
          | some c, some i, some w, some v => st := { st with at_ := st.at_ ++ [(c, i, w, v)] }
          | _, _, _, _ => throw s!"unparseable {repr l}"
      | ["in", i, w, v] =>
          match i.toNat?, w.toNat?, v.toInt? with
          | some i, some w, some v => st := { st with every := st.every ++ [(i, w, v)] }
          | _, _, _ => throw s!"unparseable {repr l}"
      | _ => throw s!"unrecognised {repr l}"
    pure st

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

def zeroState (D : DesignCert) : RuntimeState :=
  { flops := D.flops.map (fun f => mk_bv f.width 0)
  , mems  := D.memories.map (fun m => fun _ => mk_bv m.dw 0) }

/-- Every flop at its reset value, memories zero: the default start. A flop with
a Verilog power-on value and no reset carries that value as `resetValue`, so this
is the start that matches the RTL; it is also the Futamura runner's
(`initStateOf`). `init:zero` keeps the old all-zero start. -/
def resetState (D : DesignCert) : RuntimeState :=
  { flops := D.flops.map (fun f => mk_bv f.width f.resetValue)
  , mems  := D.memories.map (fun m => fun _ => mk_bv m.dw 0) }

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
    let mut tv := false
    let mut schedPath : Option String := none
    for opt in rest do
      if opt.startsWith "edges:file:" then
        schedPath := some (opt.replace "edges:file:" "")
      else
        match opt with
        | "edges:all" => third := false
        | "edges:third" => third := true
        | "init:reset" => zeroInit := false
        | "init:zero" => zeroInit := true
        | "route:ir" => tv := false
        | "route:tv" => tv := true
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
    let mut s := if zeroInit then zeroState D else resetState D
    if tv then
      match compileDesign D with
      | .error e => do IO.eprintln s!"compileDesign refused: {repr e}"; return 1
      | .ok R =>
        for k in List.range cycles do
          let r := denoteResidual R (edgesAt D third sched k) (inputAt D st k) s
          IO.println s!"STEP {k} {" ".intercalate (r.outputs.toList.map (fun o => toString (bv_uint o)))}"
          s := r.nextState
        return 0
    for k in List.range cycles do
      -- `directStep` is proved equal to `interpretDesign` on every input it
      -- accepts (`directStep_correct`; it refuses a certificate or runtime shape
      -- outside the checked fragment instead of stepping it). Executing
      -- `interpretDesign` itself is exponential in the number of steps on a
      -- design with a memory: measured on a 16x4 register file, 7 s for 10
      -- steps and about 2.3x per extra step, against 0.6 s for 200 steps here.
      let r ← match Compiler.Direct.directStep D (edgesAt D third sched k) (inputAt D st k) s with
        | .ok r => pure r
        | .error e => do IO.eprintln s!"step {k}: refused: {e.render}"; return 1
      IO.println s!"STEP {k} {" ".intercalate (r.outputs.toList.map (fun o => toString (bv_uint o)))}"
      s := r.nextState
    return 0
  | _ =>
    IO.eprintln "usage: cert_trace <cert> <cycles> <stim> [edges:all|edges:third] [init:reset|init:zero] [route:ir|route:tv]"
    return 2
