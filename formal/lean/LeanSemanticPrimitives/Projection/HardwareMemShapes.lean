/-
# Admitted memory shapes, and where `InRange` comes from

The enabled byte-enabled write agrees with the reference only `InRange` (§63.32).
This module says WHICH images may be admitted as write targets and discharges
`InRange` from that admission, rather than carrying it as an unexplained
hypothesis.

## The shape

```
FullTable aw m  :=  m.words.length = 2 ^ aw
```

A MUTABLE memory of address width `aw` must hold the whole `2 ^ aw`-entry table.
Then every `aw`-bit address is in range, because `bv_uint a = a.value % 2 ^ aw`
is non-negative and below `2 ^ aw` — `InRange_of_FullTable`, with no side
condition on the value.

**A ROM is NOT a full table.**  `MemImg.ofMemConst dw contents` is
`contents.size` long, deliberately (it is the bound `sourceValue` itself uses),
and `ofMemConst_FullTable_iff` proves it is a `FullTable aw` exactly when
`contents.size = 2 ^ aw`.  So a read-only `memConst` may not be silently reused
as a mutable `2 ^ aw` table: `romNotFull_example` exhibits the real Pico-shaped
counterexample — a 3-word 32-bit ROM at `aw = 5` — and
`rom_write_outOfRange_refused` shows what the primitive then does: it REFUSES.

## Survival

`writeBE` goes through `setAt`, which is a `List.set`: the word COUNT and the
default are preserved, so `FullTable` survives an admitted write
(`writeBE_FullTable`), as does the default (`writeBE_dflt`).  A DISABLED write
needs no range premise and preserves the image EXACTLY
(`writeBE_disabled_eq`) — not just its shape.

## Pico

`picoImg_FullTable` is the actual PicoRV32 memory shape: `aw = 5`, `dw = 32`,
a 32-word table.  `pico_InRange` discharges `InRange` for every 5-bit address,
and `pico_byte_w_one` records that Pico's `Op_MemWriteBE 1` is BIT-enable
semantics — `byte_w = 1`, so mask bit `i` selects data bit `i` — which is the
`byte_w` the certificate payload carries and `applyOpM` passes through.
-/
import LeanSemanticPrimitives.Projection.HardwareMemSlots
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace MemImg

open Compiler

/-! ## 1. The admitted mutable shape -/

/-- A mutable memory of address width `aw` holds the WHOLE `2 ^ aw` table. -/
def FullTable (aw : Nat) (m : MemImg) : Prop := m.words.length = 2 ^ aw

instance (aw : Nat) (m : MemImg) : Decidable (FullTable aw m) := by
  unfold FullTable; infer_instance

/-- **`InRange` follows from the admitted shape**, for every `aw`-bit address and
with no condition on its value: `bv_uint` is a remainder modulo `2 ^ aw`. -/
private theorem natCast_two_pow : ∀ (n : Nat), ((2 ^ n : Nat) : Int) = (2 : Int) ^ n
  | 0     => rfl
  | k + 1 => by
      rw [Nat.pow_succ, Int.pow_succ, ← natCast_two_pow k]
      simp

theorem InRange_of_FullTable {aw : Nat} {m : MemImg} (h : FullTable aw m)
    {a : BV} (hw : a.width = aw) : InRange m (bv_uint a) := by
  have hpos : (0 : Int) < 2 ^ aw := Hw.two_pow_pos aw
  have hnn : 0 ≤ bv_uint a := Hw.bv_uint_nonneg a
  have hlt : bv_uint a < 2 ^ aw := by
    unfold bv_uint
    rw [hw]
    exact Int.emod_lt_of_pos _ hpos
  refine ⟨hnn, ?_⟩
  rw [h]
  have hc : ((bv_uint a).toNat : Int) < ((2 ^ aw : Nat) : Int) := by
    rw [Int.toNat_of_nonneg hnn, natCast_two_pow]
    exact hlt
  omega

/-- The zero image of an address width IS the admitted shape. -/
@[simp] theorem zeroImg_FullTable (aw dw : Nat) : FullTable aw (zeroImg aw dw) := by
  simp [FullTable, zeroImg]

