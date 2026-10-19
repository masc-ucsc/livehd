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
import LeanSemanticPrimitives.Compiler.CertIOCodec
import LeanSemanticPrimitives.Compiler.CompileDesign
import LeanSemanticPrimitives.Compiler.DirectCheck

namespace Compiler
namespace CertIO

-- The wire format, writer, total reader, checked admission and stimulus
-- helpers live in `Compiler/CertIOCodec.lean` (Mathlib-free, same namespace).
-- This module keeps what needs the compiler: the verified entry point.

/-- The UTF-8 inverse the reader needs.

Not a pre-existing named lemma: `String.fromUTF8?` is a dependent `if` over
`IsValidUTF8`, and a `String`'s own `isValidUTF8` discharges it. -/
theorem clockName_utf8_inverse (s : String) :
    String.fromUTF8? s.toUTF8 = some s := by
  unfold String.fromUTF8?
  simp only [String.toUTF8_eq_toByteArray, dif_pos s.isValidUTF8]
  congr 1

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
