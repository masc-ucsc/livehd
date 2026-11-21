/-
# `lhdcert-ckpt v1`: save and restore are inverse

The three facts about `Compiler/Ckpt.lean` that a checkpoint needs:

* **T1** `save_restore`: on a checkpoint that `validate` accepts, restoring then saving gives the
  same checkpoint back. Nothing a valid file says is lost or changed.
* **T2** `validate_save`: every checkpoint `save` writes passes `validate`, for a certificate
  that passes its two certificate-only rules (`imgRootError`, `wordError`).
* **T3** `restore_save_flop`, `restore_save_mem`: the state restored from a saved checkpoint
  agrees with the saved state where a step can see it: each flop at its declared width (as
  `sourceValue` reads it), each memory word below `depth` at the memory's width.

Mathlib-free; the axioms are audited at the end.
-/
import LeanSemanticPrimitives.Compiler.Ckpt
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Compiler
namespace Ckpt

/-! ## Words and widths -/

theorem two_pow_pos (w : Nat) : (0 : Int) < 2 ^ w := Int.pow_pos (by decide)

theorem bv_uint_nonneg (x : BV) : 0 ≤ bv_uint x :=
  Int.emod_nonneg _ (Int.ne_of_gt (two_pow_pos x.width))

theorem bv_uint_mk_bv (w : Nat) (v : Int) : bv_uint (mk_bv w v) = v % 2 ^ w := by
  simp only [bv_uint, mk_bv]
  exact Int.emod_emod_of_dvd _ (Int.dvd_refl _)

/-- A value that fits reads back unchanged. -/
theorem wordAt_mk (w v : Nat) (h : v < 2 ^ w) :
    (bv_uint (mk_bv w (v : Int))).toNat % 2 ^ w = v := by
  rw [bv_uint_mk_bv]
  have hv : ((v : Int) % 2 ^ w) = v :=
    Int.emod_eq_of_lt (Int.natCast_nonneg v) (by exact_mod_cast h)
  rw [hv, Int.toNat_natCast, Nat.mod_eq_of_lt h]

/-- `mk_bv w` sees only its argument modulo `2 ^ w`. -/
theorem mk_bv_emod (w : Nat) (v : Int) : mk_bv w (v % 2 ^ w) = mk_bv w v := by
  simp only [mk_bv, Int.emod_emod_of_dvd _ (Int.dvd_refl _)]

/-- The unsigned word at `w` bits, as an `Int`, is `bv_uint` modulo `2 ^ w`. -/
theorem wordAt_cast (w : Nat) (x : BV) :
    (((bv_uint x).toNat % 2 ^ w : Nat) : Int) = bv_uint x % 2 ^ w := by
  rw [Int.natCast_emod, Int.toNat_of_nonneg (bv_uint_nonneg x)]
  norm_cast

/-- Restoring a saved word and resizing it gives the original word, resized. -/
theorem resize_saved (w : Nat) (x : BV) :
    bv_resize w (mk_bv w (((bv_uint x).toNat % 2 ^ w : Nat) : Int)) = bv_resize w x := by
  simp only [bv_resize, bv_uint_mk_bv, wordAt_cast, Int.emod_emod_of_dvd _ (Int.dvd_refl _)]
  exact mk_bv_emod w _

/-! ## The dense table -/

/-- One step of `memTable`'s fold. -/
def setStep (t : Array Nat) (p : Nat × Nat) : Array Nat := t.setIfInBounds p.1 p.2

theorem memTable_eq (m : MemCk) :
    memTable m = m.words.toList.foldl setStep (Array.replicate m.depth m.dflt) := by
  unfold memTable setStep; rfl

theorem fold_size (ws : List (Nat × Nat)) (t : Array Nat) :
    (ws.foldl setStep t).size = t.size := by
  induction ws generalizing t with
  | nil => rfl
  | cons p ws ih => simp only [List.foldl_cons, ih, setStep, Array.size_setIfInBounds]

