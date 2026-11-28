/-
# What a certificate file is, as the drivers load it

  lean --run dcert_summary.lean <file.dcert>...

Loads each file with `Projection.CertLoadAny.admitFile` (= `CertIO.loadCert`: read, then
`readCertChecked`, the one admission every driver and proof tool uses) and prints one line:

  <file>: admitted: <s> sources, <n> nodes, <o> outputs; clocks <c>; flop clocks [..];
    memories [aw/dw/depth on clock k with m commits, ..]

(one line; wrapped here). A refused file prints `REFUSED <file>: <why>`. Exits 0 when every file
was admitted, 1 otherwise. A test compares the line with the shape it expects
(`scanner_dcert_test.sh`).
-/
import LeanSemanticPrimitives.Projection.CertLoadAny

open Compiler

def summary (D : DesignCert) : String :=
  let mems := D.memories.toList.map fun m =>
    s!"{m.aw}/{m.dw}/{m.depth} on clock {m.clock} with {m.commits.size} commits"
  s!"{D.sources.size} sources, {D.nodes.size} nodes, {D.outputs.size} outputs; " ++
  s!"clocks {D.clocks.size}; flop clocks {D.flops.toList.map (·.clock)}; memories {mems}"

def main (args : List String) : IO UInt32 := do
  if args.isEmpty then
    IO.eprintln "usage: dcert_summary <file.dcert>..."
    return 2
  let mut bad := 0
  for f in args do
    try
      let D ← Projection.CertLoadAny.admitFile f
      IO.println s!"{f}: admitted: {summary D}"
    catch e =>
      IO.println s!"REFUSED {e}"
      bad := bad + 1
  return if bad == 0 then 0 else 1
