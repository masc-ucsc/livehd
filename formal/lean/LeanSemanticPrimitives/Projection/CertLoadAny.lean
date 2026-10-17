/-
# One loader for both certificate formats

`CertLoad` is FP's production runner and reads DCERT1 only, through
`Compiler.CertIO.V1`.  Master's C++ pass emits DCERT2 only.  So until this
module, no Projection runner could read a certificate master itself produces:
DCERT2 reached the projection only through driver scripts that import the
Mathlib-side `CertIOFacade`.

This module routes by magic through `Compiler.CertIORoute` (Mathlib-free) and
keeps each format's own policy:

  * DCERT1 -- exactly FP's behaviour, nothing tightened or loosened.
    `loadCertAny` parses with `V1.parseCert` and admits nothing, as
    `V1.loadCert` always did for the probes; `runOneAny` DELEGATES to
    `CertLoad.runOne`, which runs FP's `asyncOK` transport check.
  * DCERT2 -- `CertIO.readCertChecked`: parse, then `certWfError` (at least one
    clock, every flop and memory clock ordinal declared, every output slot in
    range).  The format carries `FlopDesc.asyncReset`, so FP's `asyncOK`
    transport constraint -- which exists only because DCERT1 cannot -- does not
    apply.

`CertLoad.lean` itself is untouched, so its two audited theorems
(`slotsInRange_of_bool`, `supported_of_report`) are unchanged.
-/

import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Compiler.CertIORoute

namespace Projection
namespace CertLoadAny

open Compiler Projection.Hw Projection.CertLoad

/-- Load either format and SAY WHICH.  A DCERT1 file parses to a one-clock
record indistinguishable from a genuinely one-clock DCERT2 one, so the caller
is told the format instead of inferring it. -/
def loadAny (p : System.FilePath) : IO (CertIO.Format × DesignCert) := do
  let b ← IO.FS.readBinFile p
  match CertIO.detect b with
  | some .v1 =>
      match CertIO.V1.parseCert b with
      | .ok D    => pure (.v1, D)
      | .error e => throw (IO.userError s!"{p}: {e}")
  | some .v2 =>
      match CertIO.readCertChecked b with
      | .ok D    => pure (.v2, D)
      | .error e => throw (IO.userError s!"{p}: {e}")
  | none =>
      throw (IO.userError s!"{p}: unrecognised certificate: expected DCERT2 or DCERT1")

/-- Drop-in for `CertIO.V1.loadCert` in the probes.  On a DCERT1 file it is
exactly that function (same parse, same error text); on DCERT2 it admits
through `readCertChecked`. -/
def loadCertAny (p : System.FilePath) : IO DesignCert :=
  return (← loadAny p).2

