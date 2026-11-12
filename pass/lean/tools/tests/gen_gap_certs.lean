import LeanSemanticPrimitives.Compiler.CertIOCodec

/-!
Certificates no Verilog front end emits, for the symbolic-semantics self-test (sym_selftest.sh):

* `getmask_sym` / `setmask_sym`: `Op_GetMask` / `Op_SetMask` whose MASK is an input. Output 0
  (`ok`) says: when the mask equals the constant `0xA5`, the result equals the same operator with
  that constant mask -- the symbolic-mask builder agrees with the constant-mask one for every data
  input. Output 1 (`bad`) compares with the constant `0xA4` instead and must fail.
* `bulk_ok` / `bulk_bad`: a whole-memory load (`Op_MemLoad`) from a 32-bit bus and the image read
  back onto a bus (`Op_MemToBus`). With the load enabled (`bulk_ok`) the bus comes back unchanged;
  with the enable an input (`bulk_bad`) it does not when disabled. On the fp route this runs the
  residual's `mulI`/`divI`/`modI` by powers of two (packing words into the bus).

    lean --run gen_gap_certs.lean <out-dir>
-/

open Compiler

def oneFlop (din : Nat) : FlopDesc :=
  { width := 8, din, enable := none, resetPin := none, resetValue := 0, resetActiveLow := false,
    clock := 0, asyncReset := false }

/-- x:8 (slot 0), m:8 (1), 0xA5 (2), 0xA4 (3), a register of x (4); nodes from slot 5. -/
def getmaskSym : DesignCert where
  sources := #[.input 0 8, .input 1 8, .const 8 0xA5, .const 8 0xA4, .flopQ 0 8]
  nodes := #[
    { op := .Op_GetMask, width := 8, deps := #[0, 1] },   -- 5: g, symbolic mask
    { op := .Op_GetMask, width := 8, deps := #[0, 2] },   -- 6: g with 0xA5
    { op := .Op_GetMask, width := 8, deps := #[0, 3] },   -- 7: g with 0xA4
    { op := .Op_EQ, width := 1, deps := #[1, 2] },        -- 8: m == 0xA5
    { op := .Op_EQ, width := 1, deps := #[5, 6] },        -- 9
    { op := .Op_EQ, width := 1, deps := #[5, 7] },        -- 10
    { op := .Op_Not, width := 1, deps := #[8] },          -- 11
    { op := .Op_Or, width := 1, deps := #[11, 9] },       -- 12: ok
    { op := .Op_Or, width := 1, deps := #[11, 10] } ]     -- 13: bad
  outputs := #[{ slot := 12, width := 1 }, { slot := 13, width := 1 }, { slot := 5, width := 8 }]
  flops := #[oneFlop 0]
  memories := #[]
  clocks := #[{ name := "clk" }]

/-- a:8 (0), m:8 (1), v:4 (2), 0xA5 (3), 0xA4 (4), a register of a (5); nodes from slot 6. -/
def setmaskSym : DesignCert where
  sources := #[.input 0 8, .input 1 8, .input 2 4, .const 8 0xA5, .const 8 0xA4, .flopQ 0 8]
  nodes := #[
    { op := .Op_SetMask, width := 8, deps := #[0, 1, 2] },  -- 6
    { op := .Op_SetMask, width := 8, deps := #[0, 3, 2] },  -- 7
    { op := .Op_SetMask, width := 8, deps := #[0, 4, 2] },  -- 8
    { op := .Op_EQ, width := 1, deps := #[1, 3] },          -- 9
    { op := .Op_EQ, width := 1, deps := #[6, 7] },          -- 10
    { op := .Op_EQ, width := 1, deps := #[6, 8] },          -- 11
    { op := .Op_Not, width := 1, deps := #[9] },            -- 12
    { op := .Op_Or, width := 1, deps := #[12, 10] },        -- 13: ok
    { op := .Op_Or, width := 1, deps := #[12, 11] } ]       -- 14: bad
  outputs := #[{ slot := 13, width := 1 }, { slot := 14, width := 1 }, { slot := 6, width := 8 }]
  flops := #[oneFlop 0]
  memories := #[]
  clocks := #[{ name := "clk" }]

/-- image (0), bus:32 (1), enable (2: the constant 1, or an input); nodes from slot 3. -/
def bulk (enInput : Bool) : DesignCert where
  sources := #[.memImg 0 2 8, .input 0 32, if enInput then .input 1 1 else .const 1 1]
  nodes := #[
    { op := .Op_MemLoad 4, width := 8, deps := #[0, 1, 2] },  -- 3: the image after the load
    { op := .Op_MemToBus 4, width := 32, deps := #[3] },      -- 4: read back onto a bus
    { op := .Op_EQ, width := 1, deps := #[4, 1] } ]           -- 5: ok
  outputs := #[{ slot := 5, width := 1 }, { slot := 4, width := 32 }]
  flops := #[]
  memories := #[{ aw := 2, dw := 8, nextImg := 3, clock := 0 }]
  clocks := #[{ name := "clk" }]

def main (args : List String) : IO Unit := do
  let dir := System.FilePath.mk (args.headD ".")
  IO.FS.createDirAll dir
  for (name, D) in [("getmask_sym", getmaskSym), ("setmask_sym", setmaskSym),
                    ("bulk_ok", bulk false), ("bulk_bad", bulk true)] do
    IO.FS.writeBinFile (dir / s!"{name}.dcert") (CertIO.renderCert D)
  IO.println s!"wrote 4 certificates to {dir}"
