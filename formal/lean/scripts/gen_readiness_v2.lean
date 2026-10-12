/-
READINESS for the DCERT2 route: the generated residual is actually EXECUTED.

WHY THIS IS A SEPARATE DRIVER FROM `gen_probe_v2.lean`.  That driver stops after
specialization and `Hw.checkResidual`.  What it produces is an IN-MEMORY
`Program` and it runs no residual step at all, so "generation succeeded" there is
a GENERATION-AND-CHECK result and nothing more.  A usable-simulator claim needs
the residual to run, and that is what this file does and all it adds.

WHY IT RE-SPECIALIZES.  `gen_probe_v2` serializes nothing -- the residual lives
only in that process and dies with it -- so there is NO retained `Program` to
load, and a readiness run must rebuild one.  That is a stated reason, not a
convenience: this is therefore a NEW ATTEMPT with its own cost, not a replay of
the generation benchmark for timing, and its specialize time must never be
entered as a second measurement of the generation metric.

WHAT THE EXECUTION IS.  `Projection.Hw.stepOf` on a `ProjectedSimulator`
`⟨design, residual, bound⟩`, at the bound `checkResidual` itself computed.  That
is the projection's OWN checked simulator path -- the approach-native artifact --
NOT `interpretDesign`, not the variant control interpreter, and not a generic
design interpreter standing in for the residual.  There is no fallback bound: a
checker rejection fails closed.

WHAT IT IS NOT.  No reference comparison against `interpretDesign`, no threaded
agreement trace, no control interpreter, no mutation controls: this is a
readiness check, NOT acceptance and NOT the full benchmark, and a pass here must
never be entered as one.  `total_probe` owns that and is untouched.

HONEST STATE REPORTING.  The per-cycle state and output vectors are compared,
and the driver prints how many DISTINCT ones were seen.  A run whose flop vector
never changes is reported as such rather than described as advancing state.

PROOF ACCOUNTING, unchanged from `gen_probe_v2`: support and the fragment
checker are CHECKED, the concrete `hproj` is TRUSTED because `mixDriver` does
not kernel-reduce at nontrivial fuel, and no proof flag is claimed.  Native
linking is a separate readiness mode and is not implied by an interpreted run.

CONFIGURATION GUARDS.  A positive cycle count and positive fuel are REQUIRED,
and a non-numeric argument is REFUSED rather than silently replaced by a default.
Zero cycles would execute no residual step and still report `READINESS OK`; that
is a pass earned by doing nothing, and it is now refused before the certificate
is even loaded.

Exit: 0 readiness OK / 1 checker rejection or a cycle that did not step /
2 setup, unsupported, or a refused configuration / 3 out of fuel at the
requested budget.
-/
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport
import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Compiler.CertIOFacade

open Compiler Projection Projection.Hw Projection.Runner Projection.ProtoVar

def szOf (R : Program) : Nat := (R.funs.map (fun fd => CertLoad.tsize fd.body)).foldl (·+·) 0

def loadAny (p : System.FilePath) : IO (CertIO.Format × DesignCert) := do
  let b ← IO.FS.readBinFile p
  match CertIO.admitAny b with
  | .ok fd   => pure fd
  | .error e => throw (IO.userError s!"{p}: {e}")

/-- One cycle through the CHECKED simulator, at `sim.bound`. -/
def runStep (sim : ProjectedSimulator) (D : DesignCert) (e : ClockEdges)
    (i : RuntimeInput) (s : RuntimeState) : Outcome :=
  outcomeOfSim (stepOf sim D e i s)

/-- Count of distinct entries, by the derived equality -- used so the report can
say "8 cycles, ONE state" instead of implying motion that did not happen. -/
def distinct [BEq α] (xs : List α) : Nat :=
  (xs.foldl (fun acc x => if acc.any (· == x) then acc else x :: acc) []).length