/-- `runOne` for a DCERT2 certificate: the same five steps and the same exit
codes, with `readCertChecked` as the admission and the codec's (byte-identical)
`stimulus`/`digest`.  Exit codes: 1 admission, 3 support, 4 specialization or
fragment check, 5/6 one cycle refused/mismatch, 7/8 trace refused/mismatch. -/
def runV2 (path : String) (bytes : ByteArray) (cycles : Nat) (seed : Nat) : IO UInt32 := do
  say s!"=== {path}  [DCERT2]"
  let t1 ← ms
  let D ← match CertIO.readCertChecked bytes with
          | .ok D    => pure D
          | .error e => do say s!"  ADMISSION FAILED: {e}"; return 1
  let t2 ← ms
  say s!"  bytes {bytes.size}  parse+admit {t2 - t1} ms"
  say s!"  sources {D.sources.size}  nodes {D.nodes.size}  outputs {D.outputs.size} \
flops {D.flops.size}  memories {D.memories.size}  clocks {D.clocks.size}"
  let t3 ← ms
  let r := supportReport D
  let t4 ← ms
  say s!"  support: wf {r.wf}  memFree {r.memFree}  sources {r.sources}  ops {r.ops} \
arities {r.arities}  flopClocks {r.flopClocks}   ({t4 - t3} ms)"
  if !r.allOK then
    say "  REFUSED by the scalar route: see the per-field report above \
(a memory-bearing design belongs to the memory route, dcert2_memroute)."
    return 3
  let t5 ← ms
  let sim ← match mkSim D with
            | some sim => pure sim
            | none     => do
                say "  SPECIALIZATION or FRAGMENT CHECK FAILED"
                return 4
  let t6 ← ms
  say s!"  specialize+check {t6 - t5} ms   residual funs {sim.prog.funs.length} \
size {(sim.prog.funs.map (fun fd => tsize fd.body)).foldl (·+·) 0}  bound {sim.bound}"
  say s!"  surviving caseT tags {tagsIn sim.prog}   (tagState = {tagState})"
  let (inp, st) := CertIO.stimulus D seed
  let e := allEdges D
  let t7 ← ms
  let projected := stepOf sim D e inp st
  let t8 ← ms
  let direct := interpretDesign D e inp st
  let t9 ← ms
  match projected with
  | .error _ => do say "  ONE CYCLE: projected path refused"; return 5
  | .ok pr   =>
      say s!"  one cycle: projected {t8 - t7} ms  direct {t9 - t8} ms"
      if CertIO.digest pr != CertIO.digest direct then
        say s!"  MISMATCH\n    projected {CertIO.digest pr}\n    direct    {CertIO.digest direct}"
        return 6
      say s!"  one cycle AGREES: {CertIO.digest pr}"
  let stim : List Stim :=
    (List.range cycles).map (fun k => (e, (CertIO.stimulus D (seed + k)).1))
  let t10 ← ms
  let ptrace := stepTrace (stepOf sim) D st stim
  let t11 ← ms
  let dtrace := refTrace D st stim
  let t12 ← ms
  match ptrace with
  | .error _ => do say "  TRACE: projected path refused"; return 7
  | .ok rs   =>
      say s!"  trace {cycles} cycles: projected {t11 - t10} ms  direct {t12 - t11} ms"
      if rs.map CertIO.digest != dtrace.map CertIO.digest then
        say "  TRACE MISMATCH"; return 8
      say s!"  trace AGREES ({rs.length} results)"
  say s!"  peak RSS {← peakRSS}"
  say "  NOTE: agreement here is EXECUTION EVIDENCE about this file, not a \
kernel proof about it; `SimWF` is still undischarged for a loaded certificate."
  return 0

/-- Route one certificate: DCERT1 to FP's `runOne` unchanged, DCERT2 to `runV2`. -/
def runOneAny (path : String) (cycles : Nat) (seed : Nat) : IO UInt32 := do
  let bytes ← IO.FS.readBinFile path
  match CertIO.detect bytes with
  | some .v1 => runOne path cycles seed
  | some .v2 => runV2 path bytes cycles seed
  | none     => do
      say s!"=== {path}"
      say "  PARSE FAILED: unrecognised certificate: expected DCERT2 or DCERT1"
      return 1

/-- The same three shared fixtures as `CertLoad.emitFixtures`, written as
DCERT2, so the DCERT2 path can be smoke-tested on designs whose answer is
already known from the DCERT1 path. -/
def emitFixturesV2 (dir : String) : IO Unit := do
  IO.FS.createDirAll dir
  for (name, D) in [("tiny", Projection.Acceptance.tinyD),
                    ("seq",  Projection.Acceptance.seqD),
                    ("wide", wideD)] do
    let p := s!"{dir}/{name}.v2.dcert"
    IO.FS.writeBinFile p (CertIO.renderCert D)
    say s!"wrote {p}"

def main (args : List String) : IO UInt32 := do
  match args with
  | "--emit" :: dir :: _ => do emitFixtures dir; return 0
  | "--emit2" :: dir :: _ => do emitFixturesV2 dir; return 0
  | path :: rest =>
      let cycles := (rest.head?.bind String.toNat?).getD 4
      let seed   := ((rest.drop 1).head?.bind String.toNat?).getD 1
      runOneAny path cycles seed
  | [] => do
      say "usage: cert_runner_any <file.dcert> [cycles] [seed]   (DCERT1 or DCERT2)"
      return 64

end CertLoadAny
end Projection
