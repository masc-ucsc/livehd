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
  3. BTA + SPECIALIZE.  `mixDriver` at the requested budget on `hwMemAPF`
     (`MemFused`: the memory interpreter with `mainM`'s `env0` binding inlined
     and the node fold fused into its readers, `memF_iff`; `route:var` selects
     `MemVariant`'s `hwMemAPVar`, `route:plain` the original `hwMemAP`),
     whose entry division is `[.stat, .dyn, .dyn, .dyn]`: the design STATIC,
     and exactly the edge vector, the input vector and the state DYNAMIC.
  4. CHECK.  The existing production checker `Projection.Hw.checkResidual`,
     unchanged, and its RETURNED bound.  There is no fallback bound.
  5. EXECUTE.  Gated by `HwMem.runtimeFullOKB`; the residual is evaluated at the
     returned bound, the result is decoded by the real `StateM.decResultM`, and
     the DECODED next state is what the following cycle runs on.
     The step runs the residual with its list tails shared (`Projection/ShareTails`, at
     the bound the same checker gives it; `share:off` turns this off), and the residual
     as specialized when that one gives no value: that step is `runMemSim` itself on
     every input (`ShareTails.runMemSimS_eq`).

FAILURE KINDS ARE DISTINCT, and each has its own exit code, because they are
different answers: a parse failure is not an admission refusal, an admission
refusal is not a generation failure, a generation failure is not a checker
rejection, and none of them is an execution failure.  A fuel exhaustion is
reported as a failure AT THAT BUDGET and never as a semantic refusal.

BINDING.  The bundle is built by `MemProjection.mkMemSimOf`, which takes the
residual this driver has JUST specialized and does only the admission check and
the production residual check -- so the reported check time is a check and not a
second specialization.  `mkMemSimOf_wf` is what makes that honest: it requires
the caller to EXHIBIT the specialization that produced the residual, so an
arbitrary `Program` cannot be laundered into a checked bundle.  (`mkMemSimAt`,
which specializes for itself, remains available and `mkMemSimAt_design` proves
its result is bound to the design it was handed; this driver does not use it,
because it would pay for specialization twice.)
The certificate's own bytes are digested and printed, so the artifact says which
certificate it describes.

STARTUP CONTRACT.  `init:reset` (the default) is `initStateOf`: every flop at its
reset value and every memory at its power-on words (0 past them), the start
`cert_trace` calls `init:reset` (`Direct.resetState`).  Any other start is read
from a checkpoint (`ckpt:<file>`) or is one of the two synthetic controls (`ramp`,
`narrow`); anything else is REFUSED, not silently replaced.  A `memConst` ROM is
a different thing entirely -- it is a SOURCE, its actual contents and
`contents.size` bound come from the certificate, and they pass through untouched.

OPTION WORDS.  A word this driver shares with `cert_trace` means what it means
there: `edges:third` (every clock quiet on cycles k % 3 = 2) and `init:reset`.
The earlier words `edges:all` (which here meant `edges:third`; `cert_trace`'s
`edges:all` fires every clock on every cycle) and `zero` (which meant
`init:reset`; `cert_trace`'s `init:zero` starts every flop at 0) are refused with
a message naming the new word, so a run never changes meaning when it moves
between drivers.  The `memstate v1` start files and the `react` stimulus lines
are gone: a start state is a checkpoint, as in every other driver, and a
stimulus is the `stim v1` every other driver reads.

CHECKPOINTS (`lhdcert-ckpt v1`, `Compiler/Ckpt.lean`, `pass/lean/tools/formats/ckpt.md`).
The start `ckpt:<file>` reads a checkpoint, validates it against the certificate (design
hash, shapes, values; a refusal is a configuration refusal, exit 2) and builds the FP state
from it (`CkptStateM.toStateM`, which denotes `restore c` by `toStateM_denote`). The run then
starts at the checkpoint's cycle `c`: cycle numbers are absolute, for the stimulus's `at`
lines, the edge schedule and the `outputs` lines alike. `ckpt-at:<k>:<file>` writes the state
reached after `k` steps (`c <= k <= c + cycles`, repeatable) and `ckpt-out:<file>` the final
state, both through `CkptStateM.saveM`, so the bytes equal those `cert_trace` writes for the
same state on the IR and VC routes. A checkpoint due after a cycle that did not step or was
refused by the runtime policy is not written (the run fails anyway).

Exit: 0 ok / 1 execution failure / 2 configuration refused / 3 specialization
out of fuel at this budget / 4 specialization failed / 5 residual check
rejected / 6 memory route refuses admission / 7 runtime policy refuses the
state, input or edge vector / 8 parse/load failure.
-/
import LeanSemanticPrimitives.Projection.MemProjection
-- checkpoints: lhdcert-ckpt v1 as the FP state (toStateM_denote)
import LeanSemanticPrimitives.Projection.CkptStateM
import LeanSemanticPrimitives.Projection.HardwareMemWriteAdmit
import LeanSemanticPrimitives.Projection.MemVariantSim
import LeanSemanticPrimitives.Projection.MemFused
import LeanSemanticPrimitives.Projection.MemRunFast
import LeanSemanticPrimitives.Projection.ShareTails
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport
import LeanSemanticPrimitives.Compiler.CertIORoute   -- DCERT2 + DCERT1, routed by magic (Mathlib-free)
import LeanSemanticPrimitives.Compiler.RunAdmission   -- C3 edges / C4 reset polarity
import LeanSemanticPrimitives.Compiler.DirectSemantics -- the reference step, computed in one dense pass

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

/-- THE WRITE GATE'S OWN SLOTS, discovered from the certificate rather than
assumed.  A hard-coded slot id would silently describe a different design.

From the declared memory's next image: the `Op_MemWriteBE` node, then its
enable operand through any single-operand `Op_Or` wrapper, then the `Op_And`
gate, whose two operands are the ORIGINAL enable and the slot MASK, and finally
the `Op_EQ` in the mask's cone and the phase flop it compares. -/
structure WriteSlots where
  write    : Nat
  addr     : Nat
  datum    : Nat
  effEn    : Nat     -- the enable the memory actually reads
  origEn   : Nat     -- the design's own enable, before the slot gate
  mask     : Nat     -- the slot mask
  pred     : Nat     -- the Op_EQ slot predicate
  phaseSrc : Nat     -- the phase flop source

partial def findOp (D : DesignCert) (want : LGraphOp → Bool) (fuel k : Nat) : Option Nat :=
  if fuel = 0 || k < D.sources.size then none
  else match D.nodes[k - D.sources.size]? with
       | none   => none
       | some c =>
         if want c.op then some k
         else (c.deps.toList.map (findOp D want (fuel - 1))).foldl
                (fun a r => match a with | some _ => a | none => r) none

def writeSlots (D : DesignCert) : Option WriteSlots := do
  let md ← D.memories[0]?
  let wr ← D.nodes[md.nextImg - D.sources.size]?
  let a ← wr.deps[1]?; let dt ← wr.deps[2]?; let en0 ← wr.deps[3]?
  -- walk single-operand Or wrappers down to the gate
  let rec peel (fuel k : Nat) : Nat :=
    if fuel = 0 then k
    else match D.nodes[k - D.sources.size]? with
         | some c => if c.op == .Op_Or && c.deps.size == 1 then peel (fuel-1) (c.deps[0]!) else k
         | none   => k
  let gate := peel 8 en0
  let gc ← D.nodes[gate - D.sources.size]?
  if gc.op != .Op_And || gc.deps.size != 2 then none else do
  let orig ← gc.deps[0]?; let msk ← gc.deps[1]?
  let pr ← findOp D (fun o => o == .Op_EQ) 24 msk
  let pc ← D.nodes[pr - D.sources.size]?
  let ph ← (pc.deps.toList.filter (fun x => x < D.sources.size)).find?
             (fun x => match D.sources[x]? with
                       | some (.flopQ _ _) => true
                       | some (.flopQAsync _ _ _ _ _) => true
                       | _ => false)
  some ⟨md.nextImg, a, dt, en0, orig, msk, pr, ph⟩

/-- EVERY word, with its width, plus the default.  `preview` truncates, which
is fine for a progress line but cannot support a preservation claim: an
eight-word memory printed six words wide hid addresses 6 and 7 from the first
acceptance gate entirely. -/
def fullImage (m : MemImg) : String :=
  let ws := String.intercalate "," (m.words.map (fun v => s!"{v.value}:{v.width}"))
  s!"[{ws}] dflt {m.dflt.value}:{m.dflt.width}"

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

/-- `(key, count)` tallies, insertion-ordered -- the same shape
`dcert2_support` uses, so a large design reports its metadata as counts rather
than as one line per element. -/
def tally (xs : List String) : List (String × Nat) :=
  xs.foldl (fun acc k =>
    if acc.any (fun p => p.1 == k)
    then acc.map (fun p => if p.1 == k then (p.1, p.2 + 1) else p)
    else acc ++ [(k, 1)]) []

def render (t : List (String × Nat)) : String :=
  String.intercalate ", " (t.map (fun p => s!"{p.1} x{p.2}"))

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
has no slot type: the memory route implements Op_MemRead, Op_MemWriteBE, \
Op_MemLoad and Op_MemToBus (decision #2), and every scalar operator OpSupported admits"

/-! ## Comparison against the independent reference

The status is REPORTED, never implied.  `COMPLETE` means the memory array
length, EVERY admitted table word, the default and the out-of-range reads were
all compared; `PARTIAL` names what was skipped; `DISABLED` means the caller
turned it off; `FAILED` names what disagreed.  Only `COMPLETE` earns
reference-comparison credit, and the final verdict says which one it was. -/

inductive CmpStatus where
  | complete
  | skipped  (what : String)
  | disabled
  | failed   (what : String)
  deriving Inhabited

def CmpStatus.label : CmpStatus → String
  | .complete   => "COMPLETE (array length, every table word, default and out-of-range)"
  | .skipped w  => s!"PARTIAL -- NOT full credit; skipped: {w}"
  | .disabled   => "DISABLED by nocompare -- NOT full credit; nothing was compared"
  | .failed w   => s!"FAILED: {w}"

def CmpStatus.isComplete : CmpStatus → Bool
  | .complete => true
  | _         => false

def CmpStatus.isFailure : CmpStatus → Bool
  | .failed _ => true
  | _         => false

/-- ONE memory, exhaustively: the table length, every address it holds, the
default (read at `-1`, which no table holds) and the first address ABOVE the
table.  A default-only difference is caught by the `-1` read, which is why it
is here and not left to the word loop. -/
def memOneOK (aw : Nat) (img : MemImg) (ref : Int → BV) : Bool :=
  (img.words.length == 2 ^ aw)
  && (List.range (2 ^ aw)).all (fun x => img.get (Int.ofNat x) == ref (Int.ofNat x))
  && (img.get (-1) == ref (-1))
  && (img.get (Int.ofNat (2 ^ aw)) == ref (Int.ofNat (2 ^ aw)))

/-- The largest table this driver will enumerate.  Above it the memory part of
the comparison is SKIPPED and said to be. -/
def awEnumLimit : Nat := 16

def memsCheck (D : DesignCert) (st : StateM) (ref : RuntimeState) : CmpStatus :=
  if st.mems.size != D.memories.size then
    .failed s!"state memory array length {st.mems.size} != declared {D.memories.size}"
  else if st.mems.size != ref.mems.size then
    .failed s!"memory array length {st.mems.size} != reference {ref.mems.size}"
  else
    match (List.range D.memories.size).find?
            (fun k => (D.memories[k]!).aw > awEnumLimit) with
    | some k => .skipped s!"memory {k} has aw {(D.memories[k]!).aw} > {awEnumLimit}"
    | none   =>
        match (List.range D.memories.size).find? (fun k =>
                match st.mems[k]?, ref.mems[k]? with
                | some img, some f => !(memOneOK (D.memories[k]!).aw img f)
                | _, _             => true) with
        | some k => .failed s!"memory {k} disagrees with the reference"
        | none   => .complete

/-- **The reference step this driver compares against is `interpretDesign`.**  It is
COMPUTED by `directStepRaw` -- one left-to-right pass over a dense array -- which
is proved equal to `interpretDesign` for every design whose dependencies point
backwards, and every design the memory route admits is one.  Evaluated
directly, `interpretDesign`'s environment is a chain of closures, so one cycle
costs O(N^2) in the slot count: that, not the residual, was where FP spent its
time on SoomRV's MemRTL (10,348 nodes; profile: `envSetG`, `lean_apply_1`). -/
theorem refStep_eq_interpretDesign {D : DesignCert} (h : admitsB D = true)
    (e : ClockEdges) (i : RuntimeInput) (s : RuntimeState) :
    Direct.directStepRaw D e i s = Compiler.interpretDesign D e i s :=
  Direct.directStepRaw_correct D (designCertWF_of_semwf (admits_semwf h)).depsBounded e i s

def cycleCheck (D : DesignCert) (r : StateM.ResultM) (ref : RuntimeResult) : CmpStatus :=
  if r.outputs != ref.outputs then .failed "outputs"
  else if r.nextState.flops != ref.nextState.flops then .failed "flops"
  else memsCheck D r.nextState ref.nextState

/-- The weakest status seen, so one skipped cycle cannot be reported as a
complete comparison. -/
def CmpStatus.meet : CmpStatus → CmpStatus → CmpStatus
  | .failed w, _ => .failed w
  | _, .failed w => .failed w
  | .disabled, _ => .disabled
  | _, .disabled => .disabled
  | .skipped w, _ => .skipped w
  | _, .skipped w => .skipped w
  | .complete, .complete => .complete

/-! ## The driver -//-! ## The driver -/

inductive InitMode where
  /-- `initStateOf`: every flop at its reset value, every memory at its power-on
  words; `cert_trace`'s `init:reset`. -/
  | reset
  /-- An internally generated SYNTHETIC pattern (word `k` holds `k + 1`).  It is
  a control, not a loader: nothing about it comes from the caller. -/
  | rampSynthetic
  /-- A deliberately illegal synthetic control: flop values narrower than
  declared.  The gate must refuse it. -/
  | narrowControl
  /-- An `lhdcert-ckpt v1` checkpoint, validated against the certificate; the run
  starts at its cycle. -/
  | ckptFile (path : String)

/-- The start-state word, or why it is refused: an earlier word names the word that replaced it. -/
def parseInit (s : String) : Except String InitMode :=
  if s == "init:reset" then .ok .reset
  else if s == "ramp" then .ok .rampSynthetic
  else if s == "narrow" then .ok .narrowControl
  else if s.startsWith "ckpt:" then .ok (.ckptFile (s.drop 5).toString)
  else if s == "zero" then
    .error "`zero` is now `init:reset` (every flop at its reset value, every memory at its \
power-on words), the word cert_trace uses for that start; cert_trace's `init:zero` (every flop \
at 0) is a different start, which this driver does not run"
  else if s.startsWith "file:" then
    .error s!"{s}: memstate start files are no longer read; start from a checkpoint with \
`ckpt:<file>` (lhd_ckpt init and import-lhd write one)"
  else
    .error s!"startup contract {repr s} is NOT supported by this driver (init:reset | ramp | \
narrow | ckpt:<file>). It is refused rather than silently replaced."

def InitMode.label : InitMode → String
  | .reset          => "init:reset (every flop at its reset value, every memory at its \
power-on words: initStateOf)"
  | .rampSynthetic  => "ramp (SYNTHETIC control: an internally generated pattern, \
NOT caller-supplied contents)"
  | .narrowControl  => "narrow (SYNTHETIC negative control: under-width flops)"
  | .ckptFile p     => s!"ckpt:{p} (an lhdcert-ckpt v1 checkpoint, validated against the certificate)"

/-- An internally generated table at the declared geometry (word `k` holds `k + 1`). -/
def rampImg (aw dw : Nat) : MemImg :=
  ⟨(List.range (2 ^ aw)).map (fun k => mk_bv dw (Int.ofNat k + 1)), mk_bv dw 0⟩

def rampStateOf (D : DesignCert) : StateM :=
  { flops := initFlopsOf D
  , mems  := D.memories.map (fun md => rampImg md.aw md.dw) }

/-- The narrow-flop control the audit asked to be rejected. -/
def narrowStateOf (D : DesignCert) : StateM :=
  { flops := D.flops.map (fun _ => mk_bv 7 0)
  , mems  := D.memories.map (fun md => rampImg md.aw md.dw) }


/-! ### Supplied stimulus

`mkInputFor` generates a deterministic pattern from each input's WIDTH.  That
exercises the route, but it cannot drive a design that needs a particular input
to do anything -- a CPU held in reset never writes its register file, and
reporting "0 words differ" for it is honest but uninformative.  A stimulus file
supplies the vector instead:

```
stim v1
in <ordinal> <width> <value>
at <cycle> in <ordinal> <width> <value>
```

`in` holds for every cycle, `at` for one cycle (it wins); unlisted ordinals keep
their declared width and value 0.  It is SUPPLIED, and the report says so. -/

/-- A stimulus is two things: constants and per-cycle overrides (`at`).  A core
that needs a RESET PULSE cannot be driven by constants alone, which is what `at`
is for. -/
def parseStim (txt : String) :
    Except String (List (Nat × Nat × Int) × List (Nat × Nat × Nat × Int)) := do
  let body := ((txt.splitOn "\n").map (fun l => (l.splitOn "\r").headD "")).filter
                (fun l => l != "" && !l.startsWith "#")
  match body with
  | [] => .error "empty stimulus file"
  | hdr :: rest =>
    if hdr != "stim v1" then .error s!"line 1: expected \"stim v1\", got {repr hdr}"
    else
      let mut out : List (Nat × Nat × Int) := []
      let mut ats : List (Nat × Nat × Nat × Int) := []
      let mut n := 1
      for l in rest do
        n := n + 1
        match (l.splitOn " ").filter (fun w => w != "") with
        | ["at", c, "in", i, w, v] =>
            let some c' := c.toNat? | throw s!"line {n}: cycle {repr c} is not a number"
            let some i' := i.toNat? | throw s!"line {n}: ordinal {repr i} is not a number"
            let some w' := w.toNat? | throw s!"line {n}: width {repr w} is not a number"
            let some v' := v.toInt? | throw s!"line {n}: value {repr v} is not an integer"
            ats := ats ++ [(c', i', w', v')]
        | ["in", i, w, v] =>
            let some i' := i.toNat? | throw s!"line {n}: ordinal {repr i} is not a number"
            let some w' := w.toNat? | throw s!"line {n}: width {repr w} is not a number"
            let some v' := v.toInt? | throw s!"line {n}: value {repr v} is not an integer"
            out := out ++ [(i', w', v')]
        | _ => throw s!"line {n}: unrecognised {repr l}"
      .ok (out, ats)

/-- The supplied vector: a per-cycle override wins over the constant, then zero
at the declared width. -/
def stimInput (D : DesignCert) (spec : List (Nat × Nat × Int))
    (ats : List (Nat × Nat × Nat × Int)) (cyc : Nat) : RuntimeInput :=
  let n := max (maxInputIdx D) ((spec.map (fun t => t.1 + 1)).foldl max 0)
  (List.range n).toArray.map (fun i =>
    let w := match spec.find? (fun t => t.1 == i) with
             | some (_, w, _) => w
             | none           => inputWidth D i
    match ats.find? (fun t => t.1 == cyc && t.2.1 == i) with
    | some (_, _, w', v) => mk_bv w' v
    | none =>
      match spec.find? (fun t => t.1 == i) with
      | some (_, _, v) => mk_bv w v
      | none           => mk_bv w 0)

structure Cfg where
  stepFuel : Nat
  workFuel : Nat
  cycles   : Nat
  init     : InitMode
  compare  : Bool
  /-- a supplied stimulus, applied every cycle, or `none` for the generated one -/
  stim     : Option (List (Nat × Nat × Int)) := none
  ats      : List (Nat × Nat × Nat × Int) := []
  stimProv : String := "generated: mkInputFor (synthetic, width-derived)"
  /-- stop after the support report -- used to compare two certificates'
  admission without paying for specialization -/
  supportOnly : Bool := false
  /-- emit a COMPLETE per-step trace: the loaded image with every width and the
  default, each cycle's inputs and edges, the memory's phase/write-enable state,
  and each DECODED next image.  A preservation gate needs the intermediate
  states; a final-image comparison cannot see a quiet-step write that is
  overwritten before the end. -/
  trace : Bool := false
  /-- run the EXTENDED route, which additionally admits ordinary `Op_MemWrite`.
  ON by default since D6 Tier 1; `no-plain-write` selects the original route,
  which refuses a plain write.  When on, admission is `admitsWB` and the bundle
  is built by `mkMemSimOfW`; the specialization function, the production
  residual checker and the execution function are the SAME ones, and on a
  design with no plain write `runRoute` checks on every run that the two
  admissions agree. -/
  plainWrite : Bool := true
  /-- which clocks fire on each cycle (RunnerSupport `scheduleEdges`): `auto`
  keeps the one-clock schedule (`edges:third`) for a one-clock design and fires
  domains INDEPENDENTLY (`edges:indep`) when the certificate declares more than
  one clock. RunnerSupport's `EdgeMode.all` is the word `edges:third` here. -/
  edgeMode : EdgeMode := .auto
  /-- which memory interpreter is specialized: `fused` (default:
  `MemFused.projectMemFDesignAt`, `env0` inlined and the node fold fused into
  its readers, `runMemSim(W)F_correct`), `var` (`MemVariant`, `env0` inlined
  only, `runMemSim(W)V_correct`) or `plain` (`projectMemDesignAt`,
  `runMemSim(W)_correct`). All three conclusions are the same. -/
  route : String := "fused"
  /-- print each executed cycle's outputs as `STEP <k> <out0> <out1> ...` (each
  output's unsigned value, before the state update), the line format
  `cert_trace` prints, so an FP trace compares line for line with the IR, TV and
  legacy traces on the same stimulus. The shared words mean the same schedule
  and start: `init:reset edges:third stim:<f> outputs` here matches
  `cert_trace <cert> <n> <f> edges:third` (whose start is `init:reset` by
  default). This runner has no every-cycle schedule (`cert_trace`'s `edges:all`)
  and no all-zero start (`cert_trace`'s `init:zero`). -/
  outputs : Bool := false
  /-- `auto` in place of the step budget: size it from the certificate,
  `autoStepFuel D`. -/
  autoStep : Bool := false
  /-- `auto` in place of the work budget: `autoWorkFuel`. -/
  autoWork : Bool := false
  /-- the absolute cycle of the first step: the checkpoint's under `ckpt:`, else 0 -/
  start : Nat := 0
  /-- `ckpt-at:<k>:<file>`: write the state reached after `k` steps -/
  ckptAt : List (Nat × String) := []
  /-- `ckpt-out:<file>`: write the final state -/
  ckptOut : Option String := none
  /-- `loop:full`: run every cycle through `runMemSim` with the full state check, as before
  the fast loop (`Projection/MemRunFast`); `compare` and `trace` always do. -/
  fullLoop : Bool := false
  /-- `share:off`: run the residual as specialized. By default every cycle runs the residual with
  its list tails shared (`Projection/ShareTails`), and the one as specialized when that one gives
  no value: the step is `runMemSim` itself (`ShareTails.runMemSimS_eq`, `stepEnc_eq`). -/
  share : Bool := true

/-- The step budget bounds how deep the specializer recurses, and the depth grows
with the design: the smallest budget that worked on the four largest rows was
1.70 to 1.82 x (nodes + sources) (Vortex top 200,118 for 118,058; nyuzi 172,788
for 99,564; core 112,271 for 66,118; VX_fpu_dsp 66,396 for 36,422). `auto` takes
4 x, with 20,000 (the old fixed default) as the floor for small designs. A budget
is only an upper bound: an unused step costs nothing. -/
def autoStepFuel (D : DesignCert) : Nat := max 20000 (4 * (D.nodes.size + D.sources.size))

/-- The work budget (worklist rounds) never decided a measured row: ten times more
work fuel changed nothing where ten times more step fuel did. 2,000 is ten times
the old default of 200. -/
def autoWorkFuel : Nat := 2000

def say (s : String) : IO Unit := IO.println s

def runRoute (label : String) (certHash : String) (D : DesignCert) (cfg : Cfg)
    (st0 : StateM) (stateProv : String) : IO UInt32 := do
  let cfg := { cfg with stepFuel := if cfg.autoStep then autoStepFuel D else cfg.stepFuel,
                        workFuel := if cfg.autoWork then autoWorkFuel else cfg.workFuel }
  let t0 ← IO.monoMsNow
  say s!"{label}"
  say s!"  certificate fnv1a {certHash}"
  say s!"  shape: sources {D.sources.size} nodes {D.nodes.size} outputs {D.outputs.size} \
flops {D.flops.size} memories {D.memories.size} clocks {D.clocks.size}"
  if let some rc := (← RunAdmission.refuseDesign? D) then return rc
  -- 1. retained metadata
  say s!"  clocks: {String.intercalate ", " (D.clocks.toList.map (fun c => c.name))}"
  -- every flop's metadata is RETAINED; the per-flop dump is capped so a
  -- 174-flop design reports a tally and a sample instead of 174 lines.
  say s!"  flops: {render (tally (D.flops.toList.map (fun f =>
      let en := if f.enable.isSome then "en" else "noen"
      let rp := if f.resetPin.isSome then (if f.resetActiveLow then "rst(lo)" else "rst(hi)") else "norst"
      s!"w{f.width} clk{f.clock} {if f.asyncReset then "async" else "sync"} {rp} {en}")))}"
  for (f, k) in (D.flops.toList.take 6).zipIdx do say (flopLine k f)
  if D.flops.size > 6 then say s!"    ... {D.flops.size - 6} more flop(s), tallied above"
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
  -- the EXTENDED route, reported separately so the two answers never merge
  let plainN := D.nodes.toList.foldl
    (fun n c => if c.op == .Op_MemWrite then n + 1 else n) 0
  let admW := HwMem.admitsWB D
  if cfg.plainWrite then
    say s!"  EXTENDED route (default): Op_MemWrite nodes {plainN}, \
admitsB {HwMem.admitsB D}, admitsWB {admW}"
    if plainN == 0 && HwMem.admitsB D != admW then
      IO.eprintln "  the extension changed an answer on a design with NO plain write"
      return 7
  let admitted := if cfg.plainWrite then admW else mr.admitOK
  let t1 ← IO.monoMsNow
  unless admitted do
    IO.eprintln "  MEMORY ROUTE REFUSES ADMISSION -- no generation attempted"
    say s!"  ms: support {t1 - t0}"
    say "  RESULT: MEMROUTE ADMISSION REFUSED"
    return 6
  if cfg.supportOnly then
    say s!"  ms: support {t1 - t0}"
    say "  RESULT: MEMROUTE SUPPORT ONLY (admitted; no generation attempted)"
    return 0
  -- 3/4. specialize at the requested budget, then the production checker
  say s!"  specializing {if cfg.route == "plain" then "hwMemAP" else if cfg.route == "var"
      then "hwMemAPVar (env0 inlined; MemVariant)" else "hwMemAPF (fold fused; MemFused)"} \
(division [.stat,.dyn,.dyn,.dyn]: design static; edges, input, state dynamic) at fuel \
{cfg.stepFuel}/{cfg.workFuel}{if cfg.autoStep || cfg.autoWork then " (auto: sized from the certificate)" else ""}"
  (← IO.getStdout).flush
  let t2 ← IO.monoMsNow
  -- the bundle is honest by `mkMemSimOf(W)_wfF` / `_wfV` / `_wf` and runs
  -- correctly by `runMemSim(W)F_correct` / `V_correct` / `_correct` (fused / var /
  -- plain). `runMemSim`, `mkMemSimOf(W)` and `checkResidual` are the same
  -- functions on all three.
  let specialize (sf wf : Nat) := IO.lazyPure (fun _ =>
    if cfg.route == "plain" then projectMemDesignAt sf wf D
    else if cfg.route == "var" then MemVariant.projectMemVarDesignAt sf wf D
    else MemFused.projectMemFDesignAt sf wf D)
  let mut sf := cfg.stepFuel
  let mut wf := cfg.workFuel
  let mut res ← specialize sf wf
  -- `auto` fuel gets ONE retry at ten times both budgets: the size rule is a
  -- measured bound, not a proof, so a design that needs more is not refused at
  -- the first budget. The retry is printed; an explicit budget is never retried.
  if cfg.autoStep || cfg.autoWork then
    if let .error .outOfFuel := res then
      say s!"  specialization OUT OF FUEL at the auto budget {sf}/{wf}; retrying once at \
{10 * sf}/{10 * wf}"
      (← IO.getStdout).flush
      sf := 10 * sf
      wf := 10 * wf
      res ← specialize sf wf
  match res with
  | .error .outOfFuel =>
      let t3 ← IO.monoMsNow
      IO.eprintln s!"  specialization OUT OF FUEL at {sf}/{wf} \
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
    -- The residual is already in hand.  `mkMemSimOf` BINDS it -- admission plus
    -- the production residual check, and NO second specialization -- so the
    -- check metric below is a check and not a hidden re-specialization.
    match (if cfg.plainWrite then HwMem.mkMemSimOfW D R else mkMemSimOf D R) with
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
      -- the residual with its list tails shared, and the bound the same checker gives it. Each
      -- cycle runs it, and the residual above when it gives no value: that step is `runMemSim`
      -- itself on every input (`ShareTails.stepEnc_eq`, `runMemSimS_eq`), so every route's
      -- `runMemSim…_correct` holds for it unchanged.
      let ts0 ← IO.monoMsNow
      let sh ← IO.lazyPure (fun _ => if cfg.share then ShareTails.shareSim sim else none)
      match sh with
      | some (R2, b2) =>
        let ts1 ← IO.monoMsNow
        let sz2 := (R2.funs.map (fun fd => CertLoad.tsize fd.body)).foldl (·+·) 0
        say s!"  tails shared (Projection/ShareTails): residual {sz2} terms, checker bound {b2}, \
{ts1 - ts0} ms; a cycle it gives no value for runs the residual above"
      | none =>
        if cfg.share then
          say "  tails shared: the rewritten residual fails checkResidual; every cycle runs the residual above"
        else
          say "  tails shared: off (share:off)"
      -- the supplied or synthetic state, and the policy gate on it
      say s!"  init: {cfg.init.label}"
      say s!"  state provenance: {stateProv}"
      say s!"  state shape: {st0.flops.size} flop value(s), {st0.mems.size} image(s)"
      -- PRESERVATION through the production codec, checked rather than assumed
      if StateM.decStateM (StateM.encStateM st0) != some st0 then
        IO.eprintln "  the supplied state does NOT survive the production codec \
unchanged -- refusing rather than running on a state the encoder altered"
        say "  RESULT: MEMROUTE STATE CODEC REFUSED"
        return 7
      say "  state survives encStateM/decStateM unchanged (words, defaults and flops)"
      unless stateFullOKB D st0 do
        IO.eprintln "  the initial state does NOT satisfy the design's policy \
-- refusing rather than zeroing or repairing it"
        say "  RESULT: MEMROUTE RUNTIME POLICY REFUSED (initial state)"
        return 7
      say s!"  initial memory 0 words [{(match st0.mems[0]? with
             | some m => preview m 6 | none => "-")}]"
      if cfg.trace then
        for (m, idx) in st0.mems.toList.zipIdx do
          say s!"  TRACE loaded memory {idx} {fullImage m}"
        match writeSlots D with
        | none    => say "  TRACE slots UNRESOLVED"
        | some ws => say s!"  TRACE slots write {ws.write} addr {ws.addr} datum {ws.datum} \
effEn {ws.effEn} origEn {ws.origEn} mask {ws.mask} pred {ws.pred} phaseSrc {ws.phaseSrc}"
      say s!"  stimulus: {cfg.stimProv}"
      say s!"  edges: {cfg.edgeMode.describe D}"
      say s!"  executing {cfg.cycles} cycle(s) of the residual at the checked bound"
      -- checkpoints: every one names a cycle of this run, and is written from the
      -- state the residual reached (`saveM`: the bytes the IR and VC routes write)
      for (k, p) in cfg.ckptAt do
        unless cfg.start ≤ k && k ≤ cfg.start + cfg.cycles do
          IO.eprintln s!"  ckpt-at:{k}:{p}: cycle {k} is outside this run \
({cfg.start} .. {cfg.start + cfg.cycles})"
          return 2
      let ckptHash := if cfg.ckptAt.isEmpty && cfg.ckptOut.isNone then 0 else Ckpt.designHash D
      let saveAt (k : Nat) (st : StateM) (ok : Bool) : IO Unit := do
        let due := (cfg.ckptAt.filter (·.1 == k)).map (·.2) ++
          (if k == cfg.start + cfg.cycles then cfg.ckptOut.toList else [])
        for p in due do
          if ok then
            IO.FS.writeBinFile p (Ckpt.render (CkptStateM.saveM D ckptHash k #[] st))
            say s!"  checkpoint at cycle {k} written: {p}"
          else
            IO.eprintln s!"  checkpoint at cycle {k} NOT written ({p}): an earlier cycle \
did not step or was refused"
      (← IO.getStdout).flush
      let t5 ← IO.monoMsNow
      let mut st := st0
      let mut tags : List String := []
      let mut outs : List (Array BV) := []
      let mut flops : List (Array BV) := []
      let mut bad := false
      let mut policyBad := false
      let mut cmp : CmpStatus := if cfg.compare then .complete else .disabled
      if !cfg.compare && !cfg.trace && !cfg.fullLoop then
        -- The same steps and the same lines, without the redundant work (`Projection/MemRunFast`):
        -- * the state stays encoded: the state part of a step's result value is the encoding of
        --   the next state (`next_from_parts`, from the codec's canonicity), so the next step runs
        --   on it (`runMemSimEnc`, `runMemSim_eq_enc`); only the outputs and the flops (which
        --   this run reports) are decoded each cycle, the whole state when a checkpoint is due
        --   and at the end;
        -- * the state check runs once: every route's `runMemSim…_correct` gives `stateFullOKB` of
        --   the state it returns, so the check is its input, flop-bound and edge half
        --   (`runtimeFullOKB_after`), which depends only on sizes and is computed once per sizes.
        let mut sv := StateM.encStateM st
        let mut stKnown := true
        let mut stateOK := false
        let mut fl : Array BV := st.flops
        let mut chk : Option ((Nat × Nat × Nat) × Bool) := none
        for k in List.range' cfg.start cfg.cycles do
          let due := cfg.ckptAt.any (·.1 == k) || (k == cfg.start + cfg.cycles && cfg.ckptOut.isSome)
          if due && !stKnown then
            st := (StateM.decStateM sv).getD st
            stKnown := true
          saveAt k st (!bad && !policyBad)
          let e := scheduleEdges cfg.edgeMode D k
          if let some rc := (← RunAdmission.refuseEdges? D k e) then return rc
          let i := match cfg.stim with
                   | some spec => stimInput D spec cfg.ats k
                   | none      => mkInputFor D k
          let mut pass := false
          if stateOK then
            let key := (i.size, fl.size, e.size)
            match chk with
            | some (k0, b) =>
              if k0 == key then pass := b
              else
                let b := inputOKB D i.size && flopBoundOKB D fl.size && edgesOKB D e
                chk := some (key, b)
                pass := b
            | none =>
              let b := inputOKB D i.size && flopBoundOKB D fl.size && edgesOKB D e
              chk := some (key, b)
              pass := b
          else
            if !stKnown then
              st := (StateM.decStateM sv).getD st
              stKnown := true
            pass := runtimeFullOKB D i st e
          if !pass then
            tags := "policy-refused" :: tags
            policyBad := true
          else
            match ShareTails.stepEnc sim sh e i sv with
            | none => tags := "no-step" :: tags; bad := true
            | some v =>
              match MemRunFast.resultParts v with
              | none => tags := "no-step" :: tags; bad := true
              | some (sp, op) =>
                match decBVs op, (field2 StateM.tagStateM sp).bind (fun p => decBVs p.1) with
                | some os, some fls =>
                  tags := "ok" :: tags
                  if cfg.outputs then
                    IO.println s!"STEP {k} {" ".intercalate (os.toList.map (fun o => toString (bv_uint o)))}"
                  outs := os :: outs
                  flops := fls :: flops
                  sv := sp
                  stKnown := false
                  stateOK := true
                  fl := fls
                | _, _ => tags := "no-step" :: tags; bad := true
        if !stKnown then
          st := (StateM.decStateM sv).getD st
      else
        for k in List.range' cfg.start cfg.cycles do
          saveAt k st (!bad && !policyBad)
          let e := scheduleEdges cfg.edgeMode D k
          if let some rc := (← RunAdmission.refuseEdges? D k e) then return rc
          let i := match cfg.stim with
                   | some spec => stimInput D spec cfg.ats k
                   | none      => mkInputFor D k
          if runtimeFullOKB D i st e != true then
            tags := "policy-refused" :: tags
            policyBad := true
          else
            match ShareTails.runMemSimS sim sh e i st with
            | none   => tags := "no-step" :: tags; bad := true
            | some r =>
                tags := "ok" :: tags
                if cfg.outputs then
                  IO.println s!"STEP {k} {" ".intercalate (r.outputs.toList.map (fun o => toString (bv_uint o)))}"
                outs := r.outputs :: outs
                flops := r.nextState.flops :: flops
                if cfg.compare then
                  -- `interpretDesign`, computed as `directStepRaw` (`refStep_eq_interpretDesign`)
                  let ref := Direct.directStepRaw D e i (StateM.denote st)
                  cmp := cmp.meet (cycleCheck D r ref)
                if cfg.trace then
                  -- the memory's own commit condition for this step, read from
                  -- the SAME `fires`/enable semantics the gate uses, plus the
                  -- decoded next image.  `fires e md.clock` is the quiet/active
                  -- classification; it is derived from the edge vector and the
                  -- declared clock, never from the final image.
                  -- THE PRE-STEP VALUES, from the actual pre-state and input
                  -- through the SAME semantics the reference uses
                  -- (DesignSemantics: evalGraphG over srcEnv).  No overrides.
                  match writeSlots D with
                  | none => say s!"  TRACE step {k} slots UNRESOLVED"
                  | some ws =>
                    let G   := D.toGraphCert
                    let rho := evalGraphG G.topo G (Compiler.srcEnv D i (StateM.denote st))
                    let bv  := fun sl => (rho sl).asBV
                    say s!"  TRACE step {k} pre phase {(bv ws.phaseSrc).value} pred {(bv ws.pred).value} \
  origEn {(bv ws.origEn).value} effEn {(bv ws.effEn).value} \
  addr {(bv ws.addr).value} datum {(bv ws.datum).value}"
                  let edges := String.intercalate "," (e.toList.map (fun b => if b then "1" else "0"))
                  let ivals := String.intercalate "," (i.toList.map (fun v => s!"{v.value}"))
                  for (md, idx) in D.memories.toList.zipIdx do
                    let act := if fires e md.clock then "active" else "quiet"
                    say s!"  TRACE step {k} mem {idx} {act} edges [{edges}] inputs [{ivals}]"
                    match r.nextState.mems[idx]? with
                    | some m => say s!"  TRACE step {k} mem {idx} next {fullImage m}"
                    | none   => say s!"  TRACE step {k} mem {idx} next MISSING"
                  say s!"  TRACE step {k} policy {runtimeFullOKB D i r.nextState e}"
                -- the DECODED next state is what the following cycle runs on
                st := r.nextState
      saveAt (cfg.start + cfg.cycles) st (!bad && !policyBad)
      let t6 ← IO.monoMsNow
      let rss ← peakRSS
      let cpu ← cpuTicks
      say s!"  step outcomes: {String.intercalate "," tags.reverse}"
      say s!"  reference comparison: {cmp.label}"
      say s!"  distinct output vectors {(outs.eraseDups).length} of {outs.length}; \
distinct flop vectors {(flops.eraseDups).length} of {flops.length}"
      say s!"  final memory 0 words [{(match st.mems[0]? with
             | some m => preview m 6 | none => "-")}]"
      if cfg.trace then
        for (m, idx) in st.mems.toList.zipIdx do
          say s!"  TRACE final memory {idx} {fullImage m}"
      for k in List.range D.memories.size do
        match st0.mems[k]?, st.mems[k]? with
        | some a, some b =>
            say s!"  memory {k}: {diffWords a b} word(s) differ from the initial image"
        | _, _ => pure ()
      let finalOK := stateFullOKB D st
      say s!"  final returned-state policy: stateFullOKB {finalOK}"
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
      if cmp.isFailure then
        IO.eprintln "  the residual DISAGREED with the reference -- failing closed"
        say s!"  RESULT: MEMROUTE REFERENCE DISAGREEMENT -- {cmp.label}"
        return 1
      unless finalOK do
        IO.eprintln "  the RETURNED state does not satisfy the policy -- the run \
is not ready to continue, failing closed"
        say "  RESULT: MEMROUTE RETURNED-STATE POLICY FAILED"
        return 7
      if cmp.isComplete then
        say "  RESULT: MEMROUTE OK (admitted, specialized, checked, executed at \
the returned bound, returned state passes the policy, and the reference \
comparison is COMPLETE)"
      else
        say s!"  RESULT: MEMROUTE EXECUTED (admitted, specialized, checked, \
returned state passes the policy) -- reference comparison {cmp.label}, so this \
does NOT earn full reference-comparison credit"
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