/-- The same, for an address NO WIDER than the table's: `2 ^ a.width ≤ 2 ^ aw`.
This is the form the node fold needs, because a certificate may legitimately
drive a narrow address into a wider table. -/
theorem InRange_of_FullTable_le {aw : Nat} {m : MemImg} (h : FullTable aw m)
    {a : BV} (hw : a.width ≤ aw) : InRange m (bv_uint a) := by
  have hnn : 0 ≤ bv_uint a := Hw.bv_uint_nonneg a
  have hlt : bv_uint a < 2 ^ a.width := by
    unfold bv_uint
    exact Int.emod_lt_of_pos _ (Hw.two_pow_pos a.width)
  refine ⟨hnn, ?_⟩
  rw [h]
  have hc : ((bv_uint a).toNat : Int) < ((2 ^ a.width : Nat) : Int) := by
    rw [Int.toNat_of_nonneg hnn, natCast_two_pow]
    exact hlt
  have hmono : (2 : Nat) ^ a.width ≤ 2 ^ aw := Nat.pow_le_pow_right (by omega) hw
  omega

/-! ### Denotation does NOT determine geometry

The reason the write-range obligation must be stated over the ACTUAL witness
image and never over "every image with this denotation": two images can denote
exactly the same function and still disagree about which addresses they hold. -/

/-- No words at all, with a 32-bit zero default. -/
def emptyZero : MemImg := ⟨[], mk_bv 32 0⟩

/-- An image whose default and every word are the same value reads that value
at EVERY address, in range or not. -/
private theorem get_const {m : MemImg} {z : BV} (hd : m.dflt = z)
    (hw : ∀ w ∈ m.words, w = z) (x : Int) : get m x = z := by
  unfold get
  split
  · cases h : m.words[x.toNat]? with
    | none   => simpa using hd
    | some v =>
        have hmem : v ∈ m.words := by
          obtain ⟨hlt, hv⟩ := List.getElem?_eq_some_iff.mp h
          exact hv ▸ List.getElem_mem hlt
        simpa using hw v hmem
  · exact hd

/-- It denotes exactly the full 32-word zero table — both are constantly zero. -/
theorem emptyZero_denote : denote emptyZero = denote (zeroImg 5 32) := by
  funext x
  show get emptyZero x = get (zeroImg 5 32) x
  rw [get_const (z := mk_bv 32 0) rfl (by simp [emptyZero]) x,
      get_const (z := mk_bv 32 0) rfl (fun w hw => List.eq_of_mem_replicate hw) x]

/-- …yet it holds NO address. -/
theorem emptyZero_no_inRange (x : Int) : ¬ InRange emptyZero x := by
  intro h
  obtain ⟨-, hlt⟩ := h
  simp [emptyZero] at hlt

/-- **The counterexample, as a theorem.**  Equal denotation, opposite geometry.
Any premise quantified over every image with a given denotation therefore fails
for the empty one, however well shaped the image the interpreter actually
holds. -/
theorem denote_does_not_determine_geometry :
    ∃ m₁ m₂ : MemImg,
      denote m₁ = denote m₂ ∧ FullTable 5 m₂ ∧ ¬ FullTable 5 m₁
      ∧ ∀ x, ¬ InRange m₁ x :=
  ⟨emptyZero, zeroImg 5 32, emptyZero_denote, zeroImg_FullTable 5 32,
   by simp [FullTable, emptyZero], emptyZero_no_inRange⟩

/-! ## 2. A ROM is not a mutable table

`ofMemConst` is `contents.size` long — the bound the reference itself uses — so
it is a full table only when the contents happen to fill the space. -/

theorem ofMemConst_FullTable_iff (aw dw : Nat) (contents : Array Int) :
    FullTable aw (ofMemConst dw contents) ↔ contents.size = 2 ^ aw := by
  simp [FullTable, ofMemConst]

