/-
# The original DCERT1 grammar and admission refusals, with their REASONS

`CertLoad.lean` carries these as `#guard rejects "..."`, which confirms that a
malformed input is refused but not WHY. A wrong-magic rejection discharges
nothing about the body: every one of these must refuse for its own reason, and
this file prints the reason the parser actually gives so that can be read
rather than assumed.

The ADMISSION refusals are separate from the GRAMMAR ones. A file can parse
perfectly and still be refused by `SupportedByProjection`, and the `wide`
fixture is exactly that case -- it parses, and then `asyncOK` refuses it
because DCERT1 cannot carry `FlopDesc.asyncReset` and simulating it as `false`
would be a different design.
-/

import LeanSemanticPrimitives.Projection.CertLoad

open Compiler Compiler.CertIO.V1 Projection

def reasonOf (s : String) : String :=
  match parseCert s.toUTF8 with
  | .ok _    => "ACCEPTED"
  | .error e => e

def show1 (label input : String) : IO Unit :=
  IO.println s!"  {label}: {reasonOf input}"

def main : IO Unit := do
  IO.println "GRAMMAR refusals (each must refuse, and the reason is printed):"
  show1 "empty (no magic)          " ""
  show1 "wrong magic DCERX1        " "DCERX1\n0 0 0 0 0\n"
  show1 "truncated, no counts      " "DCERT1\n"
  show1 "missing nodes count       " "DCERT1\n1\n0 0 4\n0\n0\n0\n"
  show1 "unknown opcode 99         " "DCERT1\n0\n1\n99 0 4 0 7\n0\n0\n0\n"
  show1 "trailing garbage          " "DCERT1\n0\n0\n0\n0\n0\n7\n"
  show1 "source count > records    " "DCERT1\n2\n0 0 4\n0\n0\n0\n0\n"
  show1 "unknown source tag 9      " "DCERT1\n1\n9 0 4\n0\n0\n0\n0\n"
  IO.println "and the minimal well-formed file:"
  show1 "minimal valid             " "DCERT1\n0\n0\n0\n0\n0\n"
  IO.println ""
  IO.println "ADMISSION, per field, on the three emitted fixtures:"
  for (nm, D) in [("tiny", Projection.Acceptance.tinyD),
                  ("seq",  Projection.Acceptance.seqD),
                  ("wide", CertLoad.wideD)] do
    let r := CertLoad.supportReport D
    IO.println s!"  {nm}: wf {r.wf} memFree {r.memFree} sources {r.sources} \
ops {r.ops} arities {r.arities} flopClocks {r.flopClocks} | ALL {r.allOK}"
