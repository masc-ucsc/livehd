/-
PRODUCTION DCERT2 **MEMORY ROUTE**: support, generation, check, readiness.

This is the callable driver for the checked memory route, beside the scalar
ones.  `dcert2_support.lean` reports `SupportedByProjection`'s six conjuncts and
`gen_readiness_v2.lean` executes the SCALAR residual; neither is changed, and a
memory-bearing design is still refused by both.  This driver answers the other
question: is the design admitted by the MEMORY route, and does its residual run?

WHAT IT DOES, in the order the phases are reported.

  1. PARSE.  `CertIO.admitAny`, under the format's own policy.  Every piece of
     DCERT2 metadata the parser produced is retained and reported: clock table,
     per-flop clock ordinal / reset pin / polarity / async flag / enable, and
     per-memory aw/dw/nextImg/clock.  Nothing is dropped or defaulted here.
  2. SUPPORT.  The SCALAR report, verbatim from `CertLoad.supportReport`, with
     the fields that actually failed named by `MemSupport.scalarFailures` --
     never inferred.  Then the memory route's own report.
  3. BTA + SPECIALIZE.  `mixDriver` at the requested budget on `hwMemAP`, whose
     entry division is `[.stat, .dyn, .dyn, .dyn]`: the design STATIC, and
     exactly the edge vector, the input vector and the state DYNAMIC.
  4. CHECK.  The existing production checker `Projection.Hw.checkResidual`,
     unchanged, and its RETURNED bound.  There is no fallback bound.
  5. EXECUTE.  Gated by `HwMem.runtimeFullOKB`; the residual is evaluated at the
     returned bound, the result is decoded by the real `StateM.decResultM`, and
     the DECODED next state is what the following cycle runs on.

FAILURE KINDS ARE DISTINCT, and each has its own exit code, because they are
different answers: a parse failure is not an admission refusal, an admission
refusal is not a generation failure, a generation failure is not a checker
rejection, and none of them is an execution failure.  A fuel exhaustion is
reported as a failure AT THAT BUDGET and never as a semantic refusal.

BINDING.  The bundle is built only by `MemProjection.mkMemSimAt`, which
re-derives the residual and the bound from the design it is handed;
`mkMemSimAt_design` proves the result is bound to that design.  An arbitrary
`MemSimulator` value is not a checked one and this driver cannot be handed one.
The certificate's own bytes are digested and printed, so the artifact says which
certificate it describes.

STARTUP CONTRACT.  `initStateOf` is zero tables and reset-valued flops.  That is
a LEGAL-STATE WITNESS -- it shows the policy is not vacuous -- and it is NOT an
RTL startup contract.  It is used only under `init=zero`, which is printed as
such.  Any other startup contract is REFUSED, not silently replaced: a design
whose real initial memory contents matter must have them supplied, and this
driver does not invent them.  A `memConst` ROM is a different thing entirely --
it is a SOURCE, its actual contents and `contents.size` bound come from the
certificate, and they pass through untouched.

Exit: 0 ok / 1 execution failure / 2 configuration refused / 3 specialization
out of fuel at this budget / 4 specialization failed / 5 residual check
rejected / 6 memory route refuses admission / 7 runtime policy refuses the
state, input or edge vector / 8 parse/load failure.
-/
import LeanSemanticPrimitives.Projection.MemProjection
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport
import LeanSemanticPrimitives.Compiler.CertIOFacade

open Compiler Projection Projection.Hw Projection.HwMem Projection.MemProjection
open Projection.Runner

/-! ## Metrics -/

def readProcField (path : String) (key : String) : IO String := do
  let txt ← IO.FS.readFile path
  match (txt.splitOn "\n").find? (fun l => l.startsWith key) with
  | some l => pure (l.drop key.length).toString
  | none   => pure "?"

/-- Peak resident set, as the kernel reports it for THIS process. -/
def peakRSS : IO String := do
  try readProcField "/proc/self/status" "VmHWM:" catch _ => pure "?"

/-- utime+stime in clock ticks, from `/proc/self/stat` fields 14 and 15. -/
def cpuTicks : IO Nat := do
  try
    let txt ← IO.FS.readFile "/proc/self/stat"
    -- the comm field may contain spaces; everything after the final ')' is safe
    let after := (txt.splitOn ") ").getLast!
    let fs := (after.splitOn " ")
    let u := (fs[11]?.getD "0").toNat?.getD 0
    let s := (fs[12]?.getD "0").toNat?.getD 0
    pure (u + s)
  catch _ => pure 0