/-- Resolve a `stim:<path>` request into the supplied vector, or leave the
generated one in place. -/
def resolveStim (cfg : Cfg) : IO (Except String Cfg) := do
  if !(cfg.stimProv.startsWith "PENDING stim:") then pure (.ok cfg)
  else
    let path := cfg.stimProv.drop "PENDING stim:".length |>.toString
    let bytes? ← try (do let b ← IO.FS.readBinFile path; pure (some b))
                  catch e => do IO.eprintln s!"stimulus unreadable: {e}"; pure none
    match bytes? with
    | none => pure (.error s!"cannot read stimulus file {path}")
    | some bytes =>
      match String.fromUTF8? bytes with
      | none => pure (.error s!"{path}: not valid UTF-8")
      | some txt =>
        match parseStim txt with
        | .error e => pure (.error s!"{path}: {e}")
        | .ok (spec, ats) =>
            let prov := s!"SUPPLIED file {path}, {bytes.size} bytes, fnv1a {fnv1a bytes}, {spec.length} constant(s), {ats.length} per-cycle override(s)"
            pure (.ok { cfg with stim := some spec, ats := ats, stimProv := prov })

/-- Build the initial state: the power-on state, a synthetic control (generated
here and SAID to be synthetic), or a checkpoint validated against the certificate. -/
def buildState (D : DesignCert) (im : InitMode) : IO (Except String (StateM × String)) := do
  match im with
  | .ckptFile path => do
      let bytes? ← try (do let b ← IO.FS.readBinFile path; pure (some b))
                    catch e => do IO.eprintln s!"checkpoint unreadable: {e}"; pure none
      match bytes? with
      | none => pure (.error s!"cannot read checkpoint {path}")
      | some bytes =>
        match Ckpt.parse bytes with
        | .error e => pure (.error s!"{path}: {e}")
        | .ok c =>
          match Ckpt.validate D (Ckpt.designHash D) c with
          | some e => pure (.error s!"{path}: {e}")
          | none =>
            let note := if c.exts.isEmpty then "" else s!", {c.exts.size} ext section(s) not read"
            pure (.ok (CkptStateM.toStateM c,
              s!"checkpoint {path}, {bytes.size} bytes, cycle {c.cycle}{note}"))
  | .reset         => pure (.ok (initStateOf D, "generated: initStateOf (the power-on state)"))
  | .rampSynthetic => pure (.ok (rampStateOf D, "generated: ramp pattern (synthetic)"))
  | .narrowControl => pure (.ok (narrowStateOf D, "generated: narrow control (synthetic)"))

