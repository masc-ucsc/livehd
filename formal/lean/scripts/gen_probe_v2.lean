/-
GENERATION-ONLY runner, DCERT2-capable.  Same driver as `gen_probe.lean` in
every respect except the loader, and the loader is the whole point.

WHY THIS FILE EXISTS.  `gen_probe.lean` loads through `CertIO.V1.loadCert`, which
is FP's **DCERT1** codec: no clocks section, no `FlopDesc.clock`, no
`FlopDesc.asyncReset`, no `MemoryDesc.clock`.  The C++ pass now emits **DCERT2**,
so every certificate produced by the current `pass.lean` is unreadable by that
driver -- the FP column's missing functional prerequisite.  This driver routes
through `Compiler.CertIO.admitAny`, which detects the magic and then applies
**each format's own admission policy**:

  * DCERT2 -> `admitV2` = `readCertChecked` = parse, then `certWfError`;
  * DCERT1 -> `admitV1` = FP's own: parse, then the `asyncOK` transport
    constraint.

NEITHER POLICY IS WIDENED OR NARROWED, and no third policy is defined here.
`magics_are_exclusive` proves the routing is unambiguous, so a file cannot be
admitted under the wrong contract.  A DCERT2 certificate therefore arrives with
its clock table, per-flop clock and async-reset metadata, and per-memory clock
INTACT -- which is exactly what `CertLoad.supportReport`'s `flopClocks` conjunct
is about, so the support predicate is evaluated on real metadata rather than on
a defaulted one-clock table.

WHAT IS UNCHANGED FROM `gen_probe.lean`, deliberately, so the two are
comparable: the measured interval (load + support + specialize + check), `szOf`
reported AFTER it and never added to generation (it measures 0 ms even on
2,213,885 terms, so that exclusion is measurement hygiene, not a cost), the backend
(`mixDriver sf wf hwAPVarT`), the exit codes, and the output lines.  The one
added line names the detected format, because a generation result that does not
say which codec admitted the bytes is not reproducible.

WHAT THIS DOES NOT FIX.  Support is still SIX conjuncts including `memFree`:
a memory-bearing design is still refused as unsupported, and that refusal is an
inherited semantics / runtime-relation / adequacy gap in the projection, NOT a
loader defect.  This file does not remove `memFree`, does not erase memory from
a design and does not claim a design is supported that is not.

PROOF ACCOUNTING, unchanged: this run CHECKS support and the fragment checker
and TRUSTS the concrete `hproj` -- `mixDriver` does not kernel-reduce at
nontrivial fuel.  Nothing here discharges a universal premise and no proof flag
is claimed.  Success is `GENERATION OK`, which is WEAKER than `total_probe`'s
benchmark pass and must never be entered as one.

Exit: 0 generation and checking OK / 1 checker rejection / 2 setup or
unsupported / 3 out of fuel at the requested budget.
-/
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport
import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Compiler.CertIOFacade   -- DCERT2 + DCERT1, routed by magic

open Compiler Projection Projection.Hw Projection.Runner Projection.ProtoVar

def szOf (R : Program) : Nat := (R.funs.map (fun fd => CertLoad.tsize fd.body)).foldl (·+·) 0

/-- Load by magic, under the loaded format's own admission policy.  Returns the
format alongside the design so the run can report which codec admitted it. -/
def loadAny (p : System.FilePath) : IO (CertIO.Format × DesignCert) := do
  let b ← IO.FS.readBinFile p
  match CertIO.admitAny b with
  | .ok fd   => pure fd
  | .error e => throw (IO.userError s!"{p}: {e}")

