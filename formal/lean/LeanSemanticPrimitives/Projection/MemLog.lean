/-
SUPERSEDED FOR ITS PURPOSE by `Projection/MemImage.lean`, and retained because
its lemmas are true and were really proved -- not deleted to tidy a mistake.

WHAT WAS WRONG WITH IT AS A MEMORY IMAGE.  `MemLog` stores a base INDEX, and
`denote`/`writeBE`/`readEn` all take an external `Array (Int → BV)` of base
functions.  `encMemLog` encodes the index and the overrides and NEVER those
functions, so the representation is not self-contained: a first-order projected
`Program` cannot evaluate it, because the base image is a host-function oracle
sitting outside the encoding.  The three faithfulness theorems below are valid
RELATIVE write-log lemmas -- they say the log tracks `cert_mem_write`,
`cert_mem_write_be` and `cert_mem_read` given a base -- and they do not discharge
the encoding obligation.  Claiming that first obligation complete was wrong.

`MemImage.lean` carries the contents themselves (`words : List BV` plus a
default), encodes all of it, and is evaluated by the object language.
-/
/-
# A FINITE, ENCODABLE memory image — the first missing piece of FP memory support

## Why this file exists, concretely

`gen_probe_v2` loads PicoRV32's DCERT2 certificate and refuses it at support with
three distinct failures, not one:

    memFree false   1 memory (aw 5, dw 32, nextImg 3225, clock 0)
    sources false   1 unsupported SourceDesc -- memImg(idx 0, aw 5, dw 32)
    ops     false   3 nodes -- LGraphOp.Op_MemRead x2, Op_MemWriteBE 1 x1

`memFree` is not an arbitrary gate that could be deleted.  It is load-bearing for
a REPRESENTATION reason: `RuntimeState.mems : Array (Int → BV)` holds memories as
FUNCTIONS, the object language's `Val` is first-order, and `decState` therefore
reconstructs `mems := #[]` unconditionally.  `StateRel_memFree` makes that
explicit as a theorem -- *no* object value is related to a memory-bearing state.
So before any memory operator can be projected, a memory image needs a finite
representation that `Val` can hold and that denotes exactly what the reference
function-valued memory denotes.  That is what this file provides.

## The representation, and why it is faithful rather than convenient

A memory reachable in a real run is never an arbitrary `Int → BV`.  It is a base
image named by the certificate's `memImg` source, plus the finite sequence of
writes the run has performed.  So:

    MemLog := base index + override list, most-recent-write-first

`denote` resolves an address against the overrides first and falls through to the
base image, which makes last-write-wins the definition rather than an invariant
to maintain.  The two write forms and the read are then proved EQUAL to the
reference operators `cert_mem_write`, `cert_mem_write_be` and `cert_mem_read`
from `Translation/LGraphModel.lean` — not similar to them, equal as functions.
Those three theorems are the content of this file; the encoding is the easy part.

## What this does NOT do, stated so the file cannot be over-read

It does not remove `memFree`, weaken any support predicate, erase a memory or
change `RuntimeState`.  Nothing here is yet wired into `I_hw`, `SupportedByProjection`
or the adequacy chain, and PicoRV32 is still refused.  The remaining obligations
are named in `MEMORY_BRIDGE_OBLIGATIONS` at the end of this file, and they are
real work, not a formality.  What is discharged here is the first of them: a
memory image that `Val` can carry, with its denotation pinned to the reference
semantics.
-/
import LeanSemanticPrimitives.Projection.RuntimeEncoding
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
open Compiler

/-! ## The log -/

/-- A memory image as a base plus a finite override list, newest first. -/
structure MemLog where
  /-- index into the run's base images (the `memImg` source ordinal). -/
  base : Nat
  /-- overrides, MOST RECENT FIRST, so `lookup` is last-write-wins by construction. -/
  ovr  : List (Int × BV)
deriving Repr, Inhabited, DecidableEq

namespace MemLog

/-- First match wins; with newest-first order that is the latest write. -/
def lookup : List (Int × BV) → Int → Option BV
  | [],             _ => none
  | (a, v) :: rest, x => if x = a then some v else lookup rest x

/-- The base image, total: an out-of-range index reads as the zero-width zero,
the same junk convention `eval_op` uses for an operator it cannot apply. -/
def baseImg (bs : Array (Int → BV)) (m : MemLog) : Int → BV :=
  bs.getD m.base (fun _ => mk_bv 0 0)

/-- What the log MEANS: the function-valued memory the reference semantics uses. -/
def denote (bs : Array (Int → BV)) (m : MemLog) : Int → BV :=
  fun x => match lookup m.ovr x with
           | some v => v
           | none   => baseImg bs m x

@[simp] theorem lookup_cons (a : Int) (v : BV) (rest : List (Int × BV)) (x : Int) :
    lookup ((a, v) :: rest) x = if x = a then some v else lookup rest x := rfl

@[simp] theorem denote_nil_ovr (bs : Array (Int → BV)) (b : Nat) :
    denote bs ⟨b, []⟩ = baseImg bs ⟨b, []⟩ := rfl