/-- The absolute cycle a run starts at: a checkpoint's own, else 0. -/
def startOf (im : InitMode) : IO Nat := do
  match im with
  | .ckptFile path =>
      match Ckpt.parse (← IO.FS.readBinFile path) with
      | .ok c    => pure c.cycle
      | .error _ => pure 0   -- buildState has already refused it
  | _ => pure 0

def usage : String :=
  "usage: dcert2_memroute (<cert.dcert>|--fixture <name>|--selftest) <stepFuel|auto> \
<workFuel|auto> [cycles] [init] [nocompare] [trace] [outputs] [stim:<file>] [edges:auto|third|indep] \
[no-plain-write] [support-only] [route:fused|var|plain]\n  \
auto: step = max 20000 (4 x (nodes + sources)), work = 2000, one retry at 10x after an out-of-fuel\n  \
fixtures: full mixed twomem lane\n\
  fixtures (negative): romwrite\n  init: init:reset (default) | ramp | narrow | ckpt:<path>\n  \
edges: auto (default: third on one clock, indep on more) | third (cert_trace's edges:third) | indep\n  \
checkpoints: ckpt-at:<cycle>:<file> (repeatable) ckpt-out:<file>\n  \
loop:full: every cycle through runMemSim with the full state check (the default loop keeps the state \
encoded and checks the state once; compare and trace use the full loop)\n  \
share:off: run the residual as specialized (by default its list tails are shared, Projection/ShareTails)"

