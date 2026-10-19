/-
FUNCTIONAL CHECK of the DCERT2 loader prerequisite, in isolation.

This imports ONLY `Compiler.CertIOFacade` -- the magic router over D4's DCERT2
codec and FP's DCERT1 codec -- and loads a real certificate emitted by the
current C++ pass.  It reports which format was detected, which policy admitted
it, and the clock/reset metadata DCERT1 cannot carry, so that "the loader reads
DCERT2" is an observation rather than a claim.

It deliberately does NOT import the Projection chain: that chain's native route
does not build the DCERT2 codec (it builds `CertIOV1`, because `Compiler.CertIO`
transitively `import Mathlib` through `CompileDesign`), and separating the two
is the point -- this file answers "can the DCERT2 bytes be admitted at all",
not "can the projection consume them".
-/
import LeanSemanticPrimitives.Compiler.CertIORoute   -- DCERT2 + DCERT1, routed by magic (Mathlib-free)

open Compiler

def main (args : List String) : IO UInt32 := do
  match args with
  | [] => do IO.eprintln "usage: LoadCheck <cert.dcert>..."; return 2
  | paths => do
      let mut bad := 0
      for p in paths do
        let b ← IO.FS.readBinFile p
        IO.println s!"{p}  ({b.size} bytes)"
        match CertIO.detect b with
        | none => do IO.println "  detect: NEITHER DCERT2 nor DCERT1"; bad := bad + 1
        | some f => IO.println s!"  detect: {repr f}"
        match CertIO.admitAny b with
        | .error e => do IO.println s!"  admitAny: REFUSED -- {e}"; bad := bad + 1
        | .ok (f, D) => do
            IO.println s!"  admitAny: ok under {repr f}'s own policy"
            IO.println s!"  shape: sources {D.sources.size} nodes {D.nodes.size} \
outputs {D.outputs.size} flops {D.flops.size} memories {D.memories.size} \
clocks {D.clocks.size}"
            -- the three fields DCERT1 has no bytes for
            let asyncs := D.flops.foldl (fun n fd => if fd.asyncReset then n+1 else n) 0
            let clkIds := D.flops.foldl (fun (s : List Nat) fd =>
                            if s.contains fd.clock then s else fd.clock :: s) []
            let memClks := D.memories.foldl (fun (s : List Nat) md =>
                            if s.contains md.clock then s else md.clock :: s) []
            IO.println s!"  DCERT2-only metadata: distinct FlopDesc.clock {clkIds.length} \
FlopDesc.asyncReset present on {asyncs} flop(s) distinct MemoryDesc.clock {memClks.length}"
      if bad == 0 then
        IO.println "ALL CERTIFICATES ADMITTED"
        return 0
      else
        IO.println s!"{bad} failure(s)"
        return 1