def main (args : List String) : IO UInt32 := do
  match args with
  | path :: a :: b :: rest => do
      -- CONFIGURATION IS VALIDATED BEFORE ANY WORK.  Three separate boundary
      -- defects were possible here and all three are refused rather than
      -- papered over:
      --   * a malformed number used to fall back to a default via `getD`, so a
      --     typo silently ran a DIFFERENT budget than the one on the command
      --     line and reported it as the requested one;
      --   * `cycles = 0` made `List.range 0` execute NOTHING, leave `bad` at
      --     `false`, and return `READINESS OK` -- a pass earned by doing no
      --     work at all;
      --   * zero fuel is not a budget.
      -- The checks run BEFORE the certificate is loaded and before anything is
      -- generated, so a malformed invocation costs nothing.
      let some sf := a.toNat? | do
        IO.eprintln s!"REFUSING: stepFuel {repr a} is not a natural number"; return 2
      let some wf := b.toNat? | do
        IO.eprintln s!"REFUSING: workFuel {repr b} is not a natural number"; return 2
      let cyclesArg := rest.head?.getD "8"
      let some cycles := cyclesArg.toNat? | do
        IO.eprintln s!"REFUSING: cycles {repr cyclesArg} is not a natural number"; return 2
      if sf == 0 || wf == 0 then
        IO.eprintln s!"REFUSING: fuel must be positive, got {sf}/{wf}"
        return 2
      if cycles == 0 then
        IO.eprintln "REFUSING: cycles must be POSITIVE. Zero cycles executes no residual step, so a `READINESS OK` would be a pass earned by doing nothing."
        return 2
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
        IO.eprintln "  NOT SupportedByProjection -- readiness refused"
        IO.println s!"  ms: load {t1 - t0}  support {t2 - t1}"
        IO.println "  RESULT: READINESS REFUSED (unsupported)"
        return 2
      IO.println s!"  backend: PROVED mixDriver + hwAPVarT (TOTAL variant), fuel {sf}/{wf}"
      IO.println "  RE-SPECIALIZING: gen_probe_v2 serializes nothing, so no retained \
Program exists to load. This is a NEW attempt, not a timing replay."
      (← IO.getStdout).flush
      let t3 ← IO.monoMsNow
      let res ← IO.lazyPure (fun _ => mixDriver sf wf hwAPVarT [encDesign D])
      match res with
      | .error .outOfFuel => do
          let t5 ← IO.monoMsNow
          IO.eprintln s!"  projection OUT OF FUEL at {sf}/{wf} -- a failure at THIS budget"
          IO.println s!"  ms: load {t1 - t0}  support {t2 - t1}  specialize {t5 - t3}"
          IO.println "  RESULT: READINESS OUT OF FUEL"
          return 3
      | .error e => do
          let t5 ← IO.monoMsNow
          IO.eprintln s!"  projection FAILED ({repr e})"
          IO.println s!"  ms: load {t1 - t0}  support {t2 - t1}  specialize {t5 - t3}"
          IO.println "  RESULT: READINESS GENERATION FAILED"
          return 2
      | .ok R => do
          let t4 ← IO.monoMsNow
          match Hw.checkResidual R with
          | none => do
              let t5 ← IO.monoMsNow
              IO.eprintln "  checkResidual REJECTED -- failing closed; no execution at a fallback bound"
              IO.println s!"  ms: load {t1 - t0}  support {t2 - t1}  specialize {t4 - t3}  check {t5 - t4}"
              IO.println "  RESULT: READINESS CHECK REJECTED"
              return 1
          | some bound => do
            let t5 ← IO.monoMsNow
            let sim : ProjectedSimulator := ⟨D, R, bound⟩
            IO.println s!"  residual {szOf R} terms, checker bound {bound} \
(height; proved sufficient, not a minimum)"
            IO.println s!"  EXECUTING {cycles} cycle(s) of Hw.stepOf at the checked bound \
-- the projection's own simulator, no reference run and no control interpreter."
            (← IO.getStdout).flush
            let t6 ← IO.monoMsNow
            let mut st := mkStateFor D 0
            let mut states : List RuntimeState := [st]
            let mut outs : List (Array BV) := []
            let mut tags : List String := []
            let mut bad := false
            for k in List.range cycles do
              let e := edgeSchedule D k
              let i := mkInputFor D k
              let o := runStep sim D e i st
              tags := o.tag :: tags
              match o with
              | .ok r => do
                  outs := r.outputs :: outs
                  st := r.nextState
                  states := st :: states
              | _ => bad := true
            let t7 ← IO.monoMsNow
            IO.println s!"  ms: load {t1 - t0}  support {t2 - t1}  specialize {t4 - t3}  \
check {t5 - t4}  execute {t7 - t6}"
            IO.println s!"  step outcomes: {String.intercalate "," tags.reverse}"
            IO.println s!"  distinct flop-state vectors {distinct (states.map (·.flops))} \
of {states.length} (1 means the state never changed)"
            IO.println s!"  distinct output vectors {distinct outs} of {outs.length}"
            if bad then
              IO.eprintln "  at least one cycle did not step -- failing closed"
              IO.println "  RESULT: READINESS FAILED (a cycle did not step)"
              return 1
            IO.println "  RESULT: READINESS OK (residual executed at the checked bound; \
NOT acceptance, NOT a reference comparison, NOT native linking)"
            return 0
  | _ => do
      IO.eprintln "usage: gen_readiness_v2 <cert.dcert> <stepFuel> <workFuel> [cycles]"
      return 2
