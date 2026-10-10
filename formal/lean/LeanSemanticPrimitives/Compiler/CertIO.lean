/-
# Runtime certificate loading — a design costs zero Lean elaboration

`compileAndRun_correct` is stated `∀ D` (`CompileDesign.lean:358`).  Nothing in
it requires `D` to be a Lean *constant*, so nothing requires a design to be
elaborated at all: a certificate can be read from a file at run time and the
same theorem still covers the answer.

That matters because elaborating the certificate literal is the ENTIRE
per-design cost.  Measured in `DIRECTION4_INCREMENTAL.md`: `pmp_gate` spends
+20.0 s of a 29.8 s file on the literal, and `issue_stage_gate`'s literal alone
costs 318.83 s against 311.48 s for the whole file.  Every other per-design
obligation — the `_step` def, `native_decide`, `_step_correct`, `_residual` —
is within noise of zero.

## What this buys, and what it costs

`runChecked` performs the `compilesOk` test at RUN time rather than build time,
and `runChecked_correct` says any `.ok` answer equals `interpretDesign`.  Both
are proved once, for every design, so a new design adds no Lean work whatsoever.

The price is precise and worth stating plainly:

  * the PRODUCER of the bytes joins the trusted base.  The deserialiser itself
    no longer does: `parseCert` is TOTAL, and `CertIORoundTrip` proves
    `parseCert (renderCert D) = .ok D` for every `DesignCert` with no side
    condition, so the Lean reader and the Lean writer cannot disagree.  What
    remains trusted is that the bytes on disk are the ones some trusted writer
    produced for the design in question -- the same class of trust already
    extended to `pass_lean.cpp`'s transcription of LGraph into the certificate.
    It is one more link on that existing chain, not a new kind of assumption,
    and the link is narrower than it was: a mis-READ is now excluded, only a
    mis-WRITE is not.  Note also that parsing is not admission -- a certificate
    that round-trips may still be semantically ill-formed, which is what
    `certWfError` / `readCertChecked` are for;
  * the per-design `#print axioms` gate is lost, because there is no per-design
    theorem left to print axioms for.  It moves here, to `runChecked_correct`,
    and is checked ONCE for the whole library.

What is still proved, for every design and with no per-design obligation: that
the residual program the compiler builds agrees with `interpretDesign` on the
certificate that was actually loaded.
-/
import LeanSemanticPrimitives.Compiler.CompileDesign
import LeanSemanticPrimitives.Compiler.DirectCheck

namespace Compiler
namespace CertIO

--------------------------------------------------------------------------------
-- 1.  The wire format
--------------------------------------------------------------------------------

/-
Whitespace-separated integers, length-prefixed everywhere, fixed arity per
record.  Deliberately dull: `pass_lean.cpp` already walks exactly these fields
to print the Lean literal, so emitting this instead is a change of punctuation
in the printer, not a change of structure.

  DCERT2
  <nSources>   then per source, a tag and its fields:
     0 idx width                                   input
     1 width value                                 const
     2 idx width                                   flopQ
     3 idx width resetInput resetValue activeLow   flopQAsync
     4 idx aw dw                                   memImg
     5 aw dw n v0 .. v(n-1)                        memConst
  <nNodes>     then per node:
     opCode opArg width nDeps d0 .. origin
  <nOutputs>   then per output:  slot width
  <nFlops>     then per flop:
     width din hasEn en hasRst rst resetValue activeLow clock asyncReset
  <nMemories>  then per memory:  aw dw nextImg clock
  <nClocks>    then per clock:   <nameBytes> <name>

`hasEn`/`hasRst` keep the arity fixed so the reader never branches on shape.

`clock` is an ORDINAL into the clocks section, `asyncReset` a 0/1 flag; both are
written unconditionally, so a flop record is ten numbers whatever its metadata.
A clock record is the only field that is not a number: a byte count, ONE space,
then exactly that many bytes of UTF-8 -- not whitespace-delimited, because a
clock name may contain a space or a newline, and not re-encoded, because
`String.fromUTF8? s.toUTF8 = some s` is what makes the round trip hold for every
name without an ASCII side condition.

The clocks section is LAST because it is the section this version ADDED.  That
is the only sense in which the order is inherited: the records above are NOT
byte-identical to DCERT1, since the flop record gained `clock` and `asyncReset`
and the memory record gained `clock`.  A DCERT1 stream is refused by `hasMagic`
rather than defaulted (see `magicBytes`), and would be unreadable here anyway.
-/

