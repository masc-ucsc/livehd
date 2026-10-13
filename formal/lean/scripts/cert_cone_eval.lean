/-
CONTROL: evaluate a memory write's ENABLE cone on an ACTUALLY EMITTED
certificate, driving the write-gate mux by its REAL SELECTOR.

This is not a hand-built term comparison.  It parses a real `.dcert`, finds the
declared memory's next-image write node, takes its ENABLE operand, locates the
`Op_MuxBool` the enable cone passes through, and forces THAT MUX'S SELECTOR
DEPENDENCY -- `deps[0]`, the slot the certificate itself names as the selector.
The mux is then EVALUATED, not replaced: its result comes from its own declared
arms.  Every other width, constant and operator is read from the certificate.

SCOPE.  This overrides the selector rather than observing it, so it says what
the enable WOULD be at that selector value and asserts nothing about whether
the selector is reached at runtime, nor on which cycle.

An earlier version of this script injected a value at the mux's RESULT slot and
described that as forcing the selector.  Those are different things: result
injection bypasses the arms entirely.  It is fixed here, and the arms are now
printed and checked so the two can never again be confused.

Exits: 0 only when every requested expectation held; 2 on a shape failure
(no memory, no enable operand, no MuxBool, no selector); 3 on a VALUE mismatch
against `--expect`; 4 on an ARM mismatch against `--expect-arms`.
-/
import LeanSemanticPrimitives.Projection.HardwareMemRuntime
import LeanSemanticPrimitives.Compiler.CertIOFacade

open Compiler Projection Projection.HwMem

/-- Evaluate slot `k` of `D`, with `ovr` substituted wherever it is named.
Fuel-bounded; a source is read as its own constant, an input/flop as 0. -/
partial def evalSlot (D : DesignCert) (ovr : Nat × BV) (fuel : Nat) (k : Nat) : BV :=
  if fuel = 0 then mk_bv 0 0
  else if k == ovr.1 then ovr.2
  else if h : k < D.sources.size then
    match D.sources[k]'h with
    | .const w v => mk_bv w v
    | .input _ w => mk_bv w 0
    | .flopQ _ w => mk_bv w 0
    | .flopQAsync _ w _ _ _ => mk_bv w 0
    | _ => mk_bv 0 0
  else
    match D.nodes[k - D.sources.size]? with
    | none   => mk_bv 0 0
    | some c => eval_op c.op c.width (c.deps.toList.map (evalSlot D ovr (fuel - 1)))

/-- Describe a slot as the certificate declares it, for the arm report. -/
def describe (D : DesignCert) (k : Nat) : String :=
  if h : k < D.sources.size then
    match D.sources[k]'h with
    | .const w v => s!"const{w} {v}"
    | .input o w => s!"input{w} #{o}"
    | .flopQ o w => s!"flopQ{w} #{o}"
    | .flopQAsync o w _ _ _ => s!"flopQAsync{w} #{o}"
    | _ => "source"
  else
    match D.nodes[k - D.sources.size]? with
    | some c => s!"{repr c.op} w{c.width}"
    | none   => "?"

def main (args : List String) : IO UInt32 := do
  match args with
  | path :: rest => do
    let sel       := (rest.head?.getD "1").toNat?.getD 1
    let expect    := (rest.drop 1).head?.bind (·.toNat?)
    let expectArms := rest.contains "--expect-arms"
    let bytes ← IO.FS.readBinFile path
    match CertIO.admitAny bytes with
    | .error e => do IO.eprintln s!"PARSE FAILED: {e}"; return 8
    | .ok (_, D) => do
      IO.println s!"{path}"
      IO.println s!"  sources {D.sources.size} nodes {D.nodes.size} memories {D.memories.size}"
      match D.memories[0]? with
      | none => do IO.eprintln "SHAPE FAILURE: no declared memory"; return 2
      | some md =>
        match D.nodes[md.nextImg - D.sources.size]? with
        | none => do IO.eprintln "SHAPE FAILURE: next image is not a node"; return 2
        | some wr =>
          IO.println s!"  write node {repr wr.op} deps {wr.deps.toList}"
          match wr.deps[3]? with
          | none => do IO.eprintln "SHAPE FAILURE: write has no enable operand"; return 2
          | some en =>
            -- find the MuxBool the enable cone passes through
            let rec findMux (fuel k : Nat) : Option Nat :=
              if fuel = 0 then none
              else if k < D.sources.size then none
              else match D.nodes[k - D.sources.size]? with
                   | none   => none
                   | some c =>
                     if c.op == .Op_MuxBool then some k
                     else (c.deps.toList.map (findMux (fuel - 1))).foldl
                            (fun acc r => match acc with | some _ => acc | none => r) none
            match findMux 32 en with
            | none => do IO.eprintln "SHAPE FAILURE: no MuxBool in the enable cone"; return 2
            | some mux => do
              match D.nodes[mux - D.sources.size]? with
              | none => do IO.eprintln "SHAPE FAILURE: the MuxBool slot is not a node"; return 2
              | some mc =>
                -- deps[0] is the SELECTOR (the builder adds bank 0 first); the
                -- rest are the arms.  Report and optionally check the arms, so
                -- result injection can never be mistaken for selector forcing.
                match mc.deps[0]? with
                | none => do IO.eprintln "SHAPE FAILURE: the MuxBool has no selector dependency"; return 2
                | some selSlot => do
                  let arms := mc.deps.toList.drop 1
                  IO.println s!"  enable slot {en}, MuxBool at slot {mux} (w{mc.width})"
                  IO.println s!"    selector dependency slot {selSlot} = {describe D selSlot}"
                  IO.println s!"    arms {arms.map (fun a => describe D a)}"
                  if expectArms then
                    let want := ["const1 0", "const1 -1"]
                    let got  := arms.map (describe D)
                    if got != want then do
                      IO.eprintln s!"ARM MISMATCH: expected {want}, got {got}"
                      return 4
                    else IO.println "    arms match the expected const1 0 / const1 -1"
                  -- Force the REAL selector dependency; the mux itself is evaluated.
                  let muxv := evalSlot D (selSlot, mk_bv 1 (Int.ofNat sel)) 64 mux
                  IO.println s!"    selector forced to {sel} -> mux EVALUATES to width {muxv.width} value {muxv.value}"
                  let v := evalSlot D (selSlot, mk_bv 1 (Int.ofNat sel)) 64 en
                  IO.println s!"  ENABLE = width {v.width} value {v.value}"
                  match expect with
                  | none => return 0
                  | some e =>
                    if v.value.toNat == e then do
                      IO.println s!"  EXPECTED {e}: match"
                      return 0
                    else do
                      IO.eprintln s!"VALUE MISMATCH: expected {e}, got {v.value}"
                      return 3
  | _ => do IO.eprintln "usage: cert_cone_eval <cert.dcert> [selector] [expected] [--expect-arms]"; return 2