/-! ## Loading -/

def loadAny (p : System.FilePath) : IO (CertIO.Format × DesignCert) := do
  let b ← IO.FS.readBinFile p
  match CertIO.admitAny b with
  | .ok fd   => pure fd
  | .error e => throw (IO.userError s!"{p}: {e}")

/-- FNV-1a over the certificate's bytes.  The artifact says which certificate it
describes, independently of the path it was read from. -/
def fnv1a (b : ByteArray) : UInt64 :=
  b.foldl (fun h c => (h ^^^ c.toUInt64) * 1099511628211) 14695981039346656037

/-- A few words of a table, so the report SHOWS the contents rather than
asserting that they survived. -/
def preview (m : MemImg) (n : Nat) : String :=
  String.intercalate "," ((List.range (min n m.words.length)).map
    (fun k => toString (m.get (Int.ofNat k)).value))

/-- How many addresses actually changed.  A run whose memory never moved is
reported as such rather than described as advancing it. -/
def diffWords (a b : MemImg) : Nat :=
  (List.range (max a.words.length b.words.length)).countP
    (fun k => a.get (Int.ofNat k) != b.get (Int.ofNat k))

/-! ## Retained DCERT2 metadata, reported rather than dropped -/

def flopLine (k : Nat) (f : FlopDesc) : String :=
  let en := match f.enable with | none => "none" | some s => s!"slot {s}"
  let rp := match f.resetPin with | none => "none" | some s => s!"slot {s}"
  s!"    flop {k}: width {f.width} din slot {f.din} enable {en} resetPin {rp} \
resetValue {f.resetValue} activeLow {f.resetActiveLow} clock {f.clock} \
async {f.asyncReset}"

def memLine (k : Nat) (m : MemoryDesc) : String :=
  s!"    memory {k}: aw {m.aw} dw {m.dw} nextImg slot {m.nextImg} clock {m.clock} \
(table {2 ^ m.aw} words)"

def romLine (k : Nat) (sd : SourceDesc) : Option String :=
  match sd with
  | .memConst aw dw ct =>
      let img := MemImg.ofMemConst dw ct
      some s!"    rom source {k}: aw {aw} dw {dw} contents {ct.size} word(s), \
table {img.words.length} (= contents.size, NOT 2^aw = {2 ^ aw}), \
first words [{preview img 4}], default {img.dflt.value} at width {img.dflt.width}, \
out-of-contents read at {ct.size} = {(img.get (Int.ofNat ct.size)).value}"
  | .memImg i aw dw => some s!"    image source {k}: state memory {i}, aw {aw} dw {dw}"
  | _ => none

/-- WHICH node the slot-type inference stopped at.  `typed false` on its own is
a boolean; this names the node and the operator, the way `dcert2_support`
names an unsupported source or op. -/
def firstUntyped : Array SlotTy → List DenseNodeCert → Nat →
    Option (Nat × DenseNodeCert)
  | _,  [],      _ => none
  | ts, c :: cs, k =>
      match inferStep ts c with
      | none     => some (k, c)
      | some ts' => firstUntyped ts' cs (k + 1)

def untypedReport (D : DesignCert) : String :=
  match firstUntyped (D.sources.map srcTy) D.nodes.toList 0 with
  | none        => "no node refused (the failure is elsewhere)"
  | some (k, c) =>
      s!"node {k} ({repr c.op}, width {c.width}, {c.deps.size} operand(s)) \
has no slot type: the memory route implements Op_MemWriteBE and Op_MemRead, \
and every scalar operator OpSupported admits"

/-! ## Comparison against the independent reference

Exhaustive over the table when it is small enough to enumerate, and SAID to be
skipped otherwise -- never a sample reported as a comparison. -/

def memAgreesAt (img : MemImg) (ref : Int → BV) (aw : Nat) : Bool :=
  (List.range (2 ^ aw)).all (fun x => img.get (Int.ofNat x) == ref (Int.ofNat x))

def memsAgree (D : DesignCert) (st : StateM) (ref : RuntimeState) : Option Bool :=
  if D.memories.all (fun m => m.aw ≤ 12) then
    some ((List.range D.memories.size).all (fun k =>
      match st.mems[k]?, ref.mems[k]? with
      | some img, some f => memAgreesAt img f (D.memories[k]!).aw
      | _, _             => false))
  else none

/-! ## The driver -/

