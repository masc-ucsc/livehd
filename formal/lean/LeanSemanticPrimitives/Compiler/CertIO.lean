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

  * the DESERIALISER joins the trusted base.  `parseCert` is `partial` and
    unverified; if it mis-reads a file it yields a different `DesignCert`, and
    every theorem here is then true of a design nobody asked about.  This is the
    same class of trust already extended to `pass_lean.cpp`'s transcription of
    LGraph into the certificate — it is one more link on that existing chain,
    not a new kind of assumption;
  * the per-design `#print axioms` gate is lost, because there is no per-design
    theorem left to print axioms for.  It moves here, to `runChecked_correct`,
    and is checked ONCE for the whole library.

What is still proved, for every design and with no per-design obligation: that
the residual program the compiler builds agrees with `interpretDesign` on the
certificate that was actually loaded.
-/
import LeanSemanticPrimitives.Compiler.CompileDesign

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

  DCERT1
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
  <nFlops>     then per flop:    width din hasEn en hasRst rst resetValue activeLow
  <nMemories>  then per memory:  aw dw nextImg

`hasEn`/`hasRst` keep the arity fixed so the reader never branches on shape.
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

/-- Serialise to a handle rather than a `String`: `++` on a 14 MB certificate is
quadratic, and this is the reference implementation the round-trip test uses. -/
def writeCert (h : IO.FS.Handle) (D : DesignCert) : IO Unit := do
  h.putStr "DCERT1\n"
  h.putStr s!"{D.sources.size}\n"
  for s in D.sources do
    match s with
    | .input idx w              => h.putStr s!"0 {idx} {w}\n"
    | .const w v                => h.putStr s!"1 {w} {v}\n"
    | .flopQ idx w              => h.putStr s!"2 {idx} {w}\n"
    | .flopQAsync idx w ri rv a => h.putStr s!"3 {idx} {w} {ri} {rv} {if a then 1 else 0}\n"
    | .memImg idx aw dw         => h.putStr s!"4 {idx} {aw} {dw}\n"
    | .memConst aw dw cs        =>
        h.putStr s!"5 {aw} {dw} {cs.size}"
        for v in cs do h.putStr s!" {v}"
        h.putStr "\n"
  h.putStr s!"{D.nodes.size}\n"
  for n in D.nodes do
    let (c, arg) := opCode n.op
    h.putStr s!"{c} {arg} {n.width} {n.deps.size}"
    for d in n.deps do h.putStr s!" {d}"
    h.putStr s!" {n.origin}\n"
  h.putStr s!"{D.outputs.size}\n"
  for o in D.outputs do h.putStr s!"{o.slot} {o.width}\n"
  h.putStr s!"{D.flops.size}\n"
  for f in D.flops do
    let (he, e) := match f.enable   with | some x => (1, x) | none => (0, 0)
    let (hr, r) := match f.resetPin with | some x => (1, x) | none => (0, 0)
    h.putStr s!"{f.width} {f.din} {he} {e} {hr} {r} {f.resetValue} {if f.resetActiveLow then 1 else 0}\n"
  h.putStr s!"{D.memories.size}\n"
  for m in D.memories do h.putStr s!"{m.aw} {m.dw} {m.nextImg}\n"

--------------------------------------------------------------------------------
-- 3.  Reader  — UNVERIFIED, and therefore trusted
--------------------------------------------------------------------------------

@[inline] def isWs (c : UInt8) : Bool :=
  c == 32 || c == 10 || c == 13 || c == 9

@[inline] def isDigit (c : UInt8) : Bool := 48 ≤ c && c ≤ 57

partial def skipWs (b : ByteArray) (i : Nat) : Nat :=
  if i < b.size && isWs b[i]! then skipWs b (i + 1) else i