/-- Operator tag and its payload.  The payload slot is always present and is `0`
for the operators that carry none, so every node record has the same arity. -/
def opCode : LGraphOp → Nat × Int
  | .Op_Const c      => (0,  c)
  | .Op_Sum n        => (1,  Int.ofNat n)
  | .Op_Sub          => (2,  0)
  | .Op_Mult         => (3,  0)
  | .Op_Div          => (4,  0)
  | .Op_UDiv         => (5,  0)
  | .Op_SDiv         => (6,  0)
  | .Op_And          => (7,  0)
  | .Op_Or           => (8,  0)
  | .Op_Xor          => (9,  0)
  | .Op_Ror          => (10, 0)
  | .Op_Not          => (11, 0)
  | .Op_LT           => (12, 0)
  | .Op_GT           => (13, 0)
  | .Op_ULT          => (14, 0)
  | .Op_UGT          => (15, 0)
  | .Op_SLT          => (16, 0)
  | .Op_SGT          => (17, 0)
  | .Op_EQ           => (18, 0)
  | .Op_SHL          => (19, 0)
  | .Op_SRA          => (20, 0)
  | .Op_MuxBool      => (21, 0)
  | .Op_MuxN         => (22, 0)
  | .Op_Sext         => (23, 0)
  | .Op_GetMask      => (24, 0)
  | .Op_SetMask      => (25, 0)
  | .Op_MemRead      => (26, 0)
  | .Op_MemWrite     => (27, 0)
  | .Op_MemWriteBE b => (28, Int.ofNat b)

def opOfCode (c : Nat) (arg : Int) : Option LGraphOp :=
  match c with
  | 0  => some (.Op_Const arg)
  | 1  => some (.Op_Sum arg.toNat)
  | 2  => some .Op_Sub      | 3  => some .Op_Mult
  | 4  => some .Op_Div      | 5  => some .Op_UDiv
  | 6  => some .Op_SDiv     | 7  => some .Op_And
  | 8  => some .Op_Or       | 9  => some .Op_Xor
  | 10 => some .Op_Ror      | 11 => some .Op_Not
  | 12 => some .Op_LT       | 13 => some .Op_GT
  | 14 => some .Op_ULT      | 15 => some .Op_UGT
  | 16 => some .Op_SLT      | 17 => some .Op_SGT
  | 18 => some .Op_EQ       | 19 => some .Op_SHL
  | 20 => some .Op_SRA      | 21 => some .Op_MuxBool
  | 22 => some .Op_MuxN     | 23 => some .Op_Sext
  | 24 => some .Op_GetMask  | 25 => some .Op_SetMask
  | 26 => some .Op_MemRead  | 27 => some .Op_MemWrite
  | 28 => some (.Op_MemWriteBE arg.toNat)
  | _  => none

--------------------------------------------------------------------------------
-- 2.  Writer
--------------------------------------------------------------------------------

/-! The renderer is PURE and the IO does nothing but hand its bytes to a handle.

The round-trip theorem has to name the bytes, so they cannot be produced by an
`IO` action; and the decimal rendering is spelled out here rather than borrowed
from `Nat.repr`, so the proof reasons about one recursion of its own instead of
about the internals of `Nat.toDigits`.

`natBytes` agrees with `Nat.repr` on every input -- same digits, no leading
zeros -- which is what keeps the bytes identical to those the previous
`h.putStr s!"{n}"` writer produced, and is checked against the stored
certificates by the differential harness rather than assumed. -/

@[inline] def chSpace : UInt8 := 32
@[inline] def chNL    : UInt8 := 10
@[inline] def chMinus : UInt8 := 45

/-- Decimal digits, most significant first.  `natBytes 0 = ['0']`. -/
def natBytes (n : Nat) : List UInt8 :=
  if n < 10 then [UInt8.ofNat (48 + n)]
  else natBytes (n / 10) ++ [UInt8.ofNat (48 + n % 10)]
termination_by n
decreasing_by exact Nat.div_lt_self (by omega) (by omega)

/-- `Int.negSucc n` is `-(n+1)`, so a negative renders as '-' then the digits of
its magnitude.  Zero is never signed. -/
def intBytes : Int → List UInt8
  | .ofNat n   => natBytes n
  | .negSucc n => chMinus :: natBytes (n + 1)

@[inline] def spc (l : List UInt8) : List UInt8 := chSpace :: l