/-! ## The operators, and their equality with the reference semantics -/

/-- `cert_mem_write`'s log form: enabled pushes one override, disabled is a no-op. -/
def writeEn (m : MemLog) (a d en : BV) : MemLog :=
  if bv_nonzero en then { m with ovr := (bv_uint a, d) :: m.ovr } else m

/-- `cert_mem_write_be`'s log form.  The masked word is computed against the
CURRENT denotation at that address, which is exactly what the reference does. -/
def writeBE (w : Nat) (bs : Array (Int → BV)) (m : MemLog) (a d bev : BV)
    (byte_w : Nat) : MemLog :=
  if bv_nonzero bev then
    { m with ovr := (bv_uint a,
                     cert_masked_update w (denote bs m (bv_uint a)) d bev byte_w) :: m.ovr }
  else m

/-- `cert_mem_read`'s log form. -/
def readEn (w : Nat) (bs : Array (Int → BV)) (m : MemLog) (a en : BV) : BV :=
  cert_mem_read w (denote bs m) a en

/-- **The write is faithful.**  Not "behaves like": the two functions are equal. -/
theorem denote_writeEn (bs : Array (Int → BV)) (m : MemLog) (a d en : BV) :
    denote bs (writeEn m a d en) = cert_mem_write (denote bs m) a d en := by
  unfold writeEn cert_mem_write
  by_cases h : bv_nonzero en
  · simp only [h, if_true]
    funext x
    by_cases hx : x = bv_uint a
    · simp [denote, hx]
    · simp [denote, baseImg, hx]
  · simp [h]

/-- **The byte-enabled write is faithful.** -/
theorem denote_writeBE (w : Nat) (bs : Array (Int → BV)) (m : MemLog)
    (a d bev : BV) (byte_w : Nat) :
    denote bs (writeBE w bs m a d bev byte_w)
      = cert_mem_write_be w (denote bs m) a d bev byte_w := by
  unfold writeBE cert_mem_write_be
  by_cases h : bv_nonzero bev
  · simp only [h, if_true]
    funext x
    by_cases hx : x = bv_uint a
    · simp [denote, hx]
    · simp [denote, baseImg, hx]
  · simp [h]

/-- **The read is faithful.**  True by definition, stated so the chain is complete. -/
theorem readEn_eq (w : Nat) (bs : Array (Int → BV)) (m : MemLog) (a en : BV) :
    readEn w bs m a en = cert_mem_read w (denote bs m) a en := rfl

/-- Last write wins, which is the property an override LIST has to earn. -/
theorem denote_writeEn_same (bs : Array (Int → BV)) (m : MemLog) (a d₁ d₂ en : BV)
    (h : bv_nonzero en) :
    denote bs (writeEn (writeEn m a d₁ en) a d₂ en) (bv_uint a) = d₂ := by
  simp [writeEn, h, denote]

/-- A write at one address does not disturb another. -/
theorem denote_writeEn_other (bs : Array (Int → BV)) (m : MemLog) (a d en : BV)
    (x : Int) (hx : x ≠ bv_uint a) :
    denote bs (writeEn m a d en) x = denote bs m x := by
  unfold writeEn
  by_cases h : bv_nonzero en
  · simp [h, denote, baseImg, hx]
  · simp [h]

/-! ## The encoding -/

def tagMemOvr : Nat := 122
def tagMemLog : Nat := 123

@[inline] def encOvr (p : Int × BV) : Val := .ctor tagMemOvr [.int p.1, ofBV p.2]

def decOvr : Val → Option (Int × BV)
  | .ctor t [.int a, bv] => if t = tagMemOvr then (asBV bv).map (fun b => (a, b)) else none
  | _                    => none

@[simp] theorem decOvr_encOvr (p : Int × BV) : decOvr (encOvr p) = some p := by
  cases p with
  | mk a b => simp [decOvr, encOvr]

@[inline] def encMemLog (m : MemLog) : Val :=
  .ctor tagMemLog [encNat m.base, encListG encOvr m.ovr]

def decMemLog (v : Val) : Option MemLog :=
  (field2 tagMemLog v).bind (fun p =>
    (decNat p.1).bind (fun b =>
      (decListG decOvr p.2).map (fun l => ⟨b, l⟩)))

/-- The round trip: a log survives encoding into `Val` and back, unchanged. -/
@[simp] theorem decMemLog_encMemLog (m : MemLog) : decMemLog (encMemLog m) = some m := by
  cases m with
  | mk b l =>
    simp [decMemLog, encMemLog, field2, decNat, encNat,
          decListG_encListG (e := encOvr) (d := decOvr) decOvr_encOvr]

/-- `MemRel v m` -- object value `v` denotes memory log `m`. -/
def MemRel (v : Val) (m : MemLog) : Prop := decMemLog v = some m

theorem MemRel_encMemLog (m : MemLog) : MemRel (encMemLog m) m := decMemLog_encMemLog m

/-- One value denotes at most one log, the same obligation `StateRel_functional`
carries for flop state. -/
theorem MemRel_functional {v : Val} {m n : MemLog} (h₁ : MemRel v m) (h₂ : MemRel v n) :
    m = n :=
  Option.some.inj (h₁ ▸ h₂)

