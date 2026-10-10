/-
  Write a tiny, self-contained DCERT2 file.

  The replay-gate controls need a certificate they can corrupt, delete and
  mis-hash on purpose.  Borrowing one from `temp/` would tie a tracked test to
  untracked sweep leftovers, so this probe MAKES one, through the same verified
  renderer the C++ serializer is checked against (`renderCert`, the function
  `parseCert_renderCert` proves `parseCert` inverts).

  Usage:  lake env lean --run probes/d4_write_tiny_cert.lean <out.dcert>

RESTORED from pinned D4 f6ad1fe29f8f15aeda045115864fd81a34c33aef. Two
adaptations, and nothing else:

  * the format is DCERT2. It carries the clock table, `FlopDesc.clock`,
    `asyncReset` and `MemoryDesc.clock`, and `parseCert_renderCert` proves the
    codec total and invertible for EVERY `DesignCert`. A historical DCERT1 file
    is refused by magic rather than reinterpreted as a one-clock design.

  This probe WRITES a certificate, so it writes the current format: DCERT2,
  with the clock table stated rather than defaulted. That is not a
  reinterpretation of anything -- no historical bytes are read here.
-/
import LeanSemanticPrimitives.Compiler.CertIO
open Compiler CertIO

/-- `y = (a + b) << 1`, 8 bits, one flop so the row is not degenerate. -/
def tinyCert : DesignCert :=
  { sources  := #[.input 0 8, .input 1 8, .const 8 1, .flopQ 0 8]
    nodes    := #[{ op := .Op_Sum 2, width := 8, deps := #[0, 1] }
                 ,{ op := .Op_SHL,   width := 8, deps := #[4, 2] }]
    outputs  := #[{ slot := 5, width := 8 }]
    flops    := #[{ width := 8, din := 5, enable := none, resetPin := none,
                    resetValue := 0, resetActiveLow := false,
                    clock := 0, asyncReset := false }]
    memories := #[]
    -- Stated, not defaulted: a certificate should say what domain it is on.
    clocks   := #[{ name := "clock" }] }

def main (args : List String) : IO Unit := do
  let path := args.headD ""
  if path == "" then
    IO.eprintln "usage: d4_write_tiny_cert.lean <out.dcert>"
    IO.Process.exit 2
  let h ← IO.FS.Handle.mk path IO.FS.Mode.write
  writeCert h tinyCert
  IO.println s!"wrote {path}"
