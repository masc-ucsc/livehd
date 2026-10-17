/-
# Groundwork for `Op_MemWrite`, the one operator the memory route still refuses

§63.58's census found exactly one operator gap across the whole retained
corpus: `Op_MemWrite`, the non-byte-enabled write, which is the sole reason
`tiny_ram`, `tiny_sync_ram`, `mem_domain`, `mem_nondefault` and `memfix_b` are
refused.  This module proves the reference-level facts that gap needs, and
nothing else.

## What is already here, and what is not

The IMAGE level is already done and was done before this module: `MemImg.writeEn`
is the word write with an enable, and `MemImg.denote_writeEn` proves it denotes
`cert_mem_write` whenever the address is in range.  So the semantics of the
operator are not in question; what is missing is the ROUTE -- an object
primitive, an `applyOpM` dispatch, the checker's admission and typing, and the
node/fold lemmas.

Those require editing `ObjectLanguage` and `hwMemS`, which moves `hwMemP`,
`hwMemAP` and the two kernel reductions that pin them.  That is the next
checkpoint and it is NOT started here.  This module is strictly additive: it
touches no existing definition, admits nothing, and changes no route.

## What it adds

The shape facts a write node will have to satisfy, proved for `writeEn` in the
same form they are already proved for `writeBE`:

* the enable discipline (`writeEn_disabled_eq`), so a disabled write leaves the
  image EXACTLY alone, not merely equivalent;
* table length preservation (`writeEn_FullTable`);
* word and default width preservation (`writeEn_MemShapeOK`), which is what
  §63.45's returned-state policy would need;
* and the reference-side width fact (`cert_mem_write_width`).

Recorded with them is the side condition §63.58's addendum turned up: the
reference write stores `d` RAW, so an image whose words must all be `dw` wide
only stays that way when the datum IS `dw` wide.  That is a hypothesis here,
not an assumption: it is exactly what the checker's `bvWidthOf td == dw` would
have to supply.
-/
import LeanSemanticPrimitives.Projection.HardwareMemRuntime

namespace Projection
namespace HwMem
open Compiler Projection.MemImg

/-! ## The reference operator -/

/-- The certificate evaluator on a four-operand `Op_MemWrite`, definitionally. -/
theorem eval_op_cert_memWrite (w : Nat) (m a d en : CertVal) :
    eval_op_cert .Op_MemWrite w [m, a, d, en]
      = .mem (cert_mem_write m.asMem a.asBV d.asBV en.asBV) := rfl

/-- Disabled: the memory function is returned UNCHANGED, not a copy that agrees
with it. -/
theorem cert_mem_write_disabled {m : Int → BV} {a d en : BV}
    (h : ¬ bv_nonzero en) : cert_mem_write m a d en = m := by
  simp [cert_mem_write, h]

/-- Width, pointwise.  The datum is stored RAW, so its own width is the
hypothesis -- `cert_mem_write` does no resizing, unlike `cert_mem_read` and the
byte-enabled write, both of which end in `mk_bv`. -/
theorem cert_mem_write_width {m : Int → BV} {a d en : BV} {dw : Nat}
    (hm : ∀ x : Int, (m x).width = dw) (hd : d.width = dw) :
    ∀ x : Int, ((cert_mem_write m a d en) x).width = dw := by
  intro x
  unfold cert_mem_write
  by_cases hen : bv_nonzero en
  · simp only [hen, if_true]
    by_cases hx : x = bv_uint a
    · simp only [hx, if_true]; exact hd
    · simp only [hx, if_false]; exact hm x
  · simp only [hen, Bool.false_eq_true, if_false]; exact hm x

/-! ## The image -/

/-- Disabled: the IMAGE is returned unchanged -- the geometry survives a
disabled write for the same reason §63.45's quiet branch does. -/
theorem writeEn_disabled_eq (m : MemImg) (a d en : BV) (h : ¬ bv_nonzero en) :
    writeEn m a d en = m := by
  simp [writeEn, h]

/-- `setAt` replaces one word in place, so the table length never moves. -/
theorem setAt_length (m : MemImg) (x : Int) (d : BV) :
    (setAt m x d).words.length = m.words.length := by
  unfold setAt
  split <;> simp

theorem setAt_dflt (m : MemImg) (x : Int) (d : BV) :
    (setAt m x d).dflt = m.dflt := by
  unfold setAt
  split <;> rfl

