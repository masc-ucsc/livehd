/-
# The reset-polarity admission guard

A flop's reset polarity is recorded TWICE in a certificate, because the reset
acts in two places:

  * `FlopDesc.resetActiveLow` -- the COMMIT side, read by `flopNext` with the
    slot environment (`FlopDesc.resetPin` names a slot);
  * `SourceDesc.flopQAsync … activeLow` -- the READ side of an asynchronous
    reset, read by `sourceValue` before any slot exists (`resetInput` names a
    primary-input ordinal).

Both polarities are legal hardware (LiveHD's Flop carries a constant
`negreset` pin), and the two records must agree for the SAME flop on the SAME
signal.  The DCERT1 exporter did not guarantee that: the imported DCERT1 corpus
has 5,837 raw-port opposite polarities across 59 certificates, master's DCERT2
writer none in 132 (`pass/lean/tests/fp/fp_reset_polarity_census.tsv`), because
master derives both records from one scanned `active_low`.  On such a
certificate one of the two readings is always in reset -- `btb_gate` simulates
all-zero at either reset level -- so agreement between two simulators of it is
nearly vacuous evidence about the RTL.

This is classified as a DCERT1 EXPORTER DEFECT, and this guard refuses it:
a certificate is refused when a flop's `resetPin` slot is the RAW primary input
`ri` (an `.input ri _` source) and a `flopQAsync` source for that flop reads
the same `ri` with the opposite polarity.

A reset that reaches the flop through a NODE -- an inverter, say -- is allowed
whatever the two flags say: the two routes then read different signals and may
legitimately differ.  That is the node-mediated case the census counts (2,195
in master's DCERT2) and it is not a conflict.

`DirectCheck` explicitly declines to cross-check polarity; this guard is the
admission-side check, applied by the FP drivers.
-/

import LeanSemanticPrimitives.Compiler.DesignCert

namespace Compiler
namespace ResetPolarity

/-- One refused contradiction: flop `flop`, raw input `input`, the commit-side
flag and the read-side flag. -/
structure Conflict where
  flop       : Nat
  input      : Nat
  commitLow  : Bool
  readLow    : Bool
deriving Repr, DecidableEq, Inhabited

/-- Every raw-input polarity contradiction in `D`, in source order. -/
def conflicts (D : DesignCert) : Array Conflict :=
  D.sources.foldl (init := #[]) fun acc s =>
    match s with
    | .flopQAsync k _ ri _ al =>
        match D.flops[k]? with
        | some f =>
            match f.resetPin with
            | some p =>
                match D.sources[p]? with
                | some (.input ri' _) =>
                    if ri' == ri && f.resetActiveLow != al then
                      acc.push ⟨k, ri, f.resetActiveLow, al⟩
                    else acc
                | _ => acc            -- node-mediated, or another source kind: allowed
            | none => acc
        | none => acc
    | _ => acc

/-- The admission guard: no raw-input polarity contradiction. -/
def polarityOK (D : DesignCert) : Bool := (conflicts D).isEmpty

/-- A human-readable refusal, or `none`. -/
def polarityError (D : DesignCert) : Option String :=
  match (conflicts D)[0]? with
  | none   => none
  | some c => some s!"reset polarity conflict on flop {c.flop}: its reset pin is raw \
input {c.input} with activeLow={c.commitLow}, but its flopQAsync source reads the \
same input with activeLow={c.readLow} ({(conflicts D).size} conflict(s); a DCERT1 \
exporter defect -- re-export as DCERT2)"

/-! ## Controls -/

/-- reset on raw input 0; data on input 1 -/
private def base (commitLow readLow : Bool) (resetSlot : Nat) (nodes : Array DenseNodeCert) :
    DesignCert where
  sources := #[.input 0 1, .input 1 4, .flopQAsync 0 4 0 0 readLow]
  nodes := nodes
  outputs := #[⟨2, 4⟩]
  flops := #[{ width := 4, din := 1, enable := none, resetPin := some resetSlot,
               resetValue := 0, resetActiveLow := commitLow, asyncReset := true }]
  memories := #[]

-- same raw input, opposite polarity: the defect
#guard polarityOK (base false true 0 #[]) == false
#guard (conflicts (base false true 0 #[])).size == 1
#guard (polarityError (base false true 0 #[])).isSome
-- same raw input, same polarity: fine
#guard polarityOK (base true true 0 #[]) == true
-- through an inverter node (slot 3 = node 0 = not(input 0)): allowed whatever the flags
#guard polarityOK (base false true 3 #[{ op := .Op_Not, width := 1, deps := #[0] }]) == true
-- no reset pin at all: nothing to compare
#guard polarityOK { base false true 0 #[] with
          flops := #[{ width := 4, din := 1, enable := none, resetPin := none,
                       resetValue := 0, resetActiveLow := false, asyncReset := true }] } == true

end ResetPolarity
end Compiler