/-- Read one non-negative integer.  Returns `(value, next)`; `next == start`
signals "no digits here", which every caller treats as malformed input. -/
partial def readNatAux (b : ByteArray) (i acc : Nat) : Nat × Nat :=
  if i < b.size && isDigit b[i]! then
    readNatAux b (i + 1) (acc * 10 + (b[i]!.toNat - 48))
  else (acc, i)

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
partial def readMany {α : Type} (f : ByteArray → Nat → Option (α × Nat))
    (b : ByteArray) (i : Nat) (n : Nat) (acc : Array α) : Option (Array α × Nat) :=
  if n == 0 then some (acc, i)
  else match f b i with
    | some (v, j) => readMany f b j (n - 1) (acc.push v)
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
  some ({ width := w, din := din,
          enable := if he != 0 then some e else none,
          resetPin := if hr != 0 then some r else none,
          resetValue := rv, resetActiveLow := al != 0 }, i)

def readMemory (b : ByteArray) (i : Nat) : Option (MemoryDesc × Nat) := do
  let (aw, i) ← readNat b i
  let (dw, i) ← readNat b i
  let (nx, i) ← readNat b i
  some ({ aw := aw, dw := dw, nextImg := nx }, i)

/-- Check the magic without materialising a `String` for the whole buffer. -/
def hasMagic (b : ByteArray) : Bool :=
  let m := "DCERT1".toUTF8
  b.size ≥ m.size && (List.range m.size).all fun k => b[k]! == m[k]!

/-- Parse a whole certificate.  TRUSTED: a mis-parse silently yields a different
design, and the theorems below are then about that design instead. -/
def parseCert (b : ByteArray) : Except String DesignCert :=
  if !hasMagic b then .error "bad magic: expected DCERT1" else
  match go b with
  | some D => .ok D
  | none   => .error "malformed certificate"
where
  go (b : ByteArray) : Option DesignCert := do
    let i := 6                                   -- past "DCERT1"
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
    -- Nothing but whitespace may follow: a truncated or padded file is an error,
    -- not a silently shorter design.
    if skipWs b i != b.size then none else
    some { sources := srcs, nodes := nds, outputs := outs,
           flops := fls, memories := mems }

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
def runChecked (D : DesignCert) (inp : RuntimeInput) (st : RuntimeState) :
    Except String RuntimeResult :=
  if compilesOk D then
    .ok (compileAndRun D inp st)
  else
    .error "the verified compiler refuses this certificate"

/-- Everything `runChecked` accepts, it computes correctly — for EVERY design.

This is the whole of what a generated file's `_step_correct` used to say, proved
once here instead of once per design.  Note the hypothesis is `runChecked … =
.ok r` rather than `compilesOk D`: a caller that never inspects the certificate
still gets the guarantee, because the `.ok` it received is itself the witness. -/
theorem runChecked_correct (D : DesignCert) (inp : RuntimeInput) (st : RuntimeState)
    (r : RuntimeResult) (h : runChecked D inp st = .ok r) :
    r = interpretDesign D inp st := by
  unfold runChecked at h
  split at h
  · rename_i hok
    have hc := compileAndRun_correct D hok inp st
    have hr : compileAndRun D inp st = r := by simpa using h
    rw [← hr]; exact hc
  · exact absurd h (by simp)

/-- Load and run in one step. -/
def loadAndRun (p : System.FilePath) (inp : RuntimeInput) (st : RuntimeState) :
    IO RuntimeResult := do
  let D ← loadCert p
  match runChecked D inp st with
  | .ok r    => pure r
  | .error e => throw (IO.userError e)

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
def runCycles (D : DesignCert) (inp : RuntimeInput) (st0 : RuntimeState) (n : Nat) :
    Except String RuntimeResult :=
  let rec go (k : Nat) (st : RuntimeState) (last : Option RuntimeResult) :
      Except String RuntimeResult :=
    match k with
    | 0     => match last with
               | some r => .ok r
               | none   => .error "zero cycles requested"
    | k + 1 => match runChecked D inp st with
               | .ok r    => go k r.nextState (some r)
               | .error e => .error e
  go n st0 none

end CertIO
end Compiler