def main (args : List String) : IO UInt32 := do
  match args with
  | path :: a :: b :: _ => do
      let sf := (a.toNat?).getD 20000
      let wf := (b.toNat?).getD 200
      let t0 ← IO.monoMsNow
      let (fmt, D) ← loadAny path
      let t1 ← IO.monoMsNow
      let sr := CertLoad.supportReport D
      let okAll := sr.wf && sr.memFree && sr.sources && sr.ops && sr.arities && sr.flopClocks
      let t2 ← IO.monoMsNow
      IO.println s!"{path}"
      IO.println s!"  format {repr fmt} (routed by magic; admitted under that format's own policy)"
      IO.println s!"  sources {D.sources.size} nodes {D.nodes.size} \
flops {D.flops.size} outputs {D.outputs.size} clocks {D.clocks.size} \
memories {D.memories.size}"
      IO.println s!"  support: wf {sr.wf} memFree {sr.memFree} sources {sr.sources} \
ops {sr.ops} arities {sr.arities} flopClocks {sr.flopClocks} | ALL {okAll}"
      unless okAll do
        IO.eprintln "  NOT SupportedByProjection -- generation refused"
        unless sr.memFree do
          IO.eprintln "  (memFree is false: a memory-bearing design. That is the \
inherited projection semantics/runtime-relation/adequacy gap, not a loader defect.)"
        IO.println s!"  GEN ms: load {t1 - t0}  support {t2 - t1}  TOTAL {t2 - t0}"
        IO.println "  RESULT: GENERATION REFUSED (unsupported)"
        return 2
      IO.println s!"  backend: PROVED mixDriver + hwAPVarT (TOTAL variant), fuel {sf}/{wf}"
      IO.println "  GENERATION ONLY: no reference stimuli, no trace, no control interpreter."
      IO.println "  artifact: an IN-MEMORY Program; nothing is serialized and no source is written."
      (← IO.getStdout).flush
      let t3 ← IO.monoMsNow
      let res ← IO.lazyPure (fun _ => mixDriver sf wf hwAPVarT [encDesign D])
      let t4 ← IO.monoMsNow
      match res with
      | .error .outOfFuel => do
          let t5 ← IO.monoMsNow
          IO.eprintln s!"  projection OUT OF FUEL at {sf}/{wf} -- a failure at THIS budget"
          IO.println s!"  GEN ms: load {t1 - t0}  support {t2 - t1}  specialize {t5 - t3}  TOTAL {t5 - t0}"
          IO.println "  RESULT: GENERATION OUT OF FUEL"
          return 3
      | .error e => do
          let t5 ← IO.monoMsNow
          IO.eprintln s!"  projection FAILED ({repr e})"
          IO.println s!"  GEN ms: load {t1 - t0}  support {t2 - t1}  specialize {t5 - t3}  TOTAL {t5 - t0}"
          IO.println "  RESULT: GENERATION FAILED"
          return 2
      | .ok R => do
          let t5 ← IO.monoMsNow
          let chk := Hw.checkResidual R
          let accepted := chk.isSome
          let bound := chk.getD 0
          let t6 ← IO.monoMsNow
          -- END OF THE MEASURED GENERATION INTERVAL.  Everything below is
          -- reporting and is excluded from it.
          unless accepted do
            IO.eprintln "  checkResidual REJECTED -- failing closed"
            IO.println s!"  GEN ms: load {t1 - t0}  support {t2 - t1}  \
specialize-whnf {t4 - t3}  specialize-force {t5 - t4}  check {t6 - t5}  TOTAL {t6 - t0}"
            IO.println "  RESULT: GENERATION CHECK REJECTED"
            return 1
          IO.println s!"  GEN ms: load {t1 - t0}  support {t2 - t1}  \
specialize-whnf {t4 - t3}  specialize-force {t5 - t4}  check {t6 - t5}  TOTAL {t6 - t0}"
          IO.println s!"  checker bound {bound} (height; proved sufficient, not a minimum)"
          let t7 ← IO.monoMsNow
          let terms := szOf R
          let t8 ← IO.monoMsNow
          IO.println s!"  EXCLUDED FROM GENERATION -- profiling: szOf {terms} terms in {t8 - t7} ms"
          IO.println "  RESULT: GENERATION OK (NOT a full benchmark pass: no stimuli, no trace, no control)"
          return 0
  | _ => do
      IO.eprintln "usage: gen_probe_v2 <cert.dcert> <stepFuel> <workFuel>"
      return 2
