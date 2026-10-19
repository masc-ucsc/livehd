/-
`cert_trace` -- the per-step OUTPUTS of `interpretDesign` on a certificate,
under a supplied stimulus. The oracle side of an RTL differential: the same
`stim v1` file drives an event simulator on the original RTL, and the two
traces are compared line by line.

    lean --run scripts/cert_trace.lean <cert> <cycles> <stim> [edges:all|edges:third]

`stim v1` is the dcert2_memroute format, restricted to what an RTL testbench
can replay: `in <ordinal> <width> <value>` (every cycle) and
`at <cycle> in <ordinal> <width> <value>` (that cycle wins). Unlisted inputs
are 0 at their declared width. The initial state is all-zero (flops and
memories). Edges: `all` fires every declared clock on every step; `third` is
`edgeSchedule`'s one-clock pattern, every clock quiet on steps k % 3 = 2 --
the quiet steps are where an ASYNCHRONOUS reset and a synchronous one differ.

Output: one line per step, `STEP <k> <out0> <out1> ...`, each output as its
unsigned value (`bv_uint`). Runs the SPEC, not a residual: the FP drivers
separately check their residual against this same `interpretDesign`.
-/
import LeanSemanticPrimitives.Projection.CertLoadAny
import LeanSemanticPrimitives.Compiler.DesignSemantics

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

def edgesAt (D : DesignCert) (third : Bool) (k : Nat) : ClockEdges :=
  if third && k % 3 == 2 then Array.replicate D.clocks.size false
  else Array.replicate D.clocks.size true

def main (args : List String) : IO UInt32 := do
  match args with
  | cert :: cyc :: stimPath :: rest =>
    let some cycles := cyc.toNat? | IO.eprintln s!"cycles {repr cyc} is not a number"; return 2
    let third ← match rest with
      | [] | ["edges:all"] => pure false
      | ["edges:third"] => pure true
      | _ => do IO.eprintln s!"unknown option {rest}"; return 2
    let (_, D) ← Projection.CertLoadAny.loadAny cert
    let st ← match parseStim (← IO.FS.readFile stimPath) with
      | .ok s => pure s
      | .error e => do IO.eprintln s!"stimulus: {e}"; return 2
    let mut s := zeroState D
    for k in List.range cycles do
      let r := interpretDesign D (edgesAt D third k) (inputAt D st k) s
      IO.println s!"STEP {k} {" ".intercalate (r.outputs.toList.map (fun o => toString (bv_uint o)))}"
      s := r.nextState
    return 0
  | _ =>
    IO.eprintln "usage: cert_trace <cert> <cycles> <stim> [edges:all|edges:third]"
    return 2
