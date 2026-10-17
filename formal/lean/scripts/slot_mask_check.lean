/-
CONTROL: the emitted write-enable gate's VALUE, on an actual certificate.

The §63.75 gate emitted `Op_And w69 [enable, Op_Sext w1]`.  Under the
certificate's own semantics `Op_And` does not sign-extend, so a ONE-BIT mask
has only bit 0 set and lanes 1..68 could never be written -- a static
consequence of the emitted graph, independent of whether any write is reachable.

This evaluates the write node's ENABLE operand with two slots overridden:
the phase source (so the slot predicate is active or quiet) and the gate's
other `And` operand (the design's own enable cone), so the mask's effect is
observed rather than argued.  It OVERRIDES those slots; it says nothing about
whether a particular phase or enable occurs at runtime.

Exits: 0 only if every requested expectation held; 2 on a shape failure;
3 on a VALUE mismatch.
-/
import LeanSemanticPrimitives.Projection.HardwareMemRuntime
import LeanSemanticPrimitives.Compiler.CertIOFacade

open Compiler Projection

/-- Evaluate slot `k`, substituting every (slot, value) in `ovr`. -/
partial def evalWith (D : DesignCert) (ovr : List (Nat × BV)) (fuel k : Nat) : BV :=
  if fuel = 0 then mk_bv 0 0 else
  match ovr.find? (fun p => p.1 == k) with
  | some p => p.2
  | none =>
    if h : k < D.sources.size then
      match D.sources[k]'h with
      | .const w v => mk_bv w v
      | .input _ w => mk_bv w 0
      | .flopQ _ w => mk_bv w 0
      | .flopQAsync _ w _ _ _ => mk_bv w 0
      | _ => mk_bv 0 0
    else
      match D.nodes[k - D.sources.size]? with
      | none   => mk_bv 0 0
      | some c => eval_op c.op c.width (c.deps.toList.map (evalWith D ovr (fuel - 1)))

def main (args : List String) : IO UInt32 := do
  match args with
  | path :: _rest => do
    let bytes ← IO.FS.readBinFile path
    match CertIO.admitAny bytes with
    | .error e => do IO.eprintln s!"PARSE FAILED: {e}"; return 8
    | .ok (_, D) => do
      IO.println s!"{path}"
      -- the write node and its enable
      match D.memories[0]? with
      | none => do IO.eprintln "SHAPE FAILURE: no declared memory"; return 2
      | some md =>
        match D.nodes[md.nextImg - D.sources.size]? with
        | none => do IO.eprintln "SHAPE FAILURE: next image is not a node"; return 2
        | some wr =>
          -- THE EXPECTED GEOMETRY COMES FROM THE DECLARED MEMORY, NOT FROM THE
          -- CANDIDATE GATE.  Deriving the expected width from the `And` would
          -- let a NARROWED gate narrow its own expected answer and pass.
          let dw := md.dw
          match wr.deps[3]? with
          | none => do IO.eprintln "SHAPE FAILURE: the write has no enable operand"; return 2
          | some en0 =>
            -- walk any Or wrappers down to the gate itself
            let rec gateOf (fuel k : Nat) : Nat :=
              if fuel = 0 then k
              else match D.nodes[k - D.sources.size]? with
                   | some c => if c.op == .Op_Or && c.deps.size == 1
                               then gateOf (fuel-1) (c.deps[0]!) else k
                   | none   => k
            let gate := gateOf 8 en0
            match D.nodes[gate - D.sources.size]? with
            | none => do IO.eprintln "SHAPE FAILURE: the gate is not a node"; return 2
            | some gc => do
              -- the WRITE's own shape, checked rather than assumed
              let bw := match wr.op with | .Op_MemWriteBE b => b | _ => 0
              IO.println s!"  write {repr wr.op} width {wr.width}; declared memory aw {md.aw} dw {dw}; byte_w {bw}"
              if wr.width != dw then
                IO.eprintln s!"SHAPE FAILURE: the write's result width {wr.width} is not the declared dw {dw}"
                return 2
              if bw == 0 then
                IO.eprintln "SHAPE FAILURE: the next image is not an Op_MemWriteBE"
                return 2
              -- the lane count the ENABLE must carry, from the declared memory
              let lanes := (dw + bw - 1) / bw
              IO.println s!"  gate slot {gate}: {repr gc.op} width {gc.width} deps {gc.deps.toList}"
              if gc.op != .Op_And || gc.deps.size != 2 then
                IO.eprintln s!"SHAPE FAILURE: expected a two-operand Op_And gate, got {repr gc.op}"
                return 2
              if gc.width != lanes then
                IO.eprintln s!"SHAPE FAILURE: the gate is {gc.width} bits but the declared memory needs {lanes} enable lane(s)"
                return 2
              let w := lanes
              -- the phase source: the EQ's non-constant dependency
              let maskSlot := gc.deps[1]!
              let enSlot   := gc.deps[0]!
              let rec phaseOf (fuel k : Nat) : Option Nat :=
                if fuel = 0 then none
                else if k < D.sources.size then none
                else match D.nodes[k - D.sources.size]? with
                     | none   => none
                     | some c =>
                       if c.op == .Op_EQ then
                         (c.deps.toList.filter (fun d => d < D.sources.size)).find?
                           (fun d => match D.sources[d]? with
                                     | some (.flopQ _ _) => true
                                     | _ => false)
                       else (c.deps.toList.map (phaseOf (fuel-1))).foldl
                              (fun a r => match a with | some _ => a | none => r) none
              match phaseOf 16 maskSlot with
              | none => do IO.eprintln "SHAPE FAILURE: no phase flop found in the mask cone"; return 2
              | some ph => do
                IO.println s!"  phase source {ph}, mask slot {maskSlot}, enable slot {enSlot}"
                let allOnes := mk_bv w ((2 : Int) ^ w - 1)
                let highBit := mk_bv w ((2 : Int) ^ (w - 1))
                let run (phv : Nat) (env : BV) : BV :=
                  evalWith D [(ph, mk_bv 1 (Int.ofNat phv)), (enSlot, env)] 64 gate
                -- ACTIVE (phase == 0, the slot predicate's constant) with a full enable
                let a := run 0 allOnes
                -- QUIET
                let q := run 1 allOnes
                -- ACTIVE with ONLY the top lane enabled
                let h := run 0 highBit
                IO.println s!"  active/full  = {a.value}  (want {allOnes.value})"
                IO.println s!"  quiet/full   = {q.value}  (want 0)"
                IO.println s!"  active/high  = {h.value}  (want {highBit.value})"
                let mut bad := 0
                if a.value != allOnes.value then
                  IO.eprintln s!"VALUE MISMATCH active/full: the mask does not pass every lane"; bad := bad + 1
                if q.value != 0 then
                  IO.eprintln s!"VALUE MISMATCH quiet: a quiet step does not zero the vector"; bad := bad + 1
                if h.value != highBit.value then
                  IO.eprintln s!"VALUE MISMATCH active/high: lane {w-1} is not preserved by the mask"; bad := bad + 1
                if bad == 0 then do
                  -- SCOPE: the phase source and the gate's other operand are
                  -- OVERRIDDEN, so this says what the gate WOULD pass at those
                  -- values.  It is conditional preservation of the lanes, NOT
                  -- evidence that any phase, enable or write is reachable.
                  IO.println "SLOT MASK OK (conditional: every lane preserved when active, zeroed when quiet)"
                  return 0
                else do
                  IO.eprintln s!"{bad} slot-mask check(s) FAILED"
                  return 3
  | _ => do IO.eprintln "usage: slot_mask_check <cert.dcert>"; return 2