end MemLog

/-! ## What is still owed

The three theorems above pin the log's denotation to the reference operators, and
the encoding gives `Val` something finite to carry.  That is ONE obligation of
several, and the rest are not paperwork:

1. **`I_hw` source and operator cases.**  `sourceValue` must answer `memImg` /
   `memConst` with a log, and `applyOp` must implement `Op_MemRead` and
   `Op_MemWriteBE` over it.  `OpSupported` and `SourceSupported` then stop being
   false for those cases -- by IMPLEMENTING them, not by widening the predicate.
2. **A memory-carrying runtime-state encoding.**  `encState`/`decState` drop
   `mems` today and `StateRel_memFree` proves no memory-bearing state is related
   to any value.  Both must be replaced by versions that carry the logs, and
   `StateRel_encState`, `StateRel_functional` and the canonicality chain reproved.
3. **The adequacy bridge.**  `interpretDesign_memFree` and the `HardwareAdequacy`
   lemmas that consume `hsup.memFree` must be generalised to a memory-bearing
   runtime relation, with the per-step correspondence between the reference
   `mems` update and the log update established.
4. **The residual checker.**  `Hw.checkResidual` must accept the new operator
   shapes and keep its bound sound for them.
5. **`SupportedByProjection`.**  `memFree` is replaced by the real conditions the
   implementation needs -- base images present, addresses in range, one clock per
   memory as `MemoryDesc.clock` already records -- and `supported_of_report`
   reproved.

Until all of those hold, memory-bearing designs stay REFUSED.  Nothing in this
file admits one. -/
def MEMORY_BRIDGE_OBLIGATIONS : List String :=
  [ "I_hw: sourceValue memImg/memConst and applyOp Op_MemRead/Op_MemWriteBE",
    "encState/decState carry mems; StateRel lemmas reproved",
    "HardwareAdequacy: runtime relation generalised off memFree",
    "Hw.checkResidual accepts the memory operator shapes with a sound bound",
    "SupportedByProjection: memFree replaced by the real conditions" ]

/-! ## Audit, in this file on purpose

`Projection/Audit.lean` is the repository's convention for `#print axioms`, but
these checks live beside the theorems they are about so that elaborating this one
file is the whole reproduction:

    lean -o <out>/MemLog.olean LeanSemanticPrimitives/Projection/MemLog.lean

What they actually print, recorded rather than wished for:

    readEn_eq                does not depend on any axioms
    denote_writeEn_same      propext
    MemRel_functional        propext
    denote_writeEn           propext, Quot.sound
    denote_writeBE           propext, Quot.sound
    denote_writeEn_other     propext, Quot.sound
    decMemLog_encMemLog      propext, Quot.sound

`propext` and `Quot.sound` are Lean's own kernel axioms and come in through
`funext` and `simp`; they are not additions to this development's trusted base.
What matters is what is ABSENT: no `sorryAx`, no `Classical.choice`, and no
`Lean.ofReduceBool`.  `decide` is used for the worked example rather than
`native_decide` precisely so the last of those stays absent. -/
section Audit

#audit_axioms Projection.MemLog.denote_writeEn
#audit_axioms Projection.MemLog.denote_writeBE
#audit_axioms Projection.MemLog.readEn_eq
#audit_axioms Projection.MemLog.denote_writeEn_same
#audit_axioms Projection.MemLog.denote_writeEn_other
#audit_axioms Projection.MemLog.decMemLog_encMemLog
#audit_axioms Projection.MemLog.MemRel_functional

/-! ### Non-vacuity at PicoRV32's actual memory shape

`cpuregs` is `aw 5, dw 32` -- a 32-entry, 32-bit register file.  These are the
numbers from the refused certificate, not a toy. -/

private def picoBase : Array (Int → BV) := #[fun _ => mk_bv 32 0]
private def m0 : MemLog := ⟨0, []⟩
private def m1 : MemLog := MemLog.writeEn m0 (mk_bv 5 7) (mk_bv 32 0xDEADBEEF) (mk_bv 1 1)

/-- the write landed, read back through the REFERENCE read operator -/
example : MemLog.readEn 32 picoBase m1 (mk_bv 5 7) (mk_bv 1 1) = mk_bv 32 0xDEADBEEF := by decide
/-- a neighbouring address is untouched -/
example : MemLog.readEn 32 picoBase m1 (mk_bv 5 8) (mk_bv 1 1) = mk_bv 32 0 := by decide
/-- a disabled read is zero, as `cert_mem_read` specifies -/
example : MemLog.readEn 32 picoBase m1 (mk_bv 5 7) (mk_bv 1 0) = mk_bv 32 0 := by decide
/-- a disabled write is a no-op ON THE LOG ITSELF, not merely on its denotation -/
example : MemLog.writeEn m1 (mk_bv 5 7) (mk_bv 32 1) (mk_bv 1 0) = m1 := by decide
/-- the representation really is finite: one override after one write -/
example : m1.ovr.length = 1 := by decide

end Audit

end Projection