-- Each record is a BODY followed by its newline.  A reader stops at the end of
-- the body -- the newline is eaten by the next reader's `skipWs` -- so keeping
-- the two separable is what lets the round-trip lemmas say "consumes exactly
-- this chunk" instead of doing arithmetic with a trailing byte.

def srcBody : SourceDesc → List UInt8
  | .input idx w              => natBytes 0 ++ spc (natBytes idx) ++ spc (natBytes w)
  | .const w v                => natBytes 1 ++ spc (natBytes w) ++ spc (intBytes v)
  | .flopQ idx w              => natBytes 2 ++ spc (natBytes idx) ++ spc (natBytes w)
  | .flopQAsync idx w ri rv a =>
      natBytes 3 ++ spc (natBytes idx) ++ spc (natBytes w) ++ spc (natBytes ri)
        ++ spc (intBytes rv) ++ spc (natBytes (if a then 1 else 0))
  | .memImg idx aw dw         =>
      natBytes 4 ++ spc (natBytes idx) ++ spc (natBytes aw) ++ spc (natBytes dw)
  | .memConst aw dw cs        =>
      natBytes 5 ++ spc (natBytes aw) ++ spc (natBytes dw) ++ spc (natBytes cs.size)
        ++ (cs.toList.map (fun v => spc (intBytes v))).flatten

def srcBytes (s : SourceDesc) : List UInt8 := srcBody s ++ [chNL]

def nodeBody (n : DenseNodeCert) : List UInt8 :=
  let (c, arg) := opCode n.op
  natBytes c ++ spc (intBytes arg) ++ spc (natBytes n.width) ++ spc (natBytes n.deps.size)
    ++ (n.deps.toList.map (fun d => spc (natBytes d))).flatten
    ++ spc (natBytes n.origin)

def nodeBytes (n : DenseNodeCert) : List UInt8 := nodeBody n ++ [chNL]

def outBody (o : OutputDesc) : List UInt8 :=
  natBytes o.slot ++ spc (natBytes o.width)

def outBytes (o : OutputDesc) : List UInt8 := outBody o ++ [chNL]

/-- A clock's name, LENGTH-PREFIXED: its UTF-8 byte count, ONE space, then
exactly those bytes.

A `String` cannot ride raw in a space-and-newline framed record: a name
containing either would break the framing silently, and the codec theorem
promises EVERY `DesignCert`. With a count in front, the reader takes exactly
that many bytes without scanning, so no byte sequence in a name can be mistaken
for structure.

I briefly replaced this with a code-point encoding, on the view that the
round-trip needed `String.fromUTF8? s.toUTF8 = some s` and that the toolchain
had no such lemma. The second half was true and the conclusion was wrong:
`String.fromUTF8?` is a dependent `if` over `IsValidUTF8`, and `s.isValidUTF8`
supplies the hypothesis, so `unfold`, `dif_pos` and `congr 1` prove it outright
(`clockName_utf8_inverse` below, standard axioms only). Searching for a NAMED
lemma is not the same as trying to prove it, and the redesign it prompted was
unnecessary -- so the readable encoding stays. -/
def clkBody (c : ClockDesc) : List UInt8 :=
  let nm := c.name.toUTF8.toList
  natBytes nm.length ++ spc nm

def clkBytes (c : ClockDesc) : List UInt8 := clkBody c ++ [chNL]

def flopBody (f : FlopDesc) : List UInt8 :=
  let (he, e) := match f.enable   with | some x => (1, x) | none => (0, 0)
  let (hr, r) := match f.resetPin with | some x => (1, x) | none => (0, 0)
  natBytes f.width ++ spc (natBytes f.din) ++ spc (natBytes he) ++ spc (natBytes e)
    ++ spc (natBytes hr) ++ spc (natBytes r) ++ spc (intBytes f.resetValue)
    ++ spc (natBytes (if f.resetActiveLow then 1 else 0))
    -- DCERT2: the clock ordinal this flop commits on, and whether its reset is
    -- asynchronous. DCERT1 carried neither.
    ++ spc (natBytes f.clock)
    ++ spc (natBytes (if f.asyncReset then 1 else 0))

def flopBytes (f : FlopDesc) : List UInt8 := flopBody f ++ [chNL]

def memBody (m : MemoryDesc) : List UInt8 :=
  natBytes m.aw ++ spc (natBytes m.dw) ++ spc (natBytes m.nextImg)
    -- DCERT2: the clock every committing port of this memory is on.
    ++ spc (natBytes m.clock)

def memBytes (m : MemoryDesc) : List UInt8 := memBody m ++ [chNL]