theorem writeEn_length (m : MemImg) (a d en : BV) :
    (writeEn m a d en).words.length = m.words.length := by
  unfold writeEn
  split
  · exact setAt_length m (bv_uint a) d
  · rfl

theorem writeEn_dflt (m : MemImg) (a d en : BV) :
    (writeEn m a d en).dflt = m.dflt := by
  unfold writeEn
  split
  · exact setAt_dflt m (bv_uint a) d
  · rfl

theorem writeEn_FullTable {aw : Nat} {m : MemImg} (h : FullTable aw m)
    (a d en : BV) : FullTable aw (writeEn m a d en) := by
  unfold FullTable at h ⊢
  rw [writeEn_length]; exact h

/-- A member of `l.set n b` is either `b` or was already in `l`.  Proved here by
induction rather than by name, so it does not depend on which library spelling
of this fact is available. -/
private theorem mem_set_cases {α : Type _} :
    ∀ (l : List α) (n : Nat) (b a : α), a ∈ l.set n b → a = b ∨ a ∈ l
  | [],      _,     _, _, h => by simp at h
  | x :: xs, 0,     b, a, h => by
      simp only [List.set] at h
      rcases List.mem_cons.mp h with rfl | h'
      · exact Or.inl rfl
      · exact Or.inr (by simp [h'])
  | x :: xs, n + 1, b, a, h => by
      simp only [List.set] at h
      rcases List.mem_cons.mp h with rfl | h'
      · exact Or.inr (by simp)
      · rcases mem_set_cases xs n b a h' with rfl | h''
        · exact Or.inl rfl
        · exact Or.inr (by simp [h''])

/-- Every word of a `setAt` result is either the stored datum or a word that was
already there. -/
theorem setAt_mem_words {m : MemImg} {x : Int} {d w : BV}
    (h : w ∈ (setAt m x d).words) : w = d ∨ w ∈ m.words := by
  unfold setAt at h
  split at h
  · simp only at h
    exact mem_set_cases m.words x.toNat d w h
  · exact Or.inr h

/-- **The shape survives a word write**, provided the datum has the declared
data width -- which is the condition the checker's `bvWidthOf td == dw` exists
to provide. -/
theorem writeEn_MemShapeOK {md : MemoryDesc} {m : MemImg}
    (h : MemShapeOK md m) {a d en : BV} (hd : d.width = md.dw) :
    MemShapeOK md (writeEn m a d en) := by
  refine ⟨by rw [writeEn_length]; exact h.1, ?_, by rw [writeEn_dflt]; exact h.2.2⟩
  intro w hw
  unfold writeEn at hw
  split at hw
  · rcases setAt_mem_words hw with rfl | hw'
    · exact hd
    · exact h.2.1 w hw'
  · exact h.2.1 w hw

/-- …and the denotation agrees with the reference, which was already proved:
`MemImg.denote_writeEn`.  Stated here only so the three facts a write node needs
-- geometry, widths and denotation -- appear together. -/
theorem writeEn_denote {m : MemImg} {a d en : BV} (h : InRange m (bv_uint a)) :
    denote (writeEn m a d en) = cert_mem_write (denote m) a d en :=
  denote_writeEn m a d en h

/-! ## Executed controls -/

private def t4 : MemImg := ⟨(List.range 4).map (fun k => mk_bv 8 (Int.ofNat k)), mk_bv 8 9⟩

/- enabled: the addressed word changes and NOTHING else does -/
#guard (writeEn t4 (mk_bv 2 1) (mk_bv 8 77) (mk_bv 1 1)).words
         == [mk_bv 8 0, mk_bv 8 77, mk_bv 8 2, mk_bv 8 3]
/- disabled: the image is the SAME image -/
#guard writeEn t4 (mk_bv 2 1) (mk_bv 8 77) (mk_bv 1 0) == t4
/- the default is untouched either way -/
#guard (writeEn t4 (mk_bv 2 1) (mk_bv 8 77) (mk_bv 1 1)).dflt == mk_bv 8 9
/- out of range: a no-op, never a silent extension -/
#guard writeEn t4 (mk_bv 8 9) (mk_bv 8 77) (mk_bv 1 1) == t4
#guard (writeEn t4 (mk_bv 8 9) (mk_bv 8 77) (mk_bv 1 1)).words.length == 4

#print axioms cert_mem_write_width
#print axioms writeEn_FullTable
#print axioms writeEn_MemShapeOK
#print axioms writeEn_denote

end HwMem
end Projection
