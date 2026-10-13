/-
CONTROL: evaluate a memory write's ENABLE cone on an ACTUALLY EMITTED
certificate, with one slot overridden.

This is not a hand-built term comparison.  It parses a real `.dcert`, finds the
declared memory's next-image write node, takes its ENABLE operand, and
evaluates that cone with the `Op_MuxBool` selector forced to a supplied value,
reading every other operand -- widths, constants, operators -- from the
certificate itself.

It evaluates a CONE, not a design: it overrides the selector rather than
observing it, so it says what the enable WOULD be at that selector value and
asserts nothing about whether the selector is reached at runtime.
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

def main (args : List String) : IO UInt32 := do
  match args with
  | path :: rest => do
    let sel := (rest.head?.getD "1").toNat?.getD 1
    let bytes ← IO.FS.readBinFile path
    match CertIO.admitAny bytes with
    | .error e => do IO.eprintln s!"PARSE FAILED: {e}"; return 8
    | .ok (_, D) => do
      IO.println s!"{path}"
      IO.println s!"  sources {D.sources.size} nodes {D.nodes.size} memories {D.memories.size}"
      match D.memories[0]? with
      | none => do IO.eprintln "no declared memory"; return 2
      | some md =>
        match D.nodes[md.nextImg - D.sources.size]? with
        | none => do IO.eprintln "next image is not a node"; return 2
        | some wr =>
          IO.println s!"  write node {repr wr.op} deps {wr.deps.toList}"
          match wr.deps[3]? with
          | none => do IO.eprintln "write has no enable operand"; return 2
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
            | none => do IO.eprintln "no MuxBool in the enable cone"; return 2
            | some mux => do
              IO.println s!"  enable slot {en}, MuxBool at slot {mux}, selector forced to {sel}"
              let v := evalSlot D (mux, mk_bv 1 (Int.ofNat sel)) 64 en
              IO.println s!"  ENABLE = width {v.width} value {v.value}"
              return 0
  | _ => do IO.eprintln "usage: cert_cone_eval <cert.dcert> [selector]"; return 2
