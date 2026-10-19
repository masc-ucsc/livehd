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

STATE FILE, exactly.  A supplied file is READ and used, but two things in it
are the driver's and must not be mistaken for the caller's:

  * EVERY numeric field goes through `mk_bv`, so a value is stored reduced
    modulo `2 ^ width`.  `BV(8, 300)` supplied as a word is stored as
    `mk_bv 8 300`, i.e. 44.  The file supplies the number; `mk_bv` decides the
    representation.
  * A `mem` line FILLS the whole `2 ^ aw`-word table with its declared default,
    and `word` lines then overwrite the addresses they name.  An address the
    file does not mention is the DEFAULT, not something the file said.  The
    default's own width is the memory's `dw`, and `setWord` stores a word at
    that same width.

So "supplied" means the words and defaults the file names are the ones used,
not that the table contains only what the file wrote.

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
import LeanSemanticPrimitives.Projection.HardwareMemWriteAdmit
import LeanSemanticPrimitives.Projection.MemVariantSim
import LeanSemanticPrimitives.Projection.MemFused
import LeanSemanticPrimitives.Projection.Proto.RunnerSupport
import LeanSemanticPrimitives.Compiler.CertIORoute   -- DCERT2 + DCERT1, routed by magic (Mathlib-free)
import LeanSemanticPrimitives.Compiler.RunAdmission   -- C3 edges / C4 reset polarity

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
has no slot type: the memory route implements Op_MemWriteBE and Op_MemRead, \
and every scalar operator OpSupported admits"

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
  /-- `initStateOf`: zero tables, reset-valued flops.  A SYNTHETIC control and a
  legal-state witness; NOT a startup contract. -/
  | zeroWitness
  /-- An internally generated SYNTHETIC pattern (word `k` holds `k + 1`).  It is
  a control, not a loader: nothing about it comes from the caller. -/
  | rampSynthetic
  /-- A deliberately illegal synthetic control: flop values narrower than
  declared.  The gate must refuse it. -/
  | narrowControl
  /-- The real thing: initial flops and memory images READ FROM A FILE, carried
  through the production codec unchanged. -/
  | suppliedFile (path : String)

def parseInit (s : String) : Option InitMode :=
  if s == "zero" then some .zeroWitness
  else if s == "ramp" then some .rampSynthetic
  else if s == "narrow" then some .narrowControl
  else if s.startsWith "file:" then some (.suppliedFile (s.drop 5).toString)
  else none

def InitMode.label : InitMode → String
  | .zeroWitness    => "zero (SYNTHETIC control: zero tables, reset-valued flops; \
a legal-state witness, NOT an RTL startup contract)"
  | .rampSynthetic  => "ramp (SYNTHETIC control: an internally generated pattern, \
NOT caller-supplied contents)"
  | .narrowControl  => "narrow (SYNTHETIC negative control: under-width flops)"
  | .suppliedFile p => s!"file:{p} (SUPPLIED initial state, read from the file)"

/-! ### The supplied-state format

```
memstate v1
flop <width> <value>
mem  <aw> <dw> <default-value>
word <addr> <value>
```

`word` applies to the most recent `mem`.  Anything else -- a bad keyword, a
non-numeric field, a `word` before any `mem`, an address outside the declared
table -- is a REFUSAL naming the line.  Nothing is defaulted and nothing is
invented; a field that is not in the file is not supplied. -/

structure SuppliedState where
  flops : Array BV
  mems  : Array MemImg
  /-- the file asked for the design's DECLARED reset values instead of listing
  every flop.  That is synthetic and is labelled as such; it is still the
  file's explicit request, not something the driver decided. -/
  flopsFromReset : Bool := false

def emptyTable (aw dw : Nat) (dflt : Int) : MemImg :=
  ⟨List.replicate (2 ^ aw) (mk_bv dw dflt), mk_bv dw dflt⟩

def setWord (m : MemImg) (a : Nat) (v : Int) : MemImg :=
  { m with words := m.words.set a (mk_bv m.dflt.width v) }