inductive InitMode where
  /-- `initStateOf`: zero tables, reset-valued flops.  A LEGAL-STATE WITNESS. -/
  | zeroWitness
  /-- SUPPLIED contents, chosen by the caller: word `k` holds `k + 1` in every
  declared memory.  The driver does not invent it from the design, and it must
  still pass the policy, so an illegal supply is refused by the first cycle's
  gate rather than quietly corrected. -/
  | rampSupplied
  /-- A DELIBERATELY ILLEGAL supply: flop values narrower than the descriptors
  declare.  The gate must refuse it rather than run on it. -/
  | narrowControl
  deriving Repr

def parseInit (s : String) : Option InitMode :=
  if s == "zero" then some .zeroWitness
  else if s == "ramp" then some .rampSupplied
  else if s == "narrow" then some .narrowControl
  else none

/-- Caller-supplied initial contents, at the declared geometry. -/
def rampImg (aw dw : Nat) : MemImg :=
  ⟨(List.range (2 ^ aw)).map (fun k => mk_bv dw (Int.ofNat k + 1)), mk_bv dw 0⟩

def rampStateOf (D : DesignCert) : StateM :=
  { flops := initFlopsOf D
  , mems  := D.memories.map (fun md => rampImg md.aw md.dw) }

/-- The narrow-flop control the audit asked to be rejected. -/
def narrowStateOf (D : DesignCert) : StateM :=
  { flops := D.flops.map (fun _ => mk_bv 7 0)
  , mems  := D.memories.map (fun md => rampImg md.aw md.dw) }


structure Cfg where
  stepFuel : Nat
  workFuel : Nat
  cycles   : Nat
  init     : InitMode
  compare  : Bool

def say (s : String) : IO Unit := IO.println s