/-- The real shape this matters at: a three-word ROM is NOT a 32-entry table. -/
theorem romNotFull_example : ¬ FullTable 5 (ofMemConst 32 #[11, 22, 33]) := by
  rw [ofMemConst_FullTable_iff]
  decide

/-- …and the primitive does not quietly extend it: an enabled write to an
address the ROM does not hold is REFUSED. -/
theorem rom_write_outOfRange_refused :
    valMemWriteBE 32 (encMemImg (ofMemConst 32 #[11, 22, 33]))
      (ofBV (mk_bv 5 7)) (ofBV (mk_bv 32 0xDEAD)) (ofBV (mk_bv 4 0xF)) 8 = none := by
  exact valMemWriteBE_refuses_outOfRange 32 _ _ _ _ 8 (by decide) (by decide)

/-! ## 3. The shape survives an admitted write

`setAt` is a `List.set`, so the word count and the default are untouched.  A
DISABLED write preserves the image exactly, not merely its shape. -/

@[simp] theorem setAt_length (m : MemImg) (x : Int) (d : BV) :
    (setAt m x d).words.length = m.words.length := by
  unfold setAt; split <;> simp

@[simp] theorem setAt_dflt (m : MemImg) (x : Int) (d : BV) :
    (setAt m x d).dflt = m.dflt := by
  unfold setAt; split <;> rfl

theorem writeBE_length (w : Nat) (m : MemImg) (a d bev : BV) (bw : Nat) :
    (writeBE w m a d bev bw).words.length = m.words.length := by
  unfold writeBE; split <;> simp

@[simp] theorem writeBE_dflt (w : Nat) (m : MemImg) (a d bev : BV) (bw : Nat) :
    (writeBE w m a d bev bw).dflt = m.dflt := by
  unfold writeBE; split <;> simp

/-- **The admitted shape survives the write**, so a design's memory stays
admissible for every later cycle. -/
theorem writeBE_FullTable {aw : Nat} {m : MemImg} (h : FullTable aw m)
    (w : Nat) (a d bev : BV) (bw : Nat) : FullTable aw (writeBE w m a d bev bw) := by
  unfold FullTable at h ⊢
  rw [writeBE_length]
  exact h

/-- A DISABLED write preserves the EXACT image — no range premise needed. -/
theorem writeBE_disabled_eq (w : Nat) (m : MemImg) (a d bev : BV) (bw : Nat)
    (h : ¬ bv_nonzero bev) : writeBE w m a d bev bw = m := by
  simp [writeBE, h]

/-- An enabled write changes exactly one word and leaves every other address
alone, so nothing outside the written address can drift. -/
theorem writeBE_other (w : Nat) (m : MemImg) (a d bev : BV) (bw : Nat)
    {x : Int} (hx : x ≠ bv_uint a) :
    get (writeBE w m a d bev bw) x = get m x := by
  unfold writeBE
  split
  · exact get_setAt_other m (bv_uint a) x _ hx
  · rfl

/-! ## 4. PicoRV32's actual shape -/

/-- PicoRV32's memory is the already-defined `MemImg.picoImg`: `aw = 5`,
`dw = 32`, a full 32-word table. -/
theorem picoImg_FullTable : FullTable 5 picoImg := zeroImg_FullTable 5 32

theorem picoImg_words : picoImg.words.length = 32 := by decide

/-- **`InRange` holds for every 5-bit address**, which is what the admitted Pico
shape buys: the enabled-write agreement premise is discharged, not assumed. -/
theorem pico_InRange (a : BV) (hw : a.width = 5) : InRange picoImg (bv_uint a) :=
  InRange_of_FullTable picoImg_FullTable hw

/-- …and it keeps holding after a write. -/
theorem pico_InRange_after (w : Nat) (a0 d bev : BV) (bw : Nat) (a : BV) (hw : a.width = 5) :
    InRange (writeBE w picoImg a0 d bev bw) (bv_uint a) :=
  InRange_of_FullTable (writeBE_FullTable picoImg_FullTable w a0 d bev bw) hw

/-- **`byte_w = 1` is BIT-enable semantics**, which is what PicoRV32 emits:
mask bit `i` selects data bit `i`, because `i / 1 = i`.  Recorded against the
reference's own `cert_masked_update` so the payload `applyOpM` passes through is
the one the reference uses. -/
theorem pico_byte_w_one (w : Nat) (old new bev : BV) :
    cert_masked_update w old new bev 1
      = mk_bv w (bits_to_int w (fun i => if bv_bit bev i then bv_bit new i else bv_bit old i)) := by
  simp [cert_masked_update]

/-- The executed consequence: with `byte_w = 1` a mask of 8 writes exactly bit
3, so an all-ones datum stores exactly 8. -/
example : get (writeBE 32 picoImg (mk_bv 5 7) (mk_bv 32 0xFFFFFFFF) (mk_bv 32 8) 1)
    (bv_uint (mk_bv 5 7)) = mk_bv 32 8 := by decide

/-- …while `byte_w = 8` is byte-lane semantics: mask 1 writes lane 0. -/
example : get (writeBE 32 picoImg (mk_bv 5 7) (mk_bv 32 0xFFFFFFFF) (mk_bv 4 1) 8)
    (bv_uint (mk_bv 5 7)) = mk_bv 32 0xFF := by decide

#audit_axioms InRange_of_FullTable
#audit_axioms InRange_of_FullTable_le
#audit_axioms emptyZero_denote
#audit_axioms denote_does_not_determine_geometry
#audit_axioms ofMemConst_FullTable_iff
#audit_axioms romNotFull_example
#audit_axioms rom_write_outOfRange_refused
#audit_axioms writeBE_FullTable
#audit_axioms writeBE_disabled_eq
#audit_axioms writeBE_other
#audit_axioms pico_InRange
#audit_axioms pico_InRange_after
#audit_axioms pico_byte_w_one

end MemImg
end Projection
