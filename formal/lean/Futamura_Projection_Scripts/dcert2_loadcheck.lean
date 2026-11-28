/-
FUNCTIONAL CHECK of the certificate loader, in isolation.

Admits each certificate with `CertIO.readCertChecked` (DCERT4: parse, then `certWfError`), the one
admission every driver uses, and prints the clock and reset metadata DCERT1 could not carry, so
that "the loader reads it" is an observation rather than a claim. Written for DCERT2 against the
DCERT1/DCERT2 router (`CertIORoute`); DCERT1 is now refused by every driver.

It deliberately does NOT import the Projection chain: this file answers "can the bytes be admitted
at all", not "can the projection consume them".
-/
import LeanSemanticPrimitives.Compiler.CertIOCodec   -- readCertChecked, the one admission (Mathlib-free)

open Compiler

def main (args : List String) : IO UInt32 := do
  match args with
  | [] => do IO.eprintln "usage: LoadCheck <cert.dcert>..."; return 2
  | paths => do
      let mut bad := 0
      for p in paths do
        let b ← IO.FS.readBinFile p
        IO.println s!"{p}  ({b.size} bytes)"
        match CertIO.readCertChecked b with
        | .error e => do IO.println s!"  readCertChecked: REFUSED -- {e}"; bad := bad + 1
        | .ok D => do
            IO.println "  readCertChecked: admitted"
            IO.println s!"  shape: sources {D.sources.size} nodes {D.nodes.size} \
outputs {D.outputs.size} flops {D.flops.size} memories {D.memories.size} \
clocks {D.clocks.size}"
            -- the three fields DCERT1 has no bytes for
            let asyncs := D.flops.foldl (fun n fd => if fd.asyncReset then n+1 else n) 0
            let clkIds := D.flops.foldl (fun (s : List Nat) fd =>
                            if s.contains fd.clock then s else fd.clock :: s) []
            let memClks := D.memories.foldl (fun (s : List Nat) md =>
                            if s.contains md.clock then s else md.clock :: s) []
            IO.println s!"  clock metadata: distinct FlopDesc.clock {clkIds.length} \
FlopDesc.asyncReset present on {asyncs} flop(s) distinct MemoryDesc.clock {memClks.length}"
      if bad == 0 then
        IO.println "ALL CERTIFICATES ADMITTED"
        return 0
      else
        IO.println s!"{bad} failure(s)"
        return 1