/-- A length-prefixed section: the count on its own line, then one record each. -/
@[inline] def section' {α : Type} (f : α → List UInt8) (xs : Array α) : List UInt8 :=
  natBytes xs.size ++ [chNL] ++ (xs.toList.map f).flatten

/-- "DCERT2\n" -- spelled as bytes so the proof never unfolds `String.toUTF8`.

VERSION 2. DCERT1 carried no `clocks` section, no `FlopDesc.clock`, no
`asyncReset` and no `MemoryDesc.clock`. Reading a DCERT1 stream as a one-clock
design would be an assumption about the DESIGN, not a default of the record, so
the version is bumped rather than extended in place and DCERT1 is refused by
name. -/
def magicBytes : List UInt8 := [68, 67, 69, 82, 84, 50, 10]

/-- The certificate as bytes.  This is the thing `parseCert` is proved to invert. -/
def certBytes (D : DesignCert) : List UInt8 :=
  magicBytes
    ++ section' srcBytes  D.sources
    ++ section' nodeBytes D.nodes
    ++ section' outBytes  D.outputs
    ++ section' flopBytes D.flops
    ++ section' memBytes  D.memories
    ++ section' clkBytes  D.clocks

def renderCert (D : DesignCert) : ByteArray := ⟨(certBytes D).toArray⟩

/-- IO and nothing else.  Every byte decision lives in `renderCert`. -/
def writeCert (h : IO.FS.Handle) (D : DesignCert) : IO Unit :=
  h.write (renderCert D)

--------------------------------------------------------------------------------
-- 3.  Reader  — TOTAL, and the subject of the round-trip obligation
--
-- This heading said "UNVERIFIED, and therefore trusted" while the code beneath
-- it already said "TOTAL, not `partial`" and carried a round-trip theorem. The
-- reader is a total function whose equations reduce, which is what lets
-- anything be stated about it at all. What it is NOT is an admission check:
-- it reconstructs the record faithfully and judges nothing. Every semantic
-- refusal lives in `certWfError` / `readCertChecked` below.
--------------------------------------------------------------------------------

@[inline] def isWs (c : UInt8) : Bool :=
  c == 32 || c == 10 || c == 13 || c == 9

@[inline] def isDigit (c : UInt8) : Bool := 48 ≤ c && c ≤ 57

-- TOTAL, not `partial`.  A `partial` definition is opaque to the equation
-- compiler, so nothing about `parseCert` could be stated, let alone proved; the
-- round-trip theorem below needs these to reduce.  Each walk only ever moves the
-- cursor forward inside the buffer, so `b.size - i` is the measure, and the
-- dependent `if h :` keeps the index in range without the `!` getter's panic
-- branch -- which would otherwise show up in every proof obligation.
def skipWs (b : ByteArray) (i : Nat) : Nat :=
  if h : i < b.size then
    if isWs b[i] then skipWs b (i + 1) else i
  else i
termination_by b.size - i
decreasing_by omega

/-- Read one non-negative integer.  Returns `(value, next)`; `next == start`
signals "no digits here", which every caller treats as malformed input. -/
def readNatAux (b : ByteArray) (i acc : Nat) : Nat × Nat :=
  if h : i < b.size then
    if isDigit b[i] then
      readNatAux b (i + 1) (acc * 10 + (b[i].toNat - 48))
    else (acc, i)
  else (acc, i)
termination_by b.size - i
decreasing_by omega

def readNat (b : ByteArray) (i0 : Nat) : Option (Nat × Nat) :=
  let i := skipWs b i0
  let (v, j) := readNatAux b i 0
  if j == i then none else some (v, j)

def readInt (b : ByteArray) (i0 : Nat) : Option (Int × Nat) :=
  let i := skipWs b i0
  if i < b.size && b[i]! == 45 then
    match readNat b (i + 1) with
    | some (v, j) => some (-(Int.ofNat v), j)
    | none        => none
  else
    match readNat b i with
    | some (v, j) => some (Int.ofNat v, j)
    | none        => none

/-- Read `n` values with `f`, accumulating into an array.  `Array.push` is O(1)
amortised in compiled code — the quadratic `Array.push` behaviour that bit this
project is a KERNEL reduction effect, and nothing here runs in the kernel. -/
-- Structural on the COUNT, so no termination proof is needed and the equations
-- are definitional: `readMany f b i 0 acc` and `readMany f b i (n+1) acc` both
-- reduce by `rfl`, which is what makes the round-trip induction go through.
def readMany {α : Type} (f : ByteArray → Nat → Option (α × Nat))
    (b : ByteArray) : Nat → Nat → Array α → Option (Array α × Nat)
  | i, 0,     acc => some (acc, i)
  | i, n + 1, acc =>
      match f b i with
      | some (v, j) => readMany f b j n (acc.push v)
      | none        => none

