/- Operator census of an ACTUALLY EMITTED certificate: how many nodes of each
operator, so an adapter's reach can be bounded rather than assumed. -/
import LeanSemanticPrimitives.Projection.HardwareMemRuntime
import LeanSemanticPrimitives.Compiler.CertIOFacade
open Compiler Projection

def tally (xs : List String) : List (String × Nat) :=
  xs.foldl (fun acc k =>
    if acc.any (fun p => p.1 == k) then acc.map (fun p => if p.1 == k then (p.1, p.2 + 1) else p)
    else acc ++ [(k, 1)]) []

def main (args : List String) : IO UInt32 := do
  match args with
  | path :: _ => do
    let bytes ← IO.FS.readBinFile path
    match CertIO.admitAny bytes with
    | .error e => do IO.eprintln s!"PARSE FAILED: {e}"; return 8
    | .ok (_, D) => do
      let t := tally (D.nodes.toList.map (fun c => toString (repr c.op)))
      IO.println s!"{path}"
      IO.println s!"  sources {D.sources.size} nodes {D.nodes.size}"
      for (k, n) in t do
        if (k.splitOn "GetMask").length > 1 || (k.splitOn "Sext").length > 1 then
          IO.println s!"  {k} x{n}"
      -- Per-GetMask detail: the window's endpoints come from the mask CONSTANT
      -- (lo = trailing zeros, hi = bit length), so "how far the sign must
      -- reach" can be compared against the node's RESULT width.
      let srcW := fun (k : Nat) =>
        if h : k < D.sources.size then
          match D.sources[k]'h with
          | .const w _ => (w, "const")
          | .input _ w => (w, "input")
          | .flopQ _ w => (w, "flopQ")
          | .flopQAsync _ w _ _ _ => (w, "flopQA")
          | .memImg _ _ dw => (dw, "memImg")
          | .memConst _ dw _ => (dw, "memConst")
        else
          match D.nodes[k - D.sources.size]? with
          | some c => (c.width, toString (repr c.op))
          | none   => (0, "?")
      let constOf := fun (k : Nat) =>
        if h : k < D.sources.size then
          match D.sources[k]'h with
          | .const _ v => some v
          | _ => none
        else none
      let mut extended := 0
      let mut shifted := 0
      let mut endpointNeWidth := 0
      for (c, j) in D.nodes.toList.zipIdx do
        if c.op == .Op_GetMask then
          match c.deps[0]?, c.deps[1]? with
          | some a, some m =>
            let (aw, akind) := srcW a
            let isSext := akind == "LGraphOp.Op_Sext"
            let mv := (constOf m).getD 0
            -- hi = bit length of the mask, lo = trailing zeros
            let hi := (List.range 64).foldl
              (fun acc i => if mv / (2 ^ i) > 0 then i + 1 else acc) 0
            let lo := (List.range 64).foldl
              (fun acc i => if acc == 64 && (mv / (2 ^ i)) % 2 == 1 then i else acc) 64
            if isSext then
              extended := extended + 1
              if lo > 0 then shifted := shifted + 1
              if aw != c.width then endpointNeWidth := endpointNeWidth + 1
              if extended <= 3 then
                IO.println s!"  EXTENDED node {j}: result w{c.width}, operand0 = Sext w{aw}, window [{lo},{hi})"
          | _, _ => pure ()
      IO.println s!"  GetMask with a Sext operand: {extended}"
      IO.println s!"    of which the window is SHIFTED (lo > 0): {shifted}"
      IO.println s!"    of which the extension width differs from the RESULT width: {endpointNeWidth}"
      return 0
  | _ => do IO.eprintln "usage: cert_op_census <cert.dcert>"; return 2