def runRoute (label : String) (certHash : String) (D : DesignCert) (cfg : Cfg) :
    IO UInt32 := do
  let t0 ← IO.monoMsNow
  say s!"{label}"
  say s!"  certificate fnv1a {certHash}"
  say s!"  shape: sources {D.sources.size} nodes {D.nodes.size} outputs {D.outputs.size} \
flops {D.flops.size} memories {D.memories.size} clocks {D.clocks.size}"
  -- 1. retained metadata
  say s!"  clocks: {String.intercalate ", " (D.clocks.toList.map (fun c => c.name))}"
  for (f, k) in D.flops.toList.zipIdx do say (flopLine k f)
  for (m, k) in D.memories.toList.zipIdx do say (memLine k m)
  for (sd, k) in D.sources.toList.zipIdx do
    match romLine k sd with | some l => say l | none => pure ()
  -- 2. support: the SCALAR report, verbatim, with the fields that really failed
  let sr := CertLoad.supportReport D
  say s!"  scalar support: wf {sr.wf} memFree {sr.memFree} sources {sr.sources} \
ops {sr.ops} arities {sr.arities} flopClocks {sr.flopClocks} | ALL {sr.allOK}"
  unless sr.allOK do
    say s!"  scalar refused on: {String.intercalate "," (MemSupport.scalarFailures D)}"
  let mr := MemSupport.report D
  say s!"  memory route: accepts {mr.accepts} typed {mr.typed} sized {mr.sized} \
nextImgs {mr.nextImgs} | ADMITTED {mr.admitOK}"
  say s!"  verdict: {MemSupport.verdict D}"
  unless mr.typed && mr.sized do
    say s!"  memory route typing FALSE: {untypedReport D}"
  let t1 ← IO.monoMsNow
  unless mr.admitOK do
    IO.eprintln "  MEMORY ROUTE REFUSES ADMISSION -- no generation attempted"
    say s!"  ms: support {t1 - t0}"
    say "  RESULT: MEMROUTE ADMISSION REFUSED"
    return 6
  -- 3/4. specialize at the requested budget, then the production checker
  say s!"  specializing hwMemAP (division [.stat,.dyn,.dyn,.dyn]: design static; \
edges, input, state dynamic) at fuel {cfg.stepFuel}/{cfg.workFuel}"
  (← IO.getStdout).flush
  let t2 ← IO.monoMsNow
  let res ← IO.lazyPure (fun _ => projectMemDesignAt cfg.stepFuel cfg.workFuel D)
  match res with
  | .error .outOfFuel =>
      let t3 ← IO.monoMsNow
      IO.eprintln s!"  specialization OUT OF FUEL at {cfg.stepFuel}/{cfg.workFuel} \
-- a failure AT THIS BUDGET, not a semantic refusal"
      say s!"  ms: support {t1 - t0}  specialize {t3 - t2}"
      say "  RESULT: MEMROUTE SPECIALIZATION OUT OF FUEL"
      return 3
  | .error e =>
      let t3 ← IO.monoMsNow
      IO.eprintln s!"  specialization FAILED ({repr e})"
      say s!"  ms: support {t1 - t0}  specialize {t3 - t2}"
      say "  RESULT: MEMROUTE SPECIALIZATION FAILED"
      return 4
  | .ok R =>
    let t3 ← IO.monoMsNow
    match mkMemSimAt cfg.stepFuel cfg.workFuel D with
    | none =>
        let t4 ← IO.monoMsNow
        IO.eprintln "  checkResidual REJECTED the residual -- failing closed, \
no execution at a fallback bound"
        say s!"  ms: support {t1 - t0}  specialize {t3 - t2}  check {t4 - t3}"
        say "  RESULT: MEMROUTE CHECK REJECTED"
        return 5
    | some sim =>
      let t4 ← IO.monoMsNow
      let sz := (R.funs.map (fun fd => CertLoad.tsize fd.body)).foldl (·+·) 0
      say s!"  residual {sz} terms, {R.funs.length} function(s), \
arity {R.funs.map FunDef.arity}, checker bound {sim.bound}"
      -- 5. execute, gated by the runtime policy
      let st0 : StateM := match cfg.init with
                          | .zeroWitness  => initStateOf D
                          | .rampSupplied => rampStateOf D
                          | .narrowControl => narrowStateOf D
      match cfg.init with
      | .zeroWitness  =>
          say "  init=zero: zero tables and reset-valued flops -- a LEGAL-STATE \
WITNESS, not an RTL startup contract"
      | .rampSupplied =>
          say "  init=ramp: SUPPLIED initial contents (word k holds k+1), not \
invented from the design and not an RTL startup contract either"
      | .narrowControl =>
          say "  init=narrow: NEGATIVE CONTROL -- 7-bit flop values against the \
declared widths; the runtime gate must refuse this"
      say s!"  initial memory 0 words [{(match st0.mems[0]? with
             | some m => preview m 6 | none => "-")}]"
      say s!"  executing {cfg.cycles} cycle(s) of the residual at the checked bound"
      (← IO.getStdout).flush
      let t5 ← IO.monoMsNow
      let mut st := st0
      let mut tags : List String := []
      let mut outs : List (Array BV) := []
      let mut flops : List (Array BV) := []
      let mut memTags : List String := []
      let mut bad := false
      let mut policyBad := false
      let mut refBad := false
      for k in List.range cfg.cycles do
        let e := edgeSchedule D k
        let i := mkInputFor D k
        if runtimeFullOKB D i st e != true then
          tags := "policy-refused" :: tags
          policyBad := true
        else
          match runMemSim sim e i st with
          | none   => tags := "no-step" :: tags; bad := true
          | some r =>
              tags := "ok" :: tags
              outs := r.outputs :: outs
              flops := r.nextState.flops :: flops
              if cfg.compare then
                let ref := Compiler.interpretDesign D e i (StateM.denote st)
                let oOK := r.outputs == ref.outputs
                let fOK := r.nextState.flops == ref.nextState.flops
                let mOK := memsAgree D r.nextState ref.nextState
                match mOK with
                | some true  => memTags := "mem=" :: memTags
                | some false => refBad := true; memTags := "MEM-DISAGREE" :: memTags
                | none       => memTags := "mem-skipped(aw>12)" :: memTags
                unless oOK && fOK do refBad := true
              -- the DECODED next state is what the following cycle runs on
              st := r.nextState
      let t6 ← IO.monoMsNow
      let rss ← peakRSS
      let cpu ← cpuTicks
      say s!"  step outcomes: {String.intercalate "," tags.reverse}"
      if cfg.compare then
        say s!"  reference comparison: outputs/flops/memories agree = {!refBad} \
({String.intercalate "," memTags.reverse})"
      say s!"  distinct output vectors {(outs.eraseDups).length} of {outs.length}; \
distinct flop vectors {(flops.eraseDups).length} of {flops.length}"
      say s!"  final memory 0 words [{(match st.mems[0]? with
             | some m => preview m 6 | none => "-")}]"
      for k in List.range D.memories.size do
        match st0.mems[k]?, st.mems[k]? with
        | some a, some b =>
            say s!"  memory {k}: {diffWords a b} word(s) differ from the initial image"
        | _, _ => pure ()
      say s!"  final policy: stateFullOKB {stateFullOKB D st}"
      say s!"  ms: support {t1 - t0}  specialize {t3 - t2}  check {t4 - t3}  \
execute {t6 - t5}   cpu_ticks {cpu}  peak_rss {rss}"
      if policyBad then
        IO.eprintln "  the runtime policy REFUSED the state, input or edge vector \
-- no cycle was run on an unchecked state"
        say "  RESULT: MEMROUTE RUNTIME POLICY REFUSED"
        return 7
      if bad then
        IO.eprintln "  at least one cycle did not step -- failing closed"
        say "  RESULT: MEMROUTE EXECUTION FAILED"
        return 1
      if refBad then
        IO.eprintln "  the residual DISAGREED with the reference -- failing closed"
        say "  RESULT: MEMROUTE REFERENCE DISAGREEMENT"
        return 1
      say "  RESULT: MEMROUTE OK (admitted, specialized, checked, executed at the \
returned bound, and agreeing with the reference)"
      return 0