def readSource (b : ByteArray) (i : Nat) : Option (SourceDesc × Nat) := do
  let (tag, i) ← readNat b i
  match tag with
  | 0 => let (idx, i) ← readNat b i; let (w, i) ← readNat b i
         some (.input idx w, i)
  | 1 => let (w, i) ← readNat b i; let (v, i) ← readInt b i
         some (.const w v, i)
  | 2 => let (idx, i) ← readNat b i; let (w, i) ← readNat b i
         some (.flopQ idx w, i)
  | 3 => let (idx, i) ← readNat b i; let (w, i) ← readNat b i
         let (ri, i) ← readNat b i; let (rv, i) ← readInt b i
         let (al, i) ← readNat b i
         some (.flopQAsync idx w ri rv (al != 0), i)
  | 4 => let (idx, i) ← readNat b i; let (aw, i) ← readNat b i
         let (dw, i) ← readNat b i
         some (.memImg idx aw dw, i)
  | 5 => let (aw, i) ← readNat b i; let (dw, i) ← readNat b i
         let (n, i)  ← readNat b i
         let (cs, i) ← readMany readInt b i n #[]
         some (.memConst aw dw cs, i)
  | _ => none

def readNode (b : ByteArray) (i : Nat) : Option (DenseNodeCert × Nat) := do
  let (c, i)    ← readNat b i
  let (arg, i)  ← readInt b i
  let (w, i)    ← readNat b i
  let (nd, i)   ← readNat b i
  let (deps, i) ← readMany readNat b i nd #[]
  let (org, i)  ← readNat b i
  let op        ← opOfCode c arg
  some ({ op := op, width := w, deps := deps, origin := org }, i)

def readOutput (b : ByteArray) (i : Nat) : Option (OutputDesc × Nat) := do
  let (s, i) ← readNat b i
  let (w, i) ← readNat b i
  some ({ slot := s, width := w }, i)

def readFlop (b : ByteArray) (i : Nat) : Option (FlopDesc × Nat) := do
  let (w, i)  ← readNat b i
  let (din, i) ← readNat b i
  let (he, i) ← readNat b i
  let (e, i)  ← readNat b i
  let (hr, i) ← readNat b i
  let (r, i)  ← readNat b i
  let (rv, i) ← readInt b i
  let (al, i) ← readNat b i
  let (clk, i) ← readNat b i
  let (aR, i)  ← readNat b i
  some ({ width := w, din := din,
          enable := if he != 0 then some e else none,
          resetPin := if hr != 0 then some r else none,
          resetValue := rv, resetActiveLow := al != 0,
          clock := clk, asyncReset := aR != 0 }, i)

def readMemory (b : ByteArray) (i : Nat) : Option (MemoryDesc × Nat) := do
  let (aw, i) ← readNat b i
  let (dw, i) ← readNat b i
  let (nx, i) ← readNat b i
  let (ck, i) ← readNat b i
  some ({ aw := aw, dw := dw, nextImg := nx, clock := ck }, i)

/-- The UTF-8 inverse the reader needs.

Not a pre-existing named lemma: `String.fromUTF8?` is a dependent `if` over
`IsValidUTF8`, and a `String`'s own `isValidUTF8` discharges it. -/
theorem clockName_utf8_inverse (s : String) :
    String.fromUTF8? s.toUTF8 = some s := by
  unfold String.fromUTF8?
  simp only [String.toUTF8_eq_toByteArray, dif_pos s.isValidUTF8]
  congr 1

/-- A length-prefixed name: the count, ONE separating space -- CHECKED, not
assumed -- then exactly that many bytes taken without scanning. -/
def readClock (b : ByteArray) (i : Nat) : Option (ClockDesc × Nat) := do
  let (n, i) ← readNat b i
  if i ≥ b.size then none
  else if b[i]! != chSpace then none
  else
    let i := i + 1
    if i + n > b.size then none else
      match String.fromUTF8? (b.extract i (i + n)) with
      | some nm => some ({ name := nm }, i + n)
      | none    => none

/-- Check the magic without materialising a `String` for the whole buffer. -/
def hasMagic (b : ByteArray) : Bool :=
  let m := "DCERT2".toUTF8
  b.size ≥ m.size && (List.range m.size).all fun k => b[k]! == m[k]!