theorem fold_get_not_key (ws : List (Nat × Nat)) (t : Array Nat) (x : Nat)
    (h : ∀ p ∈ ws, p.1 ≠ x) : (ws.foldl setStep t)[x]? = t[x]? := by
  induction ws generalizing t with
  | nil => rfl
  | cons p ws ih =>
    simp only [List.foldl_cons]
    rw [ih _ (fun q hq => h q (List.mem_cons_of_mem _ hq))]
    have hp : p.1 ≠ x := h p (List.mem_cons_self ..)
    simp [setStep, hp]

/-- Addresses strictly increasing. -/
abbrev Sorted (ws : List (Nat × Nat)) : Prop := ws.Pairwise (fun p q => p.1 < q.1)

theorem fold_get_key (ws : List (Nat × Nat)) (t : Array Nat) (x v : Nat)
    (hs : Sorted ws) (hm : (x, v) ∈ ws) (hx : x < t.size) :
    (ws.foldl setStep t)[x]? = some v := by
  induction ws generalizing t with
  | nil => simp at hm
  | cons p ws ih =>
    simp only [List.foldl_cons]
    replace hs := List.pairwise_cons.mp hs
    rcases List.mem_cons.mp hm with he | hm'
    · subst he
      rw [fold_get_not_key _ _ _ (fun q hq => Nat.ne_of_gt (hs.1 q hq))]
      simp [setStep, hx]
    · exact ih _ hs.2 hm' (by simp [setStep, Array.size_setIfInBounds, hx])

/-- The table of a sorted list of words below `depth`: the listed value at a listed address,
the default at any other address below `depth`. -/
theorem table_key (m : MemCk) (hs : Sorted m.words.toList) {x v : Nat}
    (hm : (x, v) ∈ m.words.toList) (hx : x < m.depth) : (memTable m)[x]? = some v := by
  rw [memTable_eq]; exact fold_get_key _ _ _ _ hs hm (by simp [hx])

theorem table_other (m : MemCk) {x : Nat} (hk : ∀ p ∈ m.words.toList, p.1 ≠ x)
    (hx : x < m.depth) : (memTable m)[x]? = some m.dflt := by
  rw [memTable_eq, fold_get_not_key _ _ _ hk]; simp [hx]

theorem table_size (m : MemCk) : (memTable m).size = m.depth := by
  rw [memTable_eq, fold_size]; simp

/-! ## Back from the table: the sparse words -/