def fixtureOf (n : String) : Option DesignCert :=
  if n == "full" then some fullDesign
  else if n == "mixed" then some mixedDesign
  else if n == "twomem" then some twoMemDesign
  else if n == "lane" then some laneDesign
  else if n == "romwrite" then
    -- NEGATIVE CONTROL: the write target is a ROM, so the memory route refuses
    some { mixedDesign with
           sources := #[ .memConst 5 32 #[1, 2, 3], .input 0 5, .input 1 32
                       , .input 2 1, .input 3 32 ] }
  else none

def usage : String :=
  "usage: dcert2_memroute (<cert.dcert>|--fixture <name>) <stepFuel> <workFuel> \
[cycles] [init] [nocompare]\n  fixtures: full mixed twomem lane\n  fixtures (negative): romwrite\n  init: zero | ramp | narrow"

def parseCfg (rest : List String) : Except String Cfg := do
  match rest with
  | a :: b :: more =>
    let some sf := a.toNat? | .error s!"stepFuel {repr a} is not a natural number"
    let some wf := b.toNat? | .error s!"workFuel {repr b} is not a natural number"
    if sf == 0 || wf == 0 then .error s!"fuel must be positive, got {sf}/{wf}"
    let cycArg := more.head?.getD "8"
    let some cyc := cycArg.toNat? | .error s!"cycles {repr cycArg} is not a natural number"
    if cyc == 0 then
      .error "cycles must be POSITIVE: zero cycles executes no residual step, so an OK would be earned by doing nothing"
    let initArg := (more.drop 1).head?.getD "zero"
    let some im := parseInit initArg
      | .error s!"startup contract {repr initArg} is NOT supported by this driver. \
It is refused rather than silently replaced by zeros; supply the real initial \
contents or use init=zero, which is a legal-state witness only."
    let cmp := !((more.drop 2).contains "nocompare")
    .ok ⟨sf, wf, cyc, im, cmp⟩
  | _ => .error usage

def main (args : List String) : IO UInt32 := do
  match args with
  | "--fixture" :: name :: rest =>
      match fixtureOf name, parseCfg rest with
      | none, _ => do IO.eprintln s!"REFUSING: unknown fixture {repr name}"; return 2
      | _, .error e => do IO.eprintln s!"REFUSING: {e}"; return 2
      | some D, .ok cfg => runRoute s!"fixture:{name}" "n/a(in-repo fixture)" D cfg
  | path :: rest =>
      match parseCfg rest with
      | .error e => do IO.eprintln s!"REFUSING: {e}"; return 2
      | .ok cfg => do
          let bytes? ← try (do let b ← IO.FS.readBinFile path; pure (some b))
                       catch e => do IO.eprintln s!"LOAD FAILED: {e}"; pure none
          let some bytes := bytes? | return (8 : UInt32)
          match CertIO.admitAny bytes with
          | .error e => do
              IO.eprintln s!"PARSE FAILED: {path}: {e}"
              IO.println "  RESULT: MEMROUTE PARSE FAILED"
              return 8
          | .ok (fmt, D) => do
              IO.println s!"  format {repr fmt} (routed by magic; admitted under \
that format's own policy)"
              runRoute path (toString (fnv1a bytes)) D cfg
  | _ => do IO.eprintln usage; return 2