/-- Parse a whole certificate.  The CODEC: a faithful decoding that judges
nothing.  Every semantic refusal lives in `certWfError` / `readCertChecked`. -/
def parseCert (b : ByteArray) : Except String DesignCert :=
  if !hasMagic b then .error "bad magic: expected DCERT2" else
  match go b with
  | some D => .ok D
  | none   => .error "malformed certificate"
where
  go (b : ByteArray) : Option DesignCert := do
    -- Past the six bytes of "DCERT2"; the magic's trailing newline is consumed
    -- as leading whitespace by the first `readNat`.
    let i := 6
    let (ns, i)   ← readNat b i
    let (srcs, i) ← readMany readSource b i ns #[]
    let (nn, i)   ← readNat b i
    let (nds, i)  ← readMany readNode b i nn #[]
    let (no, i)   ← readNat b i
    let (outs, i) ← readMany readOutput b i no #[]
    let (nf, i)   ← readNat b i
    let (fls, i)  ← readMany readFlop b i nf #[]
    let (nm, i)   ← readNat b i
    let (mems, i) ← readMany readMemory b i nm #[]
    let (nc, i)   ← readNat b i
    let (clks, i) ← readMany readClock b i nc #[]
    -- Nothing but whitespace may follow: a truncated or padded file is an error,
    -- not a silently shorter design.
    if skipWs b i != b.size then none else
    some { sources := srcs, nodes := nds, outputs := outs,
           flops := fls, memories := mems, clocks := clks }

--------------------------------------------------------------------------------
-- 5.  Checked admission  — SEPARATE from the codec, deliberately
--------------------------------------------------------------------------------

/-- Why this certificate is not admissible, or `none`.

THE CODEC AND THE CHECK ARE DIFFERENT PROMISES, and conflating them is an
inconsistent API. `parseCert (renderCert D) = .ok D` is asked of EVERY
`DesignCert`; a certificate whose flop names a clock ordinal outside `clocks`
is a perfectly good inhabitant of the type, so it is itself a counterexample
the moment the parser rejects it.

So `parseCert` is a faithful encoding and judges nothing, and every semantic
refusal lives here. -/
def certWfError (D : DesignCert) : Option String :=
  let nclk := D.clocks.size
  if nclk == 0 then
    some "no clock domains: a certificate must declare at least one"
  else
    match D.flops.findIdx? (fun f => f.clock ≥ nclk) with
    | some k => some s!"flop {k} commits on clock ordinal {D.flops[k]!.clock}, \
                        outside the {nclk} declared"
    | none =>
      match D.memories.findIdx? (fun m => m.clock ≥ nclk) with
      | some k => some s!"memory {k} is on clock ordinal {D.memories[k]!.clock}, \
                          outside the {nclk} declared"
      | none =>
        let slots := D.sources.size + D.nodes.size
        match D.outputs.findIdx? (fun o => o.slot ≥ slots) with
        | some k => some s!"output {k} reads slot {D.outputs[k]!.slot}, \
                            outside the {slots} that exist"
        | none   => none

/-- Parse, then admit. A refusal here is a SEMANTIC verdict and says so; it is
never reported as a parse failure, and it never silently repairs a field. -/
def readCertChecked (b : ByteArray) : Except String DesignCert :=
  match parseCert b with
  | .error e => .error s!"parse: {e}"
  | .ok D    =>
    match certWfError D with
    | some w => .error s!"refused: {w}"
    | none   => .ok D

def loadCert (p : System.FilePath) : IO DesignCert := do
  let b ← IO.FS.readBinFile p
  match parseCert b with
  | .ok D    => pure D
  | .error e => throw (IO.userError s!"{p}: {e}")

--------------------------------------------------------------------------------
-- 4.  The verified entry point
--------------------------------------------------------------------------------

/-- Run a certificate that arrived at RUN time.

The `compilesOk` test that a generated file discharges with `native_decide` at
build time happens here instead, as an ordinary boolean evaluation.  It is the
same predicate and the same witness — only its timing moves. -/
def runChecked (D : DesignCert) (e : ClockEdges) (inp : RuntimeInput)
    (st : RuntimeState) : Except String RuntimeResult :=
  if compilesOk D then
    .ok (compileAndRun D e inp st)
  else
    .error "the verified compiler refuses this certificate"

/-- `runChecked` under the recorded legacy contract: every declared clock fires.

