/-
SUPPORT DIAGNOSTIC for the DCERT2 route: the six conjuncts, and the CONCRETE
reasons when one of them is false.

`dcert2_loadcheck.lean` decodes and admits only -- it evaluates NO support
predicate, and its output must not be read as one.  `gen_probe_v2` prints the
six booleans, but a boolean does not say WHICH source or WHICH operator the
projection lacks.  This file names them.

The two non-wf failure modes are distinct and are reported separately, because
they are different obligations:

  * `sources` -- `Projection.SourceSupported` is false only for `memImg` and
    `memConst`, so a false here is always a MEMORY-BEARING source;
  * `ops` -- `Projection.OpSupported` admits exactly seventeen operators, so a
    false here names an operator `I_hw` does not implement, which is a different
    gap from memory and must not be folded into it.

Nothing here weakens a predicate, erases a memory or claims support. It reports.
-/
import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Compiler.CertIORoute   -- DCERT2 + DCERT1, routed by magic (Mathlib-free)

open Compiler Projection Projection.Hw

def loadAny (p : System.FilePath) : IO (CertIO.Format × DesignCert) := do
  let b ← IO.FS.readBinFile p
  match CertIO.admitAny b with
  | .ok fd   => pure fd
  | .error e => throw (IO.userError s!"{p}: {e}")

/-- Constructor name only: a `memConst`'s contents are megabytes and a census
wants the KIND and the count, not the payload. -/
def srcKind : SourceDesc → String
  | .input _ _            => "input"
  | .const _ _            => "const"
  | .flopQ _ _            => "flopQ"
  | .flopQAsync _ _ _ _ _ => "flopQAsync"
  | .memImg i aw dw       => s!"memImg(idx {i}, aw {aw}, dw {dw})"
  | .memConst aw dw _     => s!"memConst(aw {aw}, dw {dw})"

/-- `(key, count)` tallies, insertion-ordered. -/
def tally (xs : List String) : List (String × Nat) :=
  xs.foldl (fun acc k =>
    if acc.any (fun p => p.1 == k)
    then acc.map (fun p => if p.1 == k then (p.1, p.2 + 1) else p)
    else acc ++ [(k, 1)]) []

def render (t : List (String × Nat)) : String :=
  String.intercalate ", " (t.map (fun p => s!"{p.1} x{p.2}"))

def main (args : List String) : IO UInt32 := do
  if args.isEmpty then
    IO.eprintln "usage: dcert2_support <cert.dcert>..."
    return 2
  let mut unsupported := 0
  for p in args do
    let (fmt, D) ← loadAny p
    let sr := CertLoad.supportReport D
    IO.println s!"{p}"
    IO.println s!"  format {repr fmt}"
    IO.println s!"  shape: sources {D.sources.size} nodes {D.nodes.size} \
outputs {D.outputs.size} flops {D.flops.size} memories {D.memories.size} \
clocks {D.clocks.size}"
    IO.println s!"  support: wf {sr.wf} memFree {sr.memFree} sources {sr.sources} \
ops {sr.ops} arities {sr.arities} flopClocks {sr.flopClocks} | ALL {sr.allOK}"
    unless sr.allOK do unsupported := unsupported + 1
    unless sr.memFree do
      IO.println s!"  memFree FALSE: {D.memories.size} memory/memories -- \
{render (tally ((D.memories.toList).map (fun m => s!"aw {m.aw} dw {m.dw} nextImg {m.nextImg} clock {m.clock}")))}"
    unless sr.sources do
      let bad := D.sources.toList.filter (fun sd => !(SourceSupported sd))
      IO.println s!"  sources FALSE: {bad.length} unsupported source(s) -- \
{render (tally (bad.map srcKind))}"
    unless sr.ops do
      let bad := D.nodes.toList.filter (fun c => !(OpSupported c.op))
      IO.println s!"  ops FALSE: {bad.length} node(s) on unsupported operator(s) -- \
{render (tally (bad.map (fun c => toString (repr c.op))))}"
    unless sr.arities do
      let bad := D.nodes.toList.filter (fun c => !(decide (ArityOK c.op c.deps.toList)))
      IO.println s!"  arities FALSE: {bad.length} node(s) -- \
{render (tally (bad.map (fun c => s!"{repr c.op} with {c.deps.size} operand(s)")))}"
    unless sr.flopClocks do
      let bad := D.flops.toList.filter (fun f => !(decide (f.clock < D.clocks.size)))
      IO.println s!"  flopClocks FALSE: {bad.length} flop(s) with a clock ordinal \
outside the {D.clocks.size}-entry clock table"
  IO.println s!"{args.length} certificate(s), {unsupported} not SupportedByProjection"
  return 0
