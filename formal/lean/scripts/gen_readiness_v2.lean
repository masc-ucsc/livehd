/-
READINESS for the DCERT2 route: the generated residual is actually EXECUTED.

WHY THIS IS A SEPARATE DRIVER FROM `gen_probe_v2.lean`.  That driver stops after
specialization and `Hw.checkResidual`.  What it produces is an IN-MEMORY
`Program` and it runs no residual step at all, so "generation succeeded" there is
a GENERATION-AND-CHECK result and nothing more.  A usable-simulator claim needs
the residual to run, and that is what this file does and all it adds.

LOAD OR RE-SPECIALIZE.  With `load:<file>` the residual `gen_probe_v2 save:`
wrote (Projection/ResidualIO) is decoded -- its header must name this
certificate, backend and fuel -- and `checkResidual` is re-run on it; the timed
phase is then `residual-load`.  Without it the design is re-specialized: a NEW
attempt with its own cost, whose specialize time must never be entered as a
second measurement of the generation metric.

WHAT THE EXECUTION IS.  `Projection.Hw.stepOf` on a `ProjectedSimulator`
`⟨design, residual, bound⟩`, at the bound `checkResidual` itself computed.  That
is the projection's OWN checked simulator path -- the approach-native artifact --
NOT `interpretDesign`, not the variant control interpreter, and not a generic
design interpreter standing in for the residual.  There is no fallback bound: a
checker rejection fails closed.

