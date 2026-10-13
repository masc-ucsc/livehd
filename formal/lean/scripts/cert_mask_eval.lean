/-
CONTROL: evaluate a Get_mask node's VALUE on an ACTUALLY EMITTED certificate,
with the design's single input forced to a supplied value.

It parses a real `.dcert`, locates the (single) `Op_GetMask` node, and
evaluates its cone -- widths, constants and operators all read from the
certificate itself, including any `Op_Sext` the translation adapter inserted.

It reports the mask constant AS THE CERTIFICATE DECLARES IT (declared width and
declared value) alongside the width that value actually needs, because the two
can disagree, and the number of set bits in the result.
-/
import LeanSemanticPrimitives.Projection.HardwareMemRuntime
import LeanSemanticPrimitives.Compiler.CertIOFacade

open Compiler Projection

/-- Evaluate slot `k` of `D` with every `.input` read as `inp`. Fuel-bounded. -/
partial def evalWithInput (D : DesignCert) (inp : Nat) (fuel : Nat) (k : Nat) : BV :=
  if fuel = 0 then mk_bv 0 0
  else if h : k < D.sources.size then
    match D.sources[k]'h with
    | .const w v => mk_bv w v
    | .input _ w => mk_bv w (Int.ofNat inp)
    | .flopQ _ w => mk_bv w 0
    | .flopQAsync _ w _ _ _ => mk_bv w 0
    | _ => mk_bv 0 0
  else
    match D.nodes[k - D.sources.size]? with
    | none   => mk_bv 0 0
    | some c => eval_op c.op c.width (c.deps.toList.map (evalWithInput D inp (fuel - 1)))

/-- Number of set bits in `v`, read from the value, not from the width. -/
partial def popcountOf (v : Nat) : Nat :=
  if v = 0 then 0 else v % 2 + popcountOf (v / 2)

/-- Smallest width that can hold `v`. -/
partial def bitsNeeded (v : Nat) : Nat :=
  if v = 0 then 0 else 1 + bitsNeeded (v / 2)

def main (args : List String) : IO UInt32 := do
  match args with
  | path :: rest => do
    let inp := (rest.head?.getD "128").toNat?.getD 128
    let bytes ← IO.FS.readBinFile path
    match CertIO.admitAny bytes with
    | .error e => do IO.eprintln s!"PARSE FAILED: {e}"; return 8
    | .ok (_, D) => do
      IO.println s!"{path}"
      IO.println s!"  sources {D.sources.size} nodes {D.nodes.size}, input forced to {inp}"
      -- report every Sext the adapter may have inserted
      for i in List.range D.nodes.size do
        match D.nodes[i]? with
        | some c => if c.op == .Op_Sext then
                      IO.println s!"  Op_Sext at slot {D.sources.size + i}: width {c.width} deps {c.deps.toList}"
        | none => pure ()
      let mut found := false
      for i in List.range D.nodes.size do
        match D.nodes[i]? with
        | none   => pure ()
        | some c =>
          if c.op == .Op_GetMask then
            found := true
            let slot := D.sources.size + i
            IO.println s!"  Op_GetMask at slot {slot}: result width {c.width} deps {c.deps.toList}"
            match c.deps[1]? with
            | none => IO.println "    (no mask operand)"
            | some mk =>
              if h : mk < D.sources.size then
                match D.sources[mk]'h with
                | .const w v =>
                  let vn := v.toNat
                  IO.println s!"    mask constant DECLARED width {w} value {vn} (needs {bitsNeeded vn} bits){if bitsNeeded vn > w then "  <-- DOES NOT FIT" else ""}"
                  IO.println s!"    mask AS READ at the declared width = {(mk_bv w v).value}"
                | _ => IO.println "    (mask operand is not a constant source)"
              else IO.println "    (mask operand is a node)"
            let v := evalWithInput D inp 64 slot
            IO.println s!"    RESULT width {v.width} value {v.value}  ({popcountOf v.value.toNat} set bit(s))"
      if !found then IO.println "  no Op_GetMask node in this certificate"
      return 0
  | _ => do IO.eprintln "usage: cert_mask_eval <cert.dcert> [input]"; return 2
