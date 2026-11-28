/-
# DCERT3 to DCERT4, lossless -- Mathlib-free

A DCERT3 certificate is a DCERT4 certificate without the memory depth: its memory record ends
before the `depth` field, and every memory means the whole `2 ^ aw` table, the field's default
(`MemoryDesc.depth`). So a DCERT3 file converts EXACTLY: read it into the same `DesignCert` the
DCERT4 reader would build with every depth at `2 ^ aw`, and write that record with `renderCert`.
The record does not change, so neither does anything computed from it: `Ckpt.designHash` is
`fnv1a64 (renderCert D)`, and every checkpoint taken against the DCERT3 file checks against the
DCERT4 one.

`upgrade` refuses, and writes nothing for:
* a file that is not plainly DCERT3: DCERT4 (already converted), DCERT2, DCERT1, any other magic;
* a DCERT3 file that does not parse;
* a DCERT3 file that is not CANONICAL: the reader skips any whitespace, so a hand-edited file can
  parse; its DCERT3 rendering (`renderCert3`) must equal the input byte for byte, or the output
  would differ from the input in more than the version;
* a DCERT3 file that admission refuses (`certWfError`): a refused certificate does not become a
  refused DCERT4 certificate silently.
and checks its own output: `readCertChecked` admits it, it re-renders to itself (so, by
`renderCert_injective`, it is the same record), and every memory is at depth `2 ^ aw`.

The depth this gives is the DCERT3 meaning, not the memory's real size: a 12-word memory converts
to depth 16. A fresh lhd export writes the real depth (and so a stronger address check), so
re-export where the RTL is at hand; convert the files whose flow cannot be rerun.

The driver is `pass/lean/tools/sim/dcert_upgrade.lean`.
-/
import LeanSemanticPrimitives.Compiler.CertIOCodec

namespace Compiler
namespace CertIO
namespace Upgrade

/-- "DCERT3\n". -/
def magic3Bytes : List UInt8 := [68, 67, 69, 82, 84, 51, 10]

/-- The DCERT3 memory record: `memBody` without its last field, the depth. -/
def memBody3 (m : MemoryDesc) : List UInt8 :=
  let (ha, a) := match m.asyncImg with | some x => (1, x) | none => (0, 0)
  natBytes m.aw ++ spc (natBytes m.dw) ++ spc (natBytes m.nextImg) ++ spc (natBytes m.clock)
    ++ spc (natBytes m.init.size) ++ (m.init.toList.map (fun v => spc (natBytes v))).flatten
    ++ spc (natBytes m.commits.size) ++ (m.commits.toList.map commitBody).flatten
    ++ spc (natBytes ha) ++ spc (natBytes a)

/-- `D` in the DCERT3 layout, which `upgrade` compares with its input. -/
def renderCert3 (D : DesignCert) : ByteArray :=
  ⟨(magic3Bytes
    ++ section' srcBytes  D.sources
    ++ section' nodeBytes D.nodes
    ++ section' outBytes  D.outputs
    ++ section' flopBytes D.flops
    ++ section' (fun m => memBody3 m ++ [chNL]) D.memories
    ++ section' clkBytes  D.clocks).toArray⟩

/-- Read a DCERT3 certificate with the DCERT4 reader's sections (`parseCert.go`) and the DCERT3
memory record (`readMemory3`): every memory at its default depth, the whole `2 ^ aw` table. -/
def parseCert3 (b : ByteArray) : Except String DesignCert :=
  if !hasMagic3 b then .error "bad magic: expected DCERT3"
  else match parseCert.go readMemory3 b with
    | some D => .ok D
    | none   => .error "malformed certificate"

/-- The first offset where two byte arrays differ (the shorter size if one is a prefix). -/
def firstDiff (a b : ByteArray) : Nat :=
  let n := min a.size b.size
  ((List.range n).find? fun k => a[k]! != b[k]!).getD n

/-- DCERT3 bytes to the DCERT4 bytes of the same record, with every check in the header. -/
def upgrade (b : ByteArray) : Except String (DesignCert × ByteArray) := do
  if hasMagic b then throw "already DCERT4: nothing to convert"
  let D ← match parseCert3 b with
    | .ok D => pure D
    | .error e => throw s!"parse: {e}"
  let again := renderCert3 D
  unless again == b do
    throw s!"not canonical DCERT3: re-rendering the parsed certificate differs from the input at \
             byte {firstDiff again b} ({b.size} bytes in, {again.size} out)"
  if let some w := certWfError D then throw s!"refused: {w}"
  let out := renderCert D
  let D' ← match readCertChecked out with
    | .ok D' => pure D'
    | .error e => throw s!"internal: the DCERT4 output does not admit: {e}"
  unless renderCert D' == out do throw "internal: the DCERT4 output reads back as another record"
  unless D'.memories.all (fun m => m.depth == 2 ^ m.aw) do
    throw "internal: a converted memory is not at the whole 2^aw table"
  return (D', out)

end Upgrade
end CertIO
end Compiler
