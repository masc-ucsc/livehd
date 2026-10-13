/-
The ordinary-write admission route, run on ACTUALLY RETAINED certificates.

§63.58's census found exactly one operator gap across the whole retained
corpus -- `Op_MemWrite` -- and named the five certificates it refuses.  This
driver loads each one and prints BOTH answers:

  * `admitsB`  — the ORIGINAL checker, whose contract is unchanged and which
                 still refuses a plain write;
  * `admitsWB` — the EXTENDED checker.

It also counts the plain-write nodes, so a design that is refused for some
OTHER reason is not silently read as evidence about this one.  A certificate
with no plain write must give the SAME answer on both routes
(`admitsWB_eq` proves that; here it is also observed).

Exit 0 only when every file was parsed and every no-plain-write design agreed.
-/
import LeanSemanticPrimitives.Projection.HardwareMemWriteAdmit
import LeanSemanticPrimitives.Compiler.CertIOFacade

open Compiler Projection Projection.HwMem

def plainWrites (D : DesignCert) : Nat :=
  D.nodes.toList.foldl (fun n c => if c.op == .Op_MemWrite then n + 1 else n) 0

def beWrites (D : DesignCert) : Nat :=
  D.nodes.toList.foldl (fun n c =>
    match c.op with | .Op_MemWriteBE _ => n + 1 | _ => n) 0

def main (args : List String) : IO UInt32 := do
  let mut bad := 0
  for path in args do
    let bytes ← IO.FS.readBinFile path
    match CertIO.admitAny bytes with
    | .error e => do IO.eprintln s!"PARSE FAILED {path}: {e}"; bad := bad + 1
    | .ok (_, D) =>
      let pw := plainWrites D
      let be := beWrites D
      let a  := admitsB D
      let aw := admitsWB D
      IO.println s!"{path}"
      IO.println s!"  nodes {D.nodes.size}  memories {D.memories.size}  Op_MemWrite {pw}  Op_MemWriteBE {be}"
      IO.println s!"  admitsB (ORIGINAL, unchanged) = {a}"
      IO.println s!"  admitsWB (EXTENDED)           = {aw}"
      if pw == 0 then
        if a != aw then do
          IO.eprintln "  DISAGREEMENT on a design with NO plain write -- the extension changed an existing answer"
          bad := bad + 1
        else
          IO.println "  no plain write: both routes agree, as admitsWB_eq requires"
      else
        if a then do
          IO.eprintln "  the ORIGINAL checker admitted a plain write -- its contract has been weakened"
          bad := bad + 1
        else
          IO.println s!"  {pw} plain write(s): original refuses (contract intact), extended answers {aw}"
  if bad == 0 then
    IO.println "ALL CONTROLS PASS"
    return 0
  else do
    IO.eprintln s!"{bad} CONTROL(S) FAILED"
    return 1