THE REFERENCE COMPARISON (default ON; `nocompare` turns it off).  Every
executed cycle is also run through `interpretDesign` -- the shared reference
semantics, NOT the specializer and NOT the residual -- from the SAME pre-state
(the residual's own threaded state), edges and inputs, and the residual's result
must equal it: every output and the whole next state, by `encResult`.  A
disagreement fails closed (exit 1).  The stimulus is the deterministic generator
`mkInputFor D k` / `scheduleEdges` (edges:auto -- independent domains when the
certificate declares more than one clock) from `mkStateFor D 0`; its exact sequence
is RECORDED as an fnv1a digest over the edges and input values of every cycle.
The reference runs are timed separately and are not part of `execute`.

WHAT IT IS NOT.  No control interpreter and no mutation controls, and the
stimuli are generated, not RTL-derived: a pass is the C6 bar (proved
specializer, checker-accepted residual, N cycles executed, every cycle equal to
`interpretDesign`), NOT a proof about the design for all inputs.

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

Exit: 0 readiness OK / 1 checker rejection, a cycle that did not step, or a
reference disagreement /
2 setup, unsupported, or a refused configuration / 3 out of fuel at the
requested budget.
-/
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport
import LeanSemanticPrimitives.Projection.CertLoad
import LeanSemanticPrimitives.Compiler.CertIORoute   -- DCERT2 + DCERT1, routed by magic (Mathlib-free)
import LeanSemanticPrimitives.Compiler.RunAdmission   -- C3 edges / C4 reset polarity
import LeanSemanticPrimitives.Projection.ResidualIO   -- load:<file> (B10)

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
      if let some rc := (← RunAdmission.refuseDesign? D) then return rc
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
      -- optional `load:<file>`: a residual gen_probe_v2 saved (Projection/ResidualIO).
      -- Its header must name THIS certificate (fnv1a), backend and fuel, or the
      -- file is refused; `checkResidual` is re-run on the decoded program below.
      let loadFrom := rest.findSome? (fun t => (t.dropPrefix? "load:").map (·.toString))
      let compare := !(rest.contains "nocompare")
      -- `edges:<auto|all|indep>`: which clocks fire on each cycle (RunnerSupport
      -- `scheduleEdges`). `auto` -- the default -- keeps the one-clock schedule
      -- for a one-clock design and fires domains INDEPENDENTLY when the
      -- certificate declares more than one clock. An unknown mode is refused.
      let edgeTok := rest.findSome? (fun t => (t.dropPrefix? "edges:").map (·.toString))
      let some edgeMode := (edgeTok.map EdgeMode.parse?).getD (some .auto) | do
        IO.eprintln s!"REFUSING: edges:{edgeTok.getD ""} is not one of auto | all | indep"; return 2
      let mut loaded : Option Program := none
      let t3 ← IO.monoMsNow
      if let some f := loadFrom then
        let (h, R) ← try ResidualIO.readResidual f
          catch e => do
            IO.eprintln s!"  REFUSING the residual file: {e}"
            IO.println "  RESULT: READINESS REFUSED (residual file)"
            return (2 : UInt32)
        let digest := toString (ResidualIO.fnv1a (← IO.FS.readBinFile path))
        if h.certDigest != digest || h.backend != "hwAPVarT" || h.stepFuel != sf || h.workFuel != wf then
          IO.eprintln s!"  REFUSING the residual file {f}: its header (cert {h.certDigest}, \
{h.backend}, fuel {h.stepFuel}/{h.workFuel}) does not match this run (cert {digest}, \
hwAPVarT, fuel {sf}/{wf})"
          IO.println "  RESULT: READINESS REFUSED (residual header mismatch)"
          return 2
        IO.println s!"  LOADED residual {f} (producer {h.producer}, cert fnv1a {digest}): \
NO re-specialization; checkResidual is re-run on the decoded program"
        loaded := some R
      else
        IO.println "  RE-SPECIALIZING: no residual file was given (load:<file>), so this \
is a NEW specialization, not a timing replay."
      (← IO.getStdout).flush
      -- the timed phase is a residual LOAD when a file was given, else a specialization
      let phase := if loaded.isSome then "residual-load" else "specialize"
      let res ← match loaded with
        | some R => pure (.ok R)
        | none   => IO.lazyPure (fun _ => mixDriver sf wf hwAPVarT [encDesign D])
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
              IO.println s!"  ms: load {t1 - t0}  support {t2 - t1}  {phase} {t4 - t3}  check {t5 - t4}"
              IO.println "  RESULT: READINESS CHECK REJECTED"
              return 1
          | some bound => do
            let t5 ← IO.monoMsNow
            let sim : ProjectedSimulator := ⟨D, R, bound⟩
            IO.println s!"  residual {szOf R} terms, checker bound {bound} \
(height; proved sufficient, not a minimum)"
            IO.println s!"  EXECUTING {cycles} cycle(s) of Hw.stepOf at the checked bound \
-- the projection's own simulator; reference comparison against interpretDesign \
{if compare then "ON (every cycle: outputs and next state)" else "OFF (nocompare)"}"
            IO.println s!"  stimulus: generated -- inputs mkInputFor D k, edges {edgeMode.describe D}, \
initial state mkStateFor D 0"
            (← IO.getStdout).flush
            let t6 ← IO.monoMsNow
            let mut st := mkStateFor D 0
            let mut states : List RuntimeState := [st]
            let mut outs : List (Array BV) := []
            let mut tags : List String := []
            let mut bad := false
            let mut tRef : Nat := 0
            let mut tStep : Nat := 0
            let mut firstDisagree : Option Nat := none
            let mut compared : Nat := 0
            let mut stimText : String := ""
            for k in List.range cycles do
              let e := scheduleEdges edgeMode D k
              if let some rc := (← RunAdmission.refuseEdges? D k e) then return rc
              let i := mkInputFor D k
              stimText := stimText ++ s!"{k}:" ++
                String.intercalate "," (e.toList.map (fun b => if b then "1" else "0")) ++ "|" ++
                String.intercalate "," (i.toList.map (fun v => s!"{v.width}'{v.value}")) ++ ";"
              let s0 ← IO.monoMsNow
              let o ← IO.lazyPure (fun _ => runStep sim D e i st)
              let s1 ← IO.monoMsNow
              tStep := tStep + (s1 - s0)
              tags := o.tag :: tags
              if compare then
                let r0 ← IO.monoMsNow
                let agrees ← IO.lazyPure (fun _ => Outcome.agree o (Outcome.ok (interpretDesign D e i st)))
                let r1 ← IO.monoMsNow
                tRef := tRef + (r1 - r0)
                compared := compared + 1
                if !agrees && firstDisagree.isNone then firstDisagree := some k
              match o with
              | .ok r => do
                  outs := r.outputs :: outs
                  st := r.nextState
                  states := st :: states
              | _ => bad := true
            let t7 ← IO.monoMsNow
            IO.println s!"  ms: load {t1 - t0}  support {t2 - t1}  {phase} {t4 - t3}  \
check {t5 - t4}  execute {tStep}  reference {tRef}  loop {t7 - t6}"
            IO.println s!"  stimulus digest fnv1a {ResidualIO.fnv1a stimText.toUTF8} over {cycles} cycle(s)"
            IO.println s!"  step outcomes: {String.intercalate "," tags.reverse}"
            let cmpLabel :=
              if !compare then "DISABLED by nocompare -- nothing was compared"
              else match firstDisagree with
                | some k => s!"FAILED at cycle {k}"
                | none   => s!"COMPLETE ({compared} cycle(s): every output and the whole next state equal interpretDesign)"
            IO.println s!"  reference comparison: {cmpLabel}"
            IO.println s!"  distinct flop-state vectors {distinct (states.map (·.flops))} \
of {states.length} (1 means the state never changed)"
            IO.println s!"  distinct output vectors {distinct outs} of {outs.length}"
            if bad then
              IO.eprintln "  at least one cycle did not step -- failing closed"
              IO.println "  RESULT: READINESS FAILED (a cycle did not step)"
              return 1
            if let some k := firstDisagree then
              IO.eprintln s!"  the residual DISAGREED with interpretDesign at cycle {k} -- failing closed"
              IO.println "  RESULT: READINESS FAILED (reference disagreement)"
              return 1
            if compare then
              IO.println "  RESULT: READINESS OK (residual executed at the checked bound; \
reference comparison COMPLETE against interpretDesign)"
            else
              IO.println "  RESULT: READINESS OK (residual executed at the checked bound; \
reference comparison DISABLED -- does NOT earn comparison credit)"
            return 0
  | _ => do
      IO.eprintln "usage: gen_readiness_v2 <cert.dcert> <stepFuel> <workFuel> [cycles] [load:<residual.fpres>] [nocompare] [edges:auto|all|indep]"
      return 2