NAMED, so the assumption is visible at the call. D4's procedures were written
before edges existed and mean exactly this; they are adapted to say so rather
than having it defaulted underneath them. -/
def runCheckedAllEdges (D : DesignCert) (inp : RuntimeInput) (st : RuntimeState) :
    Except String RuntimeResult :=
  runChecked D (allEdges D) inp st

/-- COMPILE acceptance is not RUNTIME admission.

`compilesOk` says the verified compiler accepts the certificate's structure. It
says nothing about whether a clock ordinal is in range or the edge vector has
the right shape — `certWfError` and `checkRuntime` do. Keeping them separate is
what lets the correctness theorem below be stated for EVERY `e` without the
codec acquiring a semantic opinion. -/
def runAdmitted (D : DesignCert) (e : ClockEdges) (inp : RuntimeInput)
    (st : RuntimeState) : Except String RuntimeResult :=
  -- RUNTIME VALIDATION IS ACTUALLY INVOKED.
  --
  -- The first version checked only `certWfError`, which is about the DESIGN --
  -- clock ordinals in range, slots that exist -- and then called `runChecked`.
  -- Its documentation said "runtime admission" and it never looked at the
  -- arguments. Measured: it accepted an EMPTY edge vector, an OVERLONG one, a
  -- missing input and a missing flop state, all four of which
  -- `Direct.checkRuntime` rejects.
  --
  -- There was no need to invent a predicate: the contract already exists, is
  -- the one `Direct` admits on, and `checkRuntime_sound` relates it to
  -- `RuntimeSemWF`. Using anything else here would have been a second,
  -- divergent policy.
  match certWfError D with
  | some w => .error s!"refused: {w}"
  | none   =>
    match Direct.checkRuntime D e inp st with
    | .error se => .error s!"refused: {se.render}"
    | .ok _     => runChecked D e inp st

/-- Everything `runChecked` accepts, it computes correctly — for EVERY design.

This is the whole of what a generated file's `_step_correct` used to say, proved
once here instead of once per design.  Note the hypothesis is `runChecked … =
.ok r` rather than `compilesOk D`: a caller that never inspects the certificate
still gets the guarantee, because the `.ok` it received is itself the witness. -/
theorem runChecked_correct (D : DesignCert) (e : ClockEdges) (inp : RuntimeInput)
    (st : RuntimeState) (r : RuntimeResult) (h : runChecked D e inp st = .ok r) :
    r = interpretDesign D e inp st := by
  unfold runChecked at h
  split at h
  · rename_i hok
    have hc := compileAndRun_correct D hok e inp st
    have hr : compileAndRun D e inp st = r := by simpa using h
    rw [← hr]; exact hc
  · exact absurd h (by simp)

/-- The same guarantee under the legacy all-edges contract. -/
theorem runCheckedAllEdges_correct (D : DesignCert) (inp : RuntimeInput)
    (st : RuntimeState) (r : RuntimeResult) (h : runCheckedAllEdges D inp st = .ok r) :
    r = interpretDesign D (allEdges D) inp st :=
  runChecked_correct D (allEdges D) inp st r h

/-- Load and run in one step. -/
def loadAndRun (p : System.FilePath) (e : ClockEdges) (inp : RuntimeInput)
    (st : RuntimeState) : IO RuntimeResult := do
  let D ← loadCert p
  match runChecked D e inp st with
  | .ok r    => pure r
  | .error er => throw (IO.userError er)

/-- Load and run under the recorded legacy contract, named as such. -/
def loadAndRunAllEdges (p : System.FilePath) (inp : RuntimeInput)
    (st : RuntimeState) : IO RuntimeResult := do
  let D ← loadCert p
  match runCheckedAllEdges D inp st with
  | .ok r    => pure r
  | .error er => throw (IO.userError er)

--------------------------------------------------------------------------------
-- 5.  Deterministic stimulus, derived from the certificate's own shape
--------------------------------------------------------------------------------

/-
Both sides of the comparison must drive the SAME vectors.  Deriving them from
`D` rather than shipping an input file keeps the test self-contained: if the
parser reconstructs a different design the stimulus differs too, but the outputs
then differ as well, and the round-trip check below pins the format separately.
-/

@[inline] def lcg (x : Nat) : Nat := (x * 1103515245 + 12345) % 2147483648

/-- Reset ports, as `(input ordinal, activeLow)` pairs collected from the
`flopQAsync` sources.

