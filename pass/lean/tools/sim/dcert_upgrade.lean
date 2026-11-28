/-
# dcert_upgrade: a DCERT3 certificate as DCERT4, the same record

  dcert_upgrade <in.dcert> <out.dcert>

Reads `<in.dcert>` (DCERT3), converts it with `Compiler.CertIO.Upgrade.upgrade` and writes
`<out.dcert>` (DCERT4). Every memory gets depth `2 ^ aw`, the meaning a DCERT3 memory always had;
nothing else changes, so the design hash (`Ckpt.designHash`, over `renderCert` of the record) is
the same and every checkpoint of the DCERT3 file checks against the DCERT4 one. The checks and
the refusals are in `Compiler/CertIOUpgrade.lean`.

The output is a NEW file: a path that exists, or the input's own path, is refused.

Exit codes: 0 converted; 1 refused (the input is not a canonical, admissible DCERT3 certificate:
nothing is written); 2 usage or I/O.
-/
import LeanSemanticPrimitives.Compiler.CertIOUpgrade
import LeanSemanticPrimitives.Compiler.Ckpt

open Compiler Compiler.CertIO

def usage : String := "usage: dcert_upgrade <in.dcert> <out.dcert>"

def main (args : List String) : IO UInt32 := do
  match args with
  | [inp, out] =>
    if ← System.FilePath.pathExists out then
      IO.eprintln s!"dcert_upgrade: {out} exists; the output must be a new file"
      return 2
    let b ← match ← (IO.FS.readBinFile inp).toBaseIO with
      | .ok b => pure b
      | .error e => do IO.eprintln s!"dcert_upgrade: {inp}: {e}"; return 2
    match Upgrade.upgrade b with
    | .error e =>
      IO.eprintln s!"{inp}: {e}"
      IO.println "RESULT: REFUSED (nothing written)"
      return 1
    | .ok (D, bytes) =>
      if let .error e ← (IO.FS.writeBinFile out bytes).toBaseIO then
        IO.eprintln s!"dcert_upgrade: {out}: {e}"; return 2
      IO.println s!"{inp}: DCERT3, {b.size} bytes, {D.memories.size} memories"
      IO.println s!"{out}: DCERT4, {bytes.size} bytes; same record: yes; every memory at depth 2^aw: \
{", ".intercalate (D.memories.toList.map fun m => toString m.depth)}"
      IO.println s!"design hash {Ckpt.hex16 (Ckpt.designHash D)} (the same for both files)"
      IO.println "RESULT: CONVERTED"
      return 0
  | _ => IO.eprintln usage; return 2