/-- Scanning addresses `s .. s+n-1` and keeping those whose value `L` differs from `d` gives
back a sorted list `ws` of keys in that range, values not `d`, when `L` reads `ws` (and `d` at
every other address of the range). -/
theorem scan_eq (L : Nat → Nat) (d : Nat) :
    ∀ (n s : Nat) (ws : List (Nat × Nat)), Sorted ws →
    (∀ p ∈ ws, s ≤ p.1 ∧ p.1 < s + n) → (∀ p ∈ ws, p.2 ≠ d) → (∀ p ∈ ws, L p.1 = p.2) →
    (∀ x, s ≤ x → x < s + n → (∀ p ∈ ws, p.1 ≠ x) → L x = d) →
    (List.range' s n).filterMap (fun a => if L a == d then none else some (a, L a)) = ws
  | 0, s, ws, _, hr, _, _, _ => by
    cases ws with
    | nil => rfl
    | cons p ws => have := hr p (List.mem_cons_self ..); omega
  | n + 1, s, ws, hs, hr, hv, hL, hd => by
    rw [List.range'_succ, List.filterMap_cons]
    cases ws with
    | nil =>
      have hs' : L s = d := hd s (Nat.le_refl _) (by omega) (by simp)
      simp only [hs', beq_self_eq_true, ite_true]
      exact scan_eq L d n (s + 1) [] List.Pairwise.nil (by simp) (by simp) (by simp)
        (fun x h1 h2 _ => hd x (by omega) (by omega) (by simp))
    | cons p ws =>
      replace hs := List.pairwise_cons.mp hs
      have hp := hr p (List.mem_cons_self ..)
      by_cases hps : p.1 = s
      · have hLs : L s = p.2 := hps ▸ hL p (List.mem_cons_self ..)
        have hne : (L s == d) = false := by
          rw [hLs]; exact beq_false_of_ne (hv p (List.mem_cons_self ..))
        simp only [hne, Bool.false_eq_true, ite_false]
        have hpe : (s, L s) = p := by rw [hLs, ← hps]
        rw [hpe]
        congr 1
        exact scan_eq L d n (s + 1) ws hs.2
          (fun q hq => by have := hs.1 q hq; have := hr q (List.mem_cons_of_mem _ hq); omega)
          (fun q hq => hv q (List.mem_cons_of_mem _ hq))
          (fun q hq => hL q (List.mem_cons_of_mem _ hq))
          (fun x h1 h2 hk => hd x (by omega) (by omega) (by
            intro q hq
            rcases List.mem_cons.mp hq with he | hq'
            · subst he; omega
            · exact hk q hq'))
      · have hLs : L s = d := hd s (Nat.le_refl _) (by omega) (by
          intro q hq
          rcases List.mem_cons.mp hq with he | hq'
          · subst he; exact hps
          · have := hs.1 q hq'; omega)
        simp only [hLs, beq_self_eq_true, ite_true]
        exact scan_eq L d n (s + 1) (p :: ws) (List.pairwise_cons.mpr hs)
          (fun q hq => by
            have hq2 := hr q hq
            rcases List.mem_cons.mp hq with he | hq'
            · subst he; omega
            · have := hs.1 q hq'; omega)
          hv hL (fun x h1 h2 hk => hd x (by omega) (by omega) hk)

/-! ## Reading `validate` -/

theorem wordsError_none {k dw depth dflt : Nat} :
    ∀ (prev : Option Nat) (ws : List (Nat × Nat)),
    wordsError k dw depth dflt prev ws = none →
      Sorted ws ∧ (∀ p ∈ ws, p.1 < depth ∧ p.2 < 2 ^ dw ∧ p.2 ≠ dflt) ∧
      (∀ q ∈ prev, ∀ p ∈ ws, q < p.1)
  | _, [], _ => by simp
  | prev, (a, v) :: ws, h => by
    unfold wordsError at h
    by_cases ha : a ≥ depth
    · simp [ha] at h
    by_cases hv : v ≥ 2 ^ dw
    · simp [ha, hv] at h
    have hprev : ∀ q ∈ prev, q < a := by
      intro q hq
      rw [Option.mem_def] at hq
      subst hq
      by_cases hap : a ≤ q
      · simp [ha, hv, Option.filter, hap] at h
      · omega
    have hf : prev.filter (fun x => decide (a ≤ x)) = none := by
      cases prev with
      | none => rfl
      | some p =>
        have := hprev p (by simp)
        simp [Option.filter]; omega
    by_cases hd : v = dflt
    · subst hd; simp [ha, hv, hf] at h
    simp only [ha, hv, hf, ite_false, beq_iff_eq, hd] at h
    obtain ⟨hs, hw, hp⟩ := wordsError_none (some a) ws h
    refine ⟨List.pairwise_cons.mpr ⟨fun q hq => hp a (by simp) q hq, hs⟩, ?_, ?_⟩
    · intro p hp'
      rcases List.mem_cons.mp hp' with he | hq
      · subst he; exact ⟨by omega, by omega, hd⟩
      · exact hw p hq
    · intro q hq p hp'
      rcases List.mem_cons.mp hp' with he | hr
      · subst he; exact hprev q hq
      · exact Nat.lt_trans (hprev q hq) (hp a (by simp) p hr)

theorem wordsError_of {k dw depth dflt : Nat} :
    ∀ (prev : Option Nat) (ws : List (Nat × Nat)),
      Sorted ws → (∀ p ∈ ws, p.1 < depth ∧ p.2 < 2 ^ dw ∧ p.2 ≠ dflt) →
      (∀ q ∈ prev, ∀ p ∈ ws, q < p.1) →
      wordsError k dw depth dflt prev ws = none
  | _, [], _, _, _ => rfl
  | prev, (a, v) :: ws, hs, hw, hp => by
    replace hs := List.pairwise_cons.mp hs
    obtain ⟨ha, hv, hd⟩ := hw (a, v) (List.mem_cons_self ..)
    have hf : prev.filter (fun x => decide (a ≤ x)) = none := by
      cases prev with
      | none => rfl
      | some p =>
        have := hp p (by simp) (a, v) (List.mem_cons_self ..)
        simp [Option.filter]; omega
    unfold wordsError
    simp only [show ¬ a ≥ depth by omega, show ¬ v ≥ 2 ^ dw by omega, hf, ite_false,
      beq_iff_eq, hd]
    exact wordsError_of (some a) ws hs.2 (fun p hp' => hw p (List.mem_cons_of_mem _ hp'))
      (fun q hq p hp' => by
        rw [Option.mem_def, Option.some.injEq] at hq; subst hq
        exact hs.1 p hp')

/-- What a memory that `memError` accepts satisfies. -/
structure MemOK (md : MemoryDesc) (m : MemCk) : Prop where
  aw    : m.aw = md.aw
  dw    : m.dw = md.dw
  depth : m.depth = md.depth
  dflt  : m.dflt < 2 ^ m.dw
  sorted : Sorted m.words.toList
  words : ∀ p ∈ m.words.toList, p.1 < m.depth ∧ p.2 < 2 ^ m.dw ∧ p.2 ≠ m.dflt

theorem memOK_of {k : Nat} {md : MemoryDesc} {m : MemCk} (h : memError k md m = none) :
    MemOK md m := by
  unfold memError at h
  split at h
  · cases h
  rename_i hs
  split at h
  · cases h
  rename_i hd
  simp only [Bool.or_eq_true, bne_iff_ne, ne_eq, not_or, Decidable.not_not] at hs
  obtain ⟨hso, hw, _⟩ := wordsError_none none _ h
  exact ⟨hs.1.1, hs.1.2, hs.2, by omega, hso, hw⟩

theorem memError_of {k : Nat} {md : MemoryDesc} {m : MemCk} (h : MemOK md m) :
    memError k md m = none := by
  unfold memError
  have h1 : ¬ ((m.aw != md.aw || m.dw != md.dw || m.depth != md.depth) = true) := by
    simp [h.aw, h.dw, h.depth]
  have h2 : ¬ (m.dflt ≥ 2 ^ m.dw) := Nat.not_le.mpr h.dflt
  simp only [h1, h2, ↓reduceIte]
  exact wordsError_of none _ h.sorted h.words (by simp)

theorem flopOK_of {k : Nat} {f : FlopDesc} {wv : Nat × Nat} (h : flopError k f wv = none) :
    wv.1 = f.width ∧ wv.2 < 2 ^ wv.1 := by
  unfold flopError at h
  split at h
  · cases h
  rename_i h1
  split at h
  · cases h
  rename_i h2
  simp only [bne_iff_ne, ne_eq, Decidable.not_not] at h1
  exact ⟨h1, by omega⟩

theorem flopError_of {k : Nat} {f : FlopDesc} {wv : Nat × Nat} (h1 : wv.1 = f.width)
    (h2 : wv.2 < 2 ^ wv.1) : flopError k f wv = none := by
  unfold flopError
  simp [h1, Nat.not_le.mpr (h1 ▸ h2)]

/-- What a checkpoint that `validate` accepts satisfies. -/
structure Valid (D : DesignCert) (hash : Nat) (c : Ckpt) : Prop where
  design : c.design = hash
  nflops : c.flops.size = D.flops.size
  flops  : ∀ k (hk : k < c.flops.size), flopError k D.flops[k]! c.flops[k] = none
  nmems  : c.mems.size = D.memories.size
  mems   : ∀ k (hk : k < c.mems.size), memError k D.memories[k]! c.mems[k] = none
  imgRoot : imgRootError D = none
  word   : wordError D = none

theorem valid_of {D : DesignCert} {hash : Nat} {c : Ckpt} (h : validate D hash c = none) :
    Valid D hash c := by
  unfold validate at h
  by_cases h1 : (c.design != hash) = true
  · simp only [h1, ite_true] at h; cases h
  simp only [h1, Bool.false_eq_true, ite_false] at h
  by_cases h2 : (c.flops.size != D.flops.size) = true
  · simp only [h2, ite_true] at h; cases h
  simp only [h2, Bool.false_eq_true, ite_false] at h
  cases h3 : (List.range c.flops.size).findSome? (fun k => flopError k D.flops[k]! c.flops[k]!)
  · simp only [h3] at h
    by_cases h4 : (c.mems.size != D.memories.size) = true
    · simp only [h4, ite_true] at h; cases h
    simp only [h4, Bool.false_eq_true, ite_false] at h
    cases h5 : (List.range c.mems.size).findSome? (fun k => memError k D.memories[k]! c.mems[k]!)
    · simp only [h5] at h
      cases h6 : imgRootError D
      · simp only [h6] at h
        simp only [bne_iff_ne, ne_eq, Decidable.not_not] at h1 h2 h4
        rw [List.findSome?_eq_none_iff] at h3 h5
        refine ⟨h1, h2, fun k hk => ?_, h4, fun k hk => ?_, h6, h⟩
        · have := h3 k (List.mem_range.mpr hk)
          rwa [getElem!_pos c.flops k hk] at this
        · have := h5 k (List.mem_range.mpr hk)
          rwa [getElem!_pos c.mems k hk] at this
      · simp only [h6] at h; cases h
    · simp only [h5] at h; cases h
  · simp only [h3] at h; cases h

/-! ## T1: restore then save is the identity on a valid checkpoint -/

theorem saveMem_memImage {md : MemoryDesc} {m : MemCk} (h : MemOK md m) :
    saveMem md (memImage m) = m := by
  have hdflt : wordAt md.dw (memImage m) (-1) = m.dflt := by
    simp only [wordAt, memImage, show ¬ (0 : Int) ≤ -1 by decide, ite_false, Option.getD_none]
    rw [← h.dw]; exact wordAt_mk _ _ h.dflt
  -- the word at every address below depth is the table's
  have hread : ∀ a : Nat, a < md.depth →
      wordAt md.dw (memImage m) (Int.ofNat a) = (memTable m)[a]?.getD m.dflt := by
    intro a ha
    have ha' : a < m.depth := by rw [h.depth]; exact ha
    have hfit : (memTable m)[a]?.getD m.dflt < 2 ^ m.dw := by
      by_cases hk : ∃ p ∈ m.words.toList, p.1 = a
      · obtain ⟨⟨x, v⟩, hp, hx⟩ := hk
        simp only at hx; subst hx
        rw [table_key m h.sorted hp ha']; exact (h.words _ hp).2.1
      · rw [table_other m (fun p hp he => hk ⟨p, hp, he⟩) ha']; exact h.dflt
    simp only [wordAt, memImage, Int.ofNat_eq_natCast, Int.natCast_nonneg, ite_true,
      Int.toNat_natCast]
    rw [← h.dw]
    exact wordAt_mk _ _ hfit
  have hscan : (List.range md.depth).filterMap (fun a =>
      if wordAt md.dw (memImage m) (Int.ofNat a) == m.dflt then none
      else some (a, wordAt md.dw (memImage m) (Int.ofNat a))) = m.words.toList := by
    rw [List.range_eq_range']
    exact scan_eq (fun a => wordAt md.dw (memImage m) (Int.ofNat a)) m.dflt md.depth 0 _ h.sorted
      (fun p hp => ⟨Nat.zero_le _, by rw [Nat.zero_add, ← h.depth]; exact (h.words p hp).1⟩)
      (fun p hp => (h.words p hp).2.2)
      (fun p hp => by
        have hlt : p.1 < m.depth := (h.words p hp).1
        show wordAt _ _ _ = _
        rw [hread p.1 (by rw [← h.depth]; exact hlt), table_key m h.sorted (x := p.1) (v := p.2) hp hlt]
        rfl)
      (fun x _ hx hk => by
        have hlt : x < md.depth := by omega
        show wordAt _ _ _ = _
        rw [hread x hlt, table_other m hk (by rw [h.depth]; exact hlt)]
        rfl)
  have e : saveMem md (memImage m) =
      { aw := md.aw, dw := md.dw, depth := md.depth, dflt := m.dflt,
        words := m.words.toList.toArray } := by
    unfold saveMem
    simp only [hdflt]
    rw [hscan]
  rw [e]
  cases m with
  | mk aw dw depth dflt words =>
    obtain ⟨haw, hdw, hdp, -, -, -⟩ := h
    simp only at haw hdw hdp
    subst haw hdw hdp
    simp

/-- **T1.** On a checkpoint that `validate` accepts, restoring then saving gives it back. -/
theorem save_restore {D : DesignCert} {hash : Nat} {c : Ckpt} (h : validate D hash c = none) :
    save D hash c.cycle c.exts (restore c) = c := by
  have hv := valid_of h
  have hfl : D.flops.mapIdx (fun k f => (f.width,
      (bv_uint ((restore c).flops[k]?.getD (mk_bv f.width 0))).toNat % 2 ^ f.width)) = c.flops := by
    apply Array.ext
    · simp [hv.nflops]
    · intro i h1 h2
      simp only [Array.getElem_mapIdx, restore]
      rw [Array.getElem?_map, Array.getElem?_eq_getElem h2]
      simp only [Option.map_some, Option.getD_some]
      have hfe := flopOK_of (hv.flops i h2)
      rw [getElem!_pos D.flops i (by simpa using h1)] at hfe
      generalize c.flops[i] = p at hfe ⊢
      obtain ⟨w, v⟩ := p
      simp only at hfe ⊢
      rw [← hfe.1, wordAt_mk w v hfe.2]
  have hme : D.memories.mapIdx (fun k md =>
      saveMem md ((restore c).mems[k]?.getD fun _ => mk_bv md.dw 0)) = c.mems := by
    apply Array.ext
    · simp [hv.nmems]
    · intro i h1 h2
      simp only [Array.getElem_mapIdx, restore]
      rw [Array.getElem?_map, Array.getElem?_eq_getElem h2]
      simp only [Option.map_some, Option.getD_some]
      have hmo := memOK_of (hv.mems i h2)
      rw [getElem!_pos D.memories i (by simpa using h1)] at hmo
      exact saveMem_memImage hmo
  have hd : c.design = hash := hv.design
  unfold save
  rw [hfl, hme, ← hd]

/-! ## T2: every save validates -/

theorem mem_saveMem_words {md : MemoryDesc} {f : Int → BV} {p : Nat × Nat} :
    p ∈ (saveMem md f).words.toList ↔
      p.1 < md.depth ∧ p.2 = wordAt md.dw f (Int.ofNat p.1) ∧ p.2 ≠ wordAt md.dw f (-1) := by
  simp only [saveMem, List.toList_toArray, List.mem_filterMap, List.mem_range]
  constructor
  · rintro ⟨a, ha, hs⟩
    split at hs
    · cases hs
    · rename_i hne
      cases hs
      exact ⟨ha, rfl, by simpa using hne⟩
  · rintro ⟨h1, h2, h3⟩
    refine ⟨p.1, h1, ?_⟩
    rw [← h2]
    have h5 : ¬ ((p.2 == wordAt md.dw f (-1)) = true) := by simpa using h3
    simp only [h5, Bool.false_eq_true, ↓reduceIte]

theorem saveMem_ok (md : MemoryDesc) (f : Int → BV) : MemOK md (saveMem md f) where
  aw := rfl
  dw := rfl
  depth := rfl
  dflt := Nat.mod_lt _ (Nat.two_pow_pos _)
  sorted := by
    simp only [saveMem, List.toList_toArray]
    refine List.Pairwise.filterMap _ ?_ List.pairwise_lt_range
    intro a a' haa' b hb b' hb'
    split at hb <;> cases hb
    split at hb' <;> cases hb'
    exact haa'
  words := by
    intro p hp
    obtain ⟨h1, h2, h3⟩ := mem_saveMem_words.mp hp
    refine ⟨h1, ?_, by simpa [saveMem] using h3⟩
    rw [h2]; exact Nat.mod_lt _ (Nat.two_pow_pos _)

/-- **T2.** Every checkpoint `save` writes passes `validate`, for a certificate that passes the
two certificate-only rules. -/
theorem validate_save (D : DesignCert) (hash cycle : Nat) (exts : Array Ext) (s : RuntimeState)
    (hr : imgRootError D = none) (hw : wordError D = none) :
    validate D hash (save D hash cycle exts s) = none := by
  have hfl : (List.range (save D hash cycle exts s).flops.size).findSome? (fun k =>
      flopError k D.flops[k]! (save D hash cycle exts s).flops[k]!) = none := by
    rw [List.findSome?_eq_none_iff]
    intro k hk
    have hk' : k < D.flops.size := by simpa [save] using hk
    rw [getElem!_pos D.flops k hk', getElem!_pos _ k (by simpa [save] using hk)]
    simp only [save, Array.getElem_mapIdx]
    exact flopError_of rfl (Nat.mod_lt _ (Nat.two_pow_pos _))
  have hme : (List.range (save D hash cycle exts s).mems.size).findSome? (fun k =>
      memError k D.memories[k]! (save D hash cycle exts s).mems[k]!) = none := by
    rw [List.findSome?_eq_none_iff]
    intro k hk
    have hk' : k < D.memories.size := by simpa [save] using hk
    rw [getElem!_pos D.memories k hk', getElem!_pos _ k (by simpa [save] using hk)]
    simp only [save, Array.getElem_mapIdx]
    exact memError_of (saveMem_ok _ _)
  unfold validate
  rw [hfl, hme]
  simp [save, hr, hw]

/-! ## T3: a restored save agrees with the saved state where a step can see it -/

/-- The table of a saved memory holds every word below `depth`. -/
theorem table_saveMem (md : MemoryDesc) (f : Int → BV) {x : Nat} (hx : x < md.depth) :
    (memTable (saveMem md f))[x]? = some (wordAt md.dw f (Int.ofNat x)) := by
  have hok := saveMem_ok md f
  by_cases hd : wordAt md.dw f (Int.ofNat x) = wordAt md.dw f (-1)
  · rw [table_other _ (fun p hp he => ?_) hx, hd]
    · rfl
    · obtain ⟨_, h2, h3⟩ := mem_saveMem_words.mp hp
      rw [he] at h2; exact h3 (h2.trans hd)
  · exact table_key _ hok.sorted (mem_saveMem_words.mpr ⟨hx, rfl, hd⟩) hx

/-- **T3, flops.** Each restored flop reads, at its declared width, as the saved flop did. -/
theorem restore_save_flop (D : DesignCert) (hash cycle : Nat) (exts : Array Ext)
    (s : RuntimeState) {k : Nat} (hk : k < D.flops.size) (w : Nat) (hw : D.flops[k].width = w) :
    bv_resize w ((restore (save D hash cycle exts s)).flops[k]?.getD (mk_bv w 0)) =
      bv_resize w (s.flops[k]?.getD (mk_bv w 0)) := by
  simp only [restore, save, Array.getElem?_map, Array.getElem?_mapIdx, Array.getElem?_eq_getElem hk,
    Option.map_some, Option.getD_some, hw]
  exact resize_saved w _

/-- **T3, memories.** Each restored memory word below `depth` is the saved word at the
memory's width. -/
theorem restore_save_mem (D : DesignCert) (hash cycle : Nat) (exts : Array Ext)
    (s : RuntimeState) {k : Nat} (hk : k < D.memories.size) (a : Int) (h0 : 0 ≤ a)
    (ha : a.toNat < D.memories[k].depth) (dflt : Int → BV) :
    ((restore (save D hash cycle exts s)).mems[k]?.getD dflt) a =
      bv_resize D.memories[k].dw ((s.mems[k]?.getD fun _ => mk_bv D.memories[k].dw 0) a) := by
  simp only [restore, save, Array.getElem?_map, Array.getElem?_mapIdx, Array.getElem?_eq_getElem hk,
    Option.map_some, Option.getD_some]
  simp only [memImage, h0, ite_true]
  rw [table_saveMem _ _ ha]
  simp only [Option.getD_some, wordAt, Int.ofNat_eq_natCast, Int.toNat_of_nonneg h0, saveMem]
  rw [wordAt_cast, mk_bv_emod]
  rfl

end Ckpt
end Compiler

#audit_axioms Compiler.Ckpt.save_restore
#audit_axioms Compiler.Ckpt.validate_save
#audit_axioms Compiler.Ckpt.restore_save_flop
#audit_axioms Compiler.Ckpt.restore_save_mem