Without this the stimulus is worthless.  Every `btb_gate` flop resets off input
4 active-low; an LCG that happens to drive that input to 0 holds the entire
design in reset, every output and flop reads 0, and the digest then compares
equal no matter what the certificate says.  Measured: with a naive stimulus,
perturbing every node's first dependency AND moving the output to a different
slot both left the digest bit-identical. -/
def resetPorts (D : DesignCert) : Array (Nat × Bool) :=
  D.sources.foldl (init := #[]) fun acc s =>
    match s with
    | .flopQAsync _ _ ri _ al => if acc.any (fun p => p.1 == ri) then acc else acc.push (ri, al)
    | _                       => acc

/-- Widths of the primary inputs, positionally.  `input idx w` may appear in any
order and an index may repeat (several slots can read one port).  Reset ordinals
are included in the sizing even when no `input` record declares them, so the
array is never too short to drive them. -/
def inputWidths (D : DesignCert) : Array Nat :=
  let n0 := D.sources.foldl (init := 0) fun acc s =>
    match s with | .input idx _ => max acc (idx + 1) | _ => acc
  let n := (resetPorts D).foldl (init := n0) fun acc p => max acc (p.1 + 1)
  D.sources.foldl (init := Array.replicate n 1) fun acc s =>
    match s with
    | .input idx w => if idx < acc.size then acc.set! idx w else acc
    | _            => acc

/-- Pseudorandom inputs, with every reset port DE-ASSERTED so the design is
actually running, and flops seeded non-zero so there is something to observe. -/
def stimulusAt (D : DesignCert) (seed : Nat) (rstLevel : Nat) :
    RuntimeInput × RuntimeState :=
  let ws := inputWidths D
  let rp := resetPorts D
  let inp0 := (ws.zipIdx).map fun (w, k) => mk_bv w (Int.ofNat (lcg (seed + k * 7 + 1)))
  let inp := rp.foldl (init := inp0) fun acc (ri, _) =>
    if ri < acc.size then acc.set! ri (mk_bv (acc[ri]!).width (Int.ofNat rstLevel)) else acc
  let fl  := (D.flops.zipIdx).map fun (f, k) => mk_bv f.width (Int.ofNat (lcg (seed + k * 13 + 3)))
  let ms  := (D.memories.zipIdx).map fun (m, k) =>
               (fun (a : Int) => mk_bv m.dw (Int.ofNat (lcg (seed + k * 17 + a.toNat + 5))))
  (inp, { flops := fl, mems := ms })

/-- Default stimulus: active-low resets idle high.

`btb_gate` shows this is not always enough.  Its async source says reset when
input 4 is 0 while every `FlopDesc` says reset when input 4 is 1, so NEITHER
level lets the design run freely and the digest is all zeros at both.  A test
that cannot distinguish a mutated certificate proves nothing, so callers should
compare digests over BOTH levels and require at least one to be sensitive. -/
def stimulus (D : DesignCert) (seed : Nat) : RuntimeInput × RuntimeState :=
  stimulusAt D seed 1

/-- Compact, comparable digest of a run: outputs and next flop state.  Memories
are excluded deliberately — `RuntimeState.mems` is function-valued, so there is
no equality to print; the memory image is observed through outputs instead. -/
def digest (r : RuntimeResult) : String :=
  let o := r.outputs.foldl (init := "") fun a b => a ++ s!"{b.width}:{b.value} "
  let f := r.nextState.flops.foldl (init := "") fun a b => a ++ s!"{b.width}:{b.value} "
  s!"OUT[{r.outputs.size}] {o}| FLOP[{r.nextState.flops.size}] {f}"

/-- Run `n` cycles, threading state, and return the final digest. -/
def runCycles (D : DesignCert) (e : ClockEdges) (inp : RuntimeInput)
    (st0 : RuntimeState) (n : Nat) : Except String RuntimeResult :=
  let rec go (k : Nat) (st : RuntimeState) (last : Option RuntimeResult) :
      Except String RuntimeResult :=
    match k with
    | 0     => match last with
               | some r => .ok r
               | none   => .error "zero cycles requested"
    | k + 1 => match runChecked D e inp st with
               | .ok r    => go k r.nextState (some r)
               | .error er => .error er
  go n st0 none

/-- `runCycles` with every declared clock firing on every cycle. -/
def runCyclesAllEdges (D : DesignCert) (inp : RuntimeInput) (st0 : RuntimeState)
    (n : Nat) : Except String RuntimeResult :=
  runCycles D (allEdges D) inp st0 n

end CertIO
end Compiler