/-- An edge word: `edges:third` (`cert_trace`'s word for `edgeSchedule`, every clock quiet on
cycles k % 3 = 2; RunnerSupport calls it `EdgeMode.all`), and this driver's own `edges:indep`
and `edges:auto`. The earlier `edges:all` meant `edges:third` here and every clock on every
cycle in `cert_trace`, so it is refused, naming the new word. -/
def parseEdgeWord : String → Except String EdgeMode
  | "third" => .ok .all
  | "indep" => .ok .indep
  | "auto"  => .ok .auto
  | "all"   => .error "edges:all is now edges:third (every clock quiet on cycles k % 3 = 2), \
the word cert_trace uses for that schedule; cert_trace's edges:all fires every clock on every \
cycle, a schedule this driver does not run"
  | w       => .error s!"edges:{w} is not one of auto | third | indep"

def parseCfg (rest : List String) : Except String Cfg := do
  match rest with
  | a :: b :: more =>
    -- `auto` sizes the budget from the certificate (`autoStepFuel`,
    -- `autoWorkFuel`); runRoute fills it in once the certificate is loaded.
    let autoStep := a == "auto"
    let autoWork := b == "auto"
    let some sf := (if autoStep then some 1 else a.toNat?)
      | .error s!"stepFuel {repr a} is not a natural number or `auto`"
    let some wf := (if autoWork then some 1 else b.toNat?)
      | .error s!"workFuel {repr b} is not a natural number or `auto`"
    if sf == 0 || wf == 0 then .error s!"fuel must be positive, got {sf}/{wf}"
    let cycArg := more.head?.getD "8"
    let some cyc := cycArg.toNat? | .error s!"cycles {repr cycArg} is not a natural number"
    if cyc == 0 then
      .error "cycles must be POSITIVE: zero cycles executes no residual step, so an OK would be earned by doing nothing"
    let initArg := (more.drop 1).head?.getD "init:reset"
    let im ← parseInit initArg
    let tl := more.drop 2
    let cmp := !(tl.contains "nocompare")
    let sup := tl.contains "support-only"
    -- `plain-write` is still accepted (it is the default); only the opt-out
    -- changes anything
    let pw  := !(tl.contains "no-plain-write")
    let tr  := tl.contains "trace"
    let op  := tl.contains "outputs"
    let stimTok := tl.find? (fun t => t.startsWith "stim:")
    let prov := match stimTok with
                | some t => s!"PENDING {t}"
                | none   => "generated: mkInputFor (synthetic, width-derived)"
    let edgeTok := tl.findSome? (fun t => (t.dropPrefix? "edges:").map (·.toString))
    let em ← match edgeTok with
      | some w => parseEdgeWord w
      | none => pure .auto
    let route := (tl.findSome? (fun t => (t.dropPrefix? "route:").map (·.toString))).getD "fused"
    unless ["fused", "var", "plain"].contains route do
      .error s!"route:{route} is not one of fused | var | plain"
    -- checkpoints to write
    let ckptOut := tl.findSome? (fun t => (t.dropPrefix? "ckpt-out:").map (·.toString))
    let mut ckptAt : List (Nat × String) := []
    for t in tl do
      if let some r := t.dropPrefix? "ckpt-at:" then
        match r.toString.splitOn ":" with
        | k :: q@(_ :: _) =>
          match k.toNat? with
          | some k => ckptAt := ckptAt ++ [(k, ":".intercalate q)]
          | none => .error s!"ckpt-at: {repr k} is not a number"
        | _ => .error s!"ckpt-at expects <cycle>:<file>, got {repr t}"
    let cfg : Cfg := ⟨sf, wf, cyc, im, cmp, none, [], prov, sup, tr, pw, em, route, op, autoStep, autoWork,
      0, [], none, tl.contains "loop:full", !(tl.contains "share:off")⟩
    .ok { cfg with ckptAt, ckptOut }
  | _ => .error usage