def parseState (txt : String) : Except String SuppliedState := do
  let lines := (txt.splitOn "\n").map (fun l => (l.splitOn "\r").headD "")
  let body := lines.filter (fun l => l != "" && !l.startsWith "#")
  match body with
  | [] => .error "empty state file"
  | hdr :: rest =>
    if hdr != "memstate v1" then
      .error s!"line 1: expected header \"memstate v1\", got {repr hdr}"
    else
      let mut fs : Array BV := #[]
      let mut ms : Array MemImg := #[]
      let mut fromReset := false
      let mut n := 1
      for l in rest do
        n := n + 1
        let ws := (l.splitOn " ").filter (fun w => w != "")
        match ws with
        | ["flops", "reset"] => fromReset := true
        | ["flop", w, v] =>
            let some w' := w.toNat? | throw s!"line {n}: flop width {repr w} is not a number"
            let some v' := v.toInt? | throw s!"line {n}: flop value {repr v} is not an integer"
            fs := fs.push (mk_bv w' v')
        | ["mem", aw, dw, d] =>
            let some aw' := aw.toNat? | throw s!"line {n}: aw {repr aw} is not a number"
            let some dw' := dw.toNat? | throw s!"line {n}: dw {repr dw} is not a number"
            let some d'  := d.toInt?  | throw s!"line {n}: default {repr d} is not an integer"
            if aw' > awEnumLimit then
              throw s!"line {n}: aw {aw'} exceeds this driver's table limit {awEnumLimit}"
            ms := ms.push (emptyTable aw' dw' d')
        | ["word", a, v] =>
            let some a' := a.toNat? | throw s!"line {n}: address {repr a} is not a number"
            let some v' := v.toInt? | throw s!"line {n}: word value {repr v} is not an integer"
            if ms.size = 0 then throw s!"line {n}: `word` before any `mem`"
            else
              let k := ms.size - 1
              let m := ms[k]!
              if a' >= m.words.length then
                throw s!"line {n}: address {a'} is outside the {m.words.length}-word table"
              ms := ms.set! k (setWord m a' v')
        | _ => throw s!"line {n}: unrecognised {repr l}"
      if fromReset && fs.size > 0 then
        .error "a file may use `flops reset` or list `flop` lines, not both"
      else .ok ⟨fs, ms, fromReset⟩

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


/-! ### Supplied stimulus

`mkInputFor` generates a deterministic pattern from each input's WIDTH.  That
exercises the route, but it cannot drive a design that needs a particular input
to do anything -- a CPU held in reset never writes its register file, and
reporting "0 words differ" for it is honest but uninformative.  A stimulus file
supplies the vector instead:

```
stim v1
in <ordinal> <width> <value>
```

Every cycle uses it; unlisted ordinals keep their declared width and value 0.
It is SUPPLIED, like the state file, and the report says so. -/

/-- A stimulus is three things: constants, per-cycle overrides (`at`), and
reactive inputs (`react`).  A core that needs a RESET PULSE cannot be driven by
constants alone, which is what `at` is for. -/
def parseStim (txt : String) :
    Except String (List (Nat × Nat × Int) × List (Nat × Nat)
                   × List (Nat × Nat × Nat × Int)) := do
  let body := ((txt.splitOn "\n").map (fun l => (l.splitOn "\r").headD "")).filter
                (fun l => l != "" && !l.startsWith "#")
  match body with
  | [] => .error "empty stimulus file"
  | hdr :: rest =>
    if hdr != "stim v1" then .error s!"line 1: expected \"stim v1\", got {repr hdr}"
    else
      let mut out : List (Nat × Nat × Int) := []
      let mut rx : List (Nat × Nat) := []
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
        | ["react", i, o] =>
            let some i' := i.toNat? | throw s!"line {n}: input ordinal {repr i} is not a number"
            let some o' := o.toNat? | throw s!"line {n}: output ordinal {repr o} is not a number"
            rx := rx ++ [(i', o')]
        | ["in", i, w, v] =>
            let some i' := i.toNat? | throw s!"line {n}: ordinal {repr i} is not a number"
            let some w' := w.toNat? | throw s!"line {n}: width {repr w} is not a number"
            let some v' := v.toInt? | throw s!"line {n}: value {repr v} is not an integer"
            out := out ++ [(i', w', v')]
        | _ => throw s!"line {n}: unrecognised {repr l}"
      .ok (out, rx, ats)

/-- The supplied vector, with any `react` input taken from the PREVIOUS cycle's
output -- the handshake a core needs, and the only part of the stimulus that is
not a constant.  On the first cycle there is no previous output, so a reactive
input is 0. -/
def stimInput (D : DesignCert) (spec : List (Nat × Nat × Int))
    (rx : List (Nat × Nat)) (ats : List (Nat × Nat × Nat × Int))
    (prevOuts : Array BV) (cyc : Nat) : RuntimeInput :=
  let n := max (maxInputIdx D) ((spec.map (fun t => t.1 + 1)).foldl max 0)
  (List.range n).toArray.map (fun i =>
    let w := match spec.find? (fun t => t.1 == i) with
             | some (_, w, _) => w
             | none           => inputWidth D i
    -- a per-cycle override wins over everything; then a reactive input; then
    -- the constant; then zero at the declared width
    match ats.find? (fun t => t.1 == cyc && t.2.1 == i) with
    | some (_, _, w', v) => mk_bv w' v
    | none =>
    match rx.find? (fun t => t.1 == i) with
    | some (_, o) => bv_resize w (prevOuts[o]?.getD (mk_bv w 0))
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
  react    : List (Nat × Nat) := []
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
  keeps the one-clock schedule for a one-clock design and fires domains
  INDEPENDENTLY when the certificate declares more than one clock. -/
  edgeMode : EdgeMode := .auto
  /-- which memory interpreter is specialized: `fused` (default:
  `MemFused.projectMemFDesignAt`, `env0` inlined and the node fold fused into
  its readers, `runMemSim(W)F_correct`), `var` (`MemVariant`, `env0` inlined
  only, `runMemSim(W)V_correct`) or `plain` (`projectMemDesignAt`,
  `runMemSim(W)_correct`). All three conclusions are the same. -/
  route : String := "fused"

def say (s : String) : IO Unit := IO.println s

def runRoute (label : String) (certHash : String) (D : DesignCert) (cfg : Cfg)
    (st0 : StateM) (stateProv : String) : IO UInt32 := do
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
{cfg.stepFuel}/{cfg.workFuel}"
  (← IO.getStdout).flush
  let t2 ← IO.monoMsNow
  -- the bundle is honest by `mkMemSimOf(W)_wfF` / `_wfV` / `_wf` and runs
  -- correctly by `runMemSim(W)F_correct` / `V_correct` / `_correct` (fused / var /
  -- plain). `runMemSim`, `mkMemSimOf(W)` and `checkResidual` are the same
  -- functions on all three.
  let res ← IO.lazyPure (fun _ =>
    if cfg.route == "plain" then projectMemDesignAt cfg.stepFuel cfg.workFuel D
    else if cfg.route == "var" then MemVariant.projectMemVarDesignAt cfg.stepFuel cfg.workFuel D
    else MemFused.projectMemFDesignAt cfg.stepFuel cfg.workFuel D)
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
      (← IO.getStdout).flush
      let t5 ← IO.monoMsNow
      let mut st := st0
      let mut tags : List String := []
      let mut outs : List (Array BV) := []
      let mut flops : List (Array BV) := []
      let mut prevOuts : Array BV := #[]
      let mut bad := false
      let mut policyBad := false
      let mut cmp : CmpStatus := if cfg.compare then .complete else .disabled
      for k in List.range cfg.cycles do
        let e := scheduleEdges cfg.edgeMode D k
        if let some rc := (← RunAdmission.refuseEdges? D k e) then return rc
        let i := match cfg.stim with
                 | some spec => stimInput D spec cfg.react cfg.ats prevOuts k
                 | none      => mkInputFor D k
        if runtimeFullOKB D i st e != true then
          tags := "policy-refused" :: tags
          policyBad := true
        else
          match runMemSim sim e i st with
          | none   => tags := "no-step" :: tags; bad := true
          | some r =>
              tags := "ok" :: tags
              outs := r.outputs :: outs
              prevOuts := r.outputs
              flops := r.nextState.flops :: flops
              if cfg.compare then
                let ref := Compiler.interpretDesign D e i (StateM.denote st)
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
        | .ok (spec, rx, ats) =>
            let prov := s!"SUPPLIED file {path}, {bytes.size} bytes, fnv1a {fnv1a bytes}, {spec.length} constant(s), {ats.length} per-cycle override(s), {rx.length} reactive (from the previous cycle's outputs)"
            pure (.ok { cfg with stim := some spec, react := rx, ats := ats, stimProv := prov })

/-- Build the initial state.  Synthetic modes are generated here and SAID to be
synthetic; a supplied file is read and used, with the two caveats the header
states: every value is `mk_bv`-normalised, and unnamed addresses hold the
declared default rather than anything the file wrote. -/
def buildState (D : DesignCert) (im : InitMode) : IO (Except String (StateM × String)) := do
  match im with
  | .zeroWitness   => pure (.ok (initStateOf D, "generated: initStateOf (synthetic)"))
  | .rampSynthetic => pure (.ok (rampStateOf D, "generated: ramp pattern (synthetic)"))
  | .narrowControl => pure (.ok (narrowStateOf D, "generated: narrow control (synthetic)"))
  | .suppliedFile path => do
      let bytes? ← try (do let b ← IO.FS.readBinFile path; pure (some b))
                    catch e => do IO.eprintln s!"state file unreadable: {e}"; pure none
      match bytes? with
      | none => pure (.error s!"cannot read state file {path}")
      | some bytes =>
        match String.fromUTF8? bytes with
        | none => pure (.error s!"{path}: not valid UTF-8")
        | some txt =>
        match parseState txt with
        | .error e => pure (.error s!"{path}: {e}")
        | .ok ss   =>
            let fl := if ss.flopsFromReset then initFlopsOf D else ss.flops
            let note := if ss.flopsFromReset then
              " ; flops: the design's DECLARED RESET VALUES, as the file requested (synthetic, not supplied)" else " ; flops: supplied"
            pure (.ok (⟨fl, ss.mems⟩,
              s!"supplied file {path}, {bytes.size} bytes, fnv1a {fnv1a bytes}{note}"))

def usage : String :=
  "usage: dcert2_memroute (<cert.dcert>|--fixture <name>|--selftest) <stepFuel> \
<workFuel> [cycles] [init] [nocompare] [trace] [no-plain-write] [route:fused|var|plain]\n  fixtures: full mixed twomem lane\n\
  fixtures (negative): romwrite\n  init: zero | ramp | narrow | file:<path>"

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
    let tl := more.drop 2
    let cmp := !(tl.contains "nocompare")
    let sup := tl.contains "support-only"
    -- `plain-write` is still accepted (it is the default); only the opt-out
    -- changes anything
    let pw  := !(tl.contains "no-plain-write")
    let tr  := tl.contains "trace"
    let stimTok := tl.find? (fun t => t.startsWith "stim:")
    let prov := match stimTok with
                | some t => s!"PENDING {t}"
                | none   => "generated: mkInputFor (synthetic, width-derived)"
    let edgeTok := tl.findSome? (fun t => (t.dropPrefix? "edges:").map (·.toString))
    let some em := (edgeTok.map EdgeMode.parse?).getD (some .auto)
      | .error s!"edges:{edgeTok.getD ""} is not one of auto | all | indep"
    let route := (tl.findSome? (fun t => (t.dropPrefix? "route:").map (·.toString))).getD "fused"
    unless ["fused", "var", "plain"].contains route do
      .error s!"route:{route} is not one of fused | var | plain"
    .ok ⟨sf, wf, cyc, im, cmp, none, [], [], prov, sup, tr, pw, em, route⟩
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
  -- the state parser refuses malformed input rather than defaulting it
  let bad : List (String × String) :=
    [ ("no header",        "flop 8 1")
    , ("bad keyword",      "memstate v1\nfrob 1 2")
    , ("non-numeric",      "memstate v1\nflop eight 1")
    , ("word before mem",  "memstate v1\nword 0 1")
    , ("address past end", "memstate v1\nmem 1 8 0\nword 9 1") ]
  for (name, txt) in bad do
    let got := (parseState txt).toOption.isNone
    ok := (← check s!"state parser refuses: {name}" got true) && ok
  let goodTxt := "memstate v1\nflop 8 3\nmem 1 8 5\nword 0 1\nword 1 2"
  ok := (← check "state parser accepts a well-formed file"
            ((parseState goodTxt).toOption.isSome) true) && ok
  match parseState goodTxt with
  | .ok ss =>
      ok := (← check "supplied NONZERO default is kept"
                ((ss.mems[0]!).dflt == mk_bv 8 5) true) && ok
      ok := (← check "supplied words are kept"
                ((ss.mems[0]!).get 1 == mk_bv 8 2) true) && ok
      ok := (← check "supplied flop is kept" (ss.flops[0]! == mk_bv 8 3) true) && ok
      ok := (← check "out-of-range read returns the SUPPLIED default"
                ((ss.mems[0]!).get 99 == mk_bv 8 5) true) && ok
  | .error _ => ok := false
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
            | .ok (st0, prov) => runRoute s!"fixture:{name}" "n/a(in-repo fixture)" D cfg st0 prov
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
              | .ok (st0, prov) => runRoute path (toString (fnv1a bytes)) D cfg st0 prov
  | _ => do IO.eprintln usage; return 2