/-! ## Comparison self-test

The comparison is only worth its label if it CATCHES things.  These controls run
without a certificate and report PASS/FAIL for each. -/

def selftest : IO UInt32 := do
  let dw := 8
  let aw := 2
  let words := (List.range (2 ^ aw)).map (fun k => mk_bv dw (Int.ofNat k))
  let good : MemImg := ⟨words, mk_bv dw 0⟩
  let dfltOnly : MemImg := ⟨words, mk_bv dw 7⟩          -- EVERY word equal, default differs
  let shortOne : MemImg := ⟨words.take 2, mk_bv dw 0⟩   -- right words, wrong length
  let oneWord : MemImg := ⟨words.set 1 (mk_bv dw 99), mk_bv dw 0⟩
  let ref : Int → BV := good.denote
  let mut ok := true
  let check (name : String) (got want : Bool) : IO Bool := do
    IO.println s!"  {if got == want then "PASS" else "FAIL"}  {name} (got {got}, want {want})"
    pure (got == want)
  ok := (← check "matching image accepted" (memOneOK aw good ref) true) && ok
  ok := (← check "DEFAULT-ONLY mismatch rejected" (memOneOK aw dfltOnly ref) false) && ok
  ok := (← check "length mismatch rejected" (memOneOK aw shortOne ref) false) && ok
  ok := (← check "single-word mismatch rejected" (memOneOK aw oneWord ref) false) && ok
  -- the status values themselves
  ok := (← check "complete earns credit" (CmpStatus.complete.isComplete) true) && ok
  ok := (← check "disabled does NOT earn credit" (CmpStatus.disabled.isComplete) false) && ok
  ok := (← check "partial does NOT earn credit"
            ((CmpStatus.skipped "x").isComplete) false) && ok
  ok := (← check "meet keeps the weakest (complete+partial)"
            ((CmpStatus.complete.meet (CmpStatus.skipped "x")).isComplete) false) && ok
  ok := (← check "meet keeps a failure"
            ((CmpStatus.complete.meet (CmpStatus.failed "x")).isFailure) true) && ok
  -- an over-large table is SKIPPED and said to be, not silently passed
  let bigD : DesignCert :=
    { sources := #[], nodes := #[], outputs := #[], flops := #[]
    , memories := #[{ aw := 20, dw := 8, nextImg := 0, clock := 0 }]
    , clocks := #[{ name := "clk" }] }
  let bigSt : StateM := { flops := #[], mems := #[good] }
  let bigRef : RuntimeState := { flops := #[], mems := #[ref] }
  ok := (← check "over-large table is SKIPPED, not passed"
            ((memsCheck bigD bigSt bigRef).isComplete) false) && ok
  ok := (← check "...and a cycle meeting it loses complete"
            ((CmpStatus.complete.meet (memsCheck bigD bigSt bigRef)).isComplete) false) && ok
  -- a memory-array length mismatch is a FAILURE, not a skip
  ok := (← check "memory array length mismatch FAILS"
            ((memsCheck bigD { flops := #[], mems := #[] } bigRef).isFailure) true) && ok
  -- the option words: cert_trace's words mean cert_trace's schedule and start; the earlier
  -- words are refused with a message naming the new one, never read with another meaning
  let cfgOf (opts : List String) : Except String Cfg := parseCfg (["auto", "auto"] ++ opts)
  let refusedNaming (opts : List String) (word : String) : Bool :=
    match cfgOf opts with
    | .error e => (e.splitOn word).length > 1
    | .ok _ => false
  ok := (← check "edges:third is edgeSchedule (cert_trace's edges:third)"
            (match cfgOf ["4", "init:reset", "edges:third"] with
             | .ok c => c.edgeMode == .all | .error _ => false) true) && ok
  ok := (← check "edges:indep and the default auto are kept"
            (match cfgOf ["4", "init:reset", "edges:indep"], cfgOf ["4"] with
             | .ok c, .ok d => c.edgeMode == .indep && d.edgeMode == .auto | _, _ => false) true) && ok
  ok := (← check "edges:all is refused, naming edges:third"
            (refusedNaming ["4", "init:reset", "edges:all"] "edges:third") true) && ok
  ok := (← check "init:reset is the default start"
            (match cfgOf ["4", "init:reset"], cfgOf ["4"] with
             | .ok c, .ok d => (match c.init, d.init with | .reset, .reset => true | _, _ => false)
             | _, _ => false) true) && ok
  ok := (← check "zero is refused, naming init:reset" (refusedNaming ["4", "zero"] "init:reset") true) && ok
  ok := (← check "a memstate start file is refused, naming ckpt:"
            (refusedNaming ["4", "file:x.memstate"] "ckpt:<file>") true) && ok
  ok := (← check "cert_trace's init:zero is refused" ((cfgOf ["4", "init:zero"]).toOption.isNone) true) && ok
  -- the stimulus reader takes `in` and `at` lines and refuses anything else (a `react` line)
  ok := (← check "stim v1 with in and at lines is read"
            ((parseStim "stim v1\nin 0 8 3\nat 2 in 1 4 5").toOption.isSome) true) && ok
  ok := (← check "a react line is refused"
            ((parseStim "stim v1\nin 0 8 3\nreact 3 8").toOption.isNone) true) && ok
  IO.println s!"  SELFTEST {if ok then "PASS" else "FAIL"}"
  pure (if ok then 0 else 1)

def main (args : List String) : IO UInt32 := do
  match args with
  | "--selftest" :: _ => selftest
  | "--fixture" :: name :: rest =>
      match fixtureOf name, parseCfg rest with
      | none, _ => do IO.eprintln s!"REFUSING: unknown fixture {repr name}"; return 2
      | _, .error e => do IO.eprintln s!"REFUSING: {e}"; return 2
      | some D, .ok cfg0 => do
          match ← resolveStim cfg0 with
          | .error e => do IO.eprintln s!"REFUSING: {e}"; return 2
          | .ok cfg =>
            match ← buildState D cfg.init with
            | .error e => do IO.eprintln s!"REFUSING: {e}"; return 2
            | .ok (st0, prov) =>
                let cfg := { cfg with start := ← startOf cfg.init }
                runRoute s!"fixture:{name}" "n/a(in-repo fixture)" D cfg st0 prov
  | path :: rest =>
      match parseCfg rest with
      | .error e => do IO.eprintln s!"REFUSING: {e}"; return 2
      | .ok cfg0 => do
          let cfgE ← resolveStim cfg0
          let .ok cfg := cfgE | do
            IO.eprintln s!"REFUSING: {match cfgE with | .error e => e | _ => ""}"
            return 2
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
              match ← buildState D cfg.init with
              | .error e => do IO.eprintln s!"REFUSING: {e}"; return 2
              | .ok (st0, prov) =>
                  let cfg := { cfg with start := ← startOf cfg.init }
                  runRoute path (toString (fnv1a bytes)) D cfg st0 prov
  | _ => do IO.eprintln usage; return 2
