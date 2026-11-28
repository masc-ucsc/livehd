/-
# The state rules of `interpretDesign`

This file states what one step of `interpretDesign` does to state.
Each theorem holds for every design, every input and every state.
The simulator routes have theorems that end at `interpretDesign`.
Composed with those, each rule here holds for every route.
So these theorems stand in for corner-case tests against a Verilog simulator.

The rules, and the Verilog behavior each one matches:

* Quiet clock (`flop_quiet_holds`).
  A flop whose clock does not fire keeps its value, unless an asynchronous reset is asserted.
  Verilog: an `always_ff @(posedge clk)` block runs only at an edge of `clk`.
* Asynchronous reset (`flop_async_reset`).
  An asserted asynchronous reset loads the reset value whether or not the clock fires.
  Verilog: `always_ff @(posedge clk or posedge rst)` also runs when `rst` is asserted.
  An asynchronous reset acts without a clock edge.
  The same-step reader sees it too (`sourceValue_flopQAsync_on`).
* Synchronous reset (`flop_sync_reset_quiet_holds`, `flop_reset_at_edge`).
  A synchronous reset acts only at an edge.
  Verilog: `if (rst)` inside `always_ff @(posedge clk)`.
* Reset priority (`flop_reset_priority`).
  At an edge, an asserted reset wins over the enable and the data.
  Verilog: `if (rst) q <= rv; else if (en) q <= d;`.
* Enable (`flop_disabled_holds`, `flop_enabled_loads`).
  At an edge, a flop with its enable off holds.
  With it on, the flop takes `din`, cut or zero-extended to the flop's width.
* Reset polarity (`resetOn_activeLow`, `resetOn_activeHigh`).
  An active-low reset is asserted when its value is zero.
  An active-high reset is asserted when its value is nonzero.
  Verilog: `negedge rst_n` with `if (!rst_n)`, against `posedge rst` with `if (rst)`.
* Memory on one clock (`mem_single_quiet_holds`, `mem_single_quiet_async`, `mem_single_fires`).
  When its clock does not fire, a memory keeps its image.
  With an asynchronous whole-array reset it keeps the image with the reset applied.
* Memory on several clocks (`mem_multi_allFire`, `mem_multi_entry`, `mem_multi_no_entry_holds`,
  `mem_multi_none_fire_holds`).
  When all its clocks fire it takes the image of every port's writes.
  When some fire it takes the commit-table entry for exactly those clocks.
  When none fires, or no entry matches, it holds.
  The none-fires rule assumes no entry has mask 0, which the loader checks.
* Write ports (`write_*`, `chain_word`).
  A disabled write changes nothing.
  A write changes only the word at its own address.
  Of two writes to one address in a chain, the later one wins.
  Verilog: the later nonblocking write in program order wins.
  Writes to different addresses commute.
  A plain write stores its data at the data's own width (`write_same`).
* Byte-enabled writes (`masked_update_bit`, `write_be_bit`).
  Only the enabled bytes change.
  Verilog: `if (be[k]) mem[a][8*k +: 8] <= d[8*k +: 8];`.
* Reads (`read_disabled`, `read_enabled`).
  A disabled read gives 0.
  An enabled read gives the stored word, cut or zero-extended to the read width.
* Read during write (`stepEnv_read_preStep`, `read_after_write_same`, `read_after_write_other`).
  A read node whose memory operand is the memory's source slot reads the PRE-step image.
  So it returns the old word even when a write to the same address commits in the same step.
  Verilog: a nonblocking write `mem[a] <= d` is not seen by a read in the same time step.
  A read node whose memory operand is a write node reads the post-write image instead.
  The two readings differ only at the written address.
* Whole-array load (`load_*`).
  An enabled load sets entries `0 ≤ x < n` from slices of the bus and leaves the rest.
* Power-on image (`powerOnImg_*`).
  Word `a` is `init[a]`, and 0 at a negative address or past the end of `init`.
* One clock (`flop_allEdges`, `interpretDesign_allEdges_flop`, `interpretDesign_allEdges_mem`).
  When every declared clock fires, the rules above are the one-clock rules.

The `interpretDesign_*` theorems state the flop and memory rules for the next state of
`interpretDesign` itself.
The `#guard` checks at the end run these rules on small concrete designs and values.
-/
import LeanSemanticPrimitives.Compiler.DesignSemantics
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Compiler
namespace StateSpec

open GraphRefine

/-! ## Names for the parts of one step -/

/-- The slot values of one step of `D`.
`interpretDesign` reads every pin and every image from this environment. -/
abbrev stepEnv (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) : Nat → CertVal :=
  evalGraphG D.toGraphCert.topo D.toGraphCert (srcEnv D i s)

/-- Flop `f`'s reset is asserted in this step.
It is `false` when the flop has no reset pin. -/
def resetOn (rho : Nat → CertVal) (f : FlopDesc) : Bool :=
  match f.resetPin with
  | none   => false
  | some r => xor f.resetActiveLow (bv_nonzero (rho r).asBV)

/-- Flop `f`'s enable is on in this step.
It is `true` when the flop has no enable pin. -/
def enableOn (rho : Nat → CertVal) (f : FlopDesc) : Bool :=
  match f.enable with
  | none    => true
  | some en => bv_nonzero (rho en).asBV

/-- The value flop `idx` keeps when it holds: its stored value. -/
def flopHeld (s : RuntimeState) (idx : Nat) (f : FlopDesc) : BV :=
  s.flops[idx]?.getD (mk_bv f.width 0)

/-- The flop rule, with its three conditions named. -/
theorem srcFlopNext_eq (rho : Nat → CertVal) (e : ClockEdges) (s : RuntimeState) (idx : Nat)
    (f : FlopDesc) :
    srcFlopNext rho e s idx f =
      if resetOn rho f && (fires e f.clock || f.asyncReset) then mk_bv f.width f.resetValue
      else if fires e f.clock && enableOn rho f then bv_resize f.width (rho f.din).asBV
      else flopHeld s idx f := rfl

theorem lt_size_of_getElem? {α : Type} {xs : Array α} {k : Nat} {a : α}
    (h : xs[k]? = some a) : k < xs.size := by
  rcases Nat.lt_or_ge k xs.size with hk | hk
  · exact hk
  · rw [Array.getElem?_eq_none hk] at h; cases h

/-! ## Flops -/

section Flops
variable {rho : Nat → CertVal} {e : ClockEdges} {s : RuntimeState} {idx : Nat} {f : FlopDesc}

/-- A quiet clock holds, unless an asynchronous reset is asserted. -/
theorem flop_quiet_holds (hq : fires e f.clock = false)
    (hr : (resetOn rho f && f.asyncReset) = false) :
    srcFlopNext rho e s idx f = flopHeld s idx f := by
  rw [srcFlopNext_eq, hq]
  cases h1 : resetOn rho f <;> cases h2 : f.asyncReset <;> simp_all

/-- A flop with no reset pin holds on a quiet clock. -/
theorem flop_quiet_holds_of_no_reset (hq : fires e f.clock = false) (hn : f.resetPin = none) :
    srcFlopNext rho e s idx f = flopHeld s idx f :=
  flop_quiet_holds hq (by simp [resetOn, hn])

/-- An asserted asynchronous reset loads the reset value, with or without an edge. -/
theorem flop_async_reset (hr : resetOn rho f = true) (ha : f.asyncReset = true) :
    srcFlopNext rho e s idx f = mk_bv f.width f.resetValue := by
  simp [srcFlopNext_eq, hr, ha]

/-- A synchronous reset does not act on a quiet clock: the flop holds.
This holds whether or not the reset is asserted, so the statement needs no reset hypothesis. -/
theorem flop_sync_reset_quiet_holds (ha : f.asyncReset = false) (hq : fires e f.clock = false) :
    srcFlopNext rho e s idx f = flopHeld s idx f :=
  flop_quiet_holds hq (by simp [ha])

/-- At an edge an asserted reset loads the reset value, synchronous or not. -/
theorem flop_reset_at_edge (hr : resetOn rho f = true) (he : fires e f.clock = true) :
    srcFlopNext rho e s idx f = mk_bv f.width f.resetValue := by
  simp [srcFlopNext_eq, hr, he]

/-- Reset priority: while the reset acts, the data and enable pins do not matter.
Any other `din` and `enable` give the same next value. -/
theorem flop_reset_priority (hr : resetOn rho f = true) (he : fires e f.clock = true)
    (din' : Nat) (en' : Option Nat) :
    srcFlopNext rho e s idx f = srcFlopNext rho e s idx { f with din := din', enable := en' } := by
  rw [flop_reset_at_edge hr he, flop_reset_at_edge (f := { f with din := din', enable := en' })]
  · exact hr
  · exact he

/-- At an edge, with the reset off and the enable off, the flop holds. -/
theorem flop_disabled_holds (he : fires e f.clock = true) (hr : resetOn rho f = false)
    (hen : enableOn rho f = false) :
    srcFlopNext rho e s idx f = flopHeld s idx f := by
  simp [srcFlopNext_eq, he, hr, hen]

/-- At an edge, with the reset off and the enable on, the flop takes `din` at its width. -/
theorem flop_enabled_loads (he : fires e f.clock = true) (hr : resetOn rho f = false)
    (hen : enableOn rho f = true) :
    srcFlopNext rho e s idx f = bv_resize f.width (rho f.din).asBV := by
  simp [srcFlopNext_eq, he, hr, hen]

/-- No enable pin means always enabled. -/
theorem enableOn_none (h : f.enable = none) : enableOn rho f = true := by
  simp [enableOn, h]

/-- No reset pin means never in reset. -/
theorem resetOn_none (h : f.resetPin = none) : resetOn rho f = false := by
  simp [resetOn, h]

/-- An active-low reset is asserted exactly when its value is zero. -/
theorem resetOn_activeLow {r : Nat} (h : f.resetPin = some r) (hl : f.resetActiveLow = true) :
    resetOn rho f = true ↔ bv_uint (rho r).asBV = 0 := by
  simp [resetOn, h, hl, bv_nonzero]

/-- An active-high reset is asserted exactly when its value is nonzero. -/
theorem resetOn_activeHigh {r : Nat} (h : f.resetPin = some r) (hl : f.resetActiveLow = false) :
    resetOn rho f = true ↔ bv_uint (rho r).asBV ≠ 0 := by
  simp [resetOn, h, hl, bv_nonzero]

/-- The enable pin is on exactly when its value is nonzero. -/
theorem enableOn_some {en : Nat} (h : f.enable = some en) :
    enableOn rho f = true ↔ bv_uint (rho en).asBV ≠ 0 := by
  simp [enableOn, h, bv_nonzero]

end Flops

/-- The read side of an asynchronous reset: in the step where it is asserted, a reader of
the flop's output already sees the reset value. -/
theorem sourceValue_flopQAsync_on (i : RuntimeInput) (s : RuntimeState)
    {idx w ri : Nat} {rv : Int} {al : Bool}
    (h : (if al then !(bv_nonzero (i[ri]?.getD (mk_bv 1 0)))
          else bv_nonzero (i[ri]?.getD (mk_bv 1 0))) = true) :
    sourceValue i s (.flopQAsync idx w ri rv al) = .bv (mk_bv w rv) := by
  simp only [sourceValue, h, ↓reduceIte]

/-- …and while it is not asserted, the reader sees the stored value. -/
theorem sourceValue_flopQAsync_off (i : RuntimeInput) (s : RuntimeState)
    {idx w ri : Nat} {rv : Int} {al : Bool}
    (h : (if al then !(bv_nonzero (i[ri]?.getD (mk_bv 1 0)))
          else bv_nonzero (i[ri]?.getD (mk_bv 1 0))) = false) :
    sourceValue i s (.flopQAsync idx w ri rv al) =
      .bv (bv_resize w (s.flops[idx]?.getD (mk_bv w 0))) := by
  simp only [sourceValue, h, Bool.false_eq_true, ↓reduceIte]

/-! ## Memories: which image a memory takes -/

section MemNext
variable {rho : Nat → CertVal} {e : ClockEdges} {s : RuntimeState} {idx : Nat} {m : MemoryDesc}

/-- One clock, quiet, no asynchronous reset: the memory keeps its stored image. -/
theorem mem_single_quiet_holds (hc : m.commits = #[]) (hq : fires e m.clock = false)
    (ha : m.asyncImg = none) :
    srcMemNext rho e s idx m = s.mems[idx]?.getD (fun _ => mk_bv 0 0) := by
  simp [srcMemNext, hc, hq, srcMemHeld, ha]

/-- One clock, quiet, with an asynchronous whole-array reset: the memory takes the slot that
holds its image with the reset applied. -/
theorem mem_single_quiet_async (hc : m.commits = #[]) (hq : fires e m.clock = false) {a : Nat}
    (ha : m.asyncImg = some a) :
    srcMemNext rho e s idx m = (rho a).asMem := by
  simp [srcMemNext, hc, hq, srcMemHeld, ha]

/-- One clock, firing: the memory takes `nextImg`, the image after every write port. -/
theorem mem_single_fires (hc : m.commits = #[]) (he : fires e m.clock = true) :
    srcMemNext rho e s idx m = (rho m.nextImg).asMem :=
  srcMemNext_fires rho e s idx m hc he

/-- Several clocks, all firing: the memory takes `nextImg`. -/
theorem mem_multi_allFire (hc : m.commits ≠ #[]) (ha : m.allFire e = true) :
    srcMemNext rho e s idx m = (rho m.nextImg).asMem := by
  simp [srcMemNext, hc, ha]

/-- Several clocks, some firing: the memory takes the image of the first commit-table entry
that names exactly the clocks that fire. -/
theorem mem_multi_entry (hc : m.commits ≠ #[]) (ha : m.allFire e = false) {c : MemCommit}
    (hf : m.commits.find? (m.firedIs e) = some c) :
    srcMemNext rho e s idx m = (rho c.nextImg).asMem := by
  simp [srcMemNext, hc, ha, hf]

/-- Several clocks, not all firing, and no entry for the clocks that fire: the memory holds. -/
theorem mem_multi_no_entry_holds (hc : m.commits ≠ #[]) (ha : m.allFire e = false)
    (hf : m.commits.find? (m.firedIs e) = none) :
    srcMemNext rho e s idx m = srcMemHeld rho s idx m := by
  simp [srcMemNext, hc, ha, hf]

end MemNext

/-- A fold of `max` is at least its start. -/
theorem foldl_max_ge_init {α : Type} (g : α → Nat) :
    ∀ (l : List α) (b : Nat), b ≤ l.foldl (fun a c => max a (g c)) b := by
  intro l
  induction l with
  | nil => intro b; exact Nat.le_refl b
  | cons x xs ih =>
      intro b
      exact Nat.le_trans (Nat.le_max_left b (g x)) (ih (max b (g x)))

/-- A fold of `max` is at least every element. -/
theorem foldl_max_ge_mem {α : Type} (g : α → Nat) :
    ∀ (l : List α) (b : Nat) (c : α), c ∈ l → g c ≤ l.foldl (fun a c => max a (g c)) b := by
  intro l
  induction l with
  | nil => intro b c hc; cases hc
  | cons x xs ih =>
      intro b c hc
      cases List.mem_cons.mp hc with
      | inl h =>
          subst h
          exact Nat.le_trans (Nat.le_max_right b (g c)) (foldl_max_ge_init g xs _)
      | inr h => exact ih _ c h

/-- The memory's own clock is one of the ordinals `allFire` and `firedIs` inspect. -/
theorem clock_lt_domSpan (m : MemoryDesc) : m.clock < m.domSpan := by
  have h := foldl_max_ge_init (fun c : MemCommit => c.mask.log2 + 1) m.commits.toList (m.clock + 1)
  simp only [Array.foldl_toList] at h
  exact Nat.lt_of_lt_of_le (Nat.lt_succ_self _) h

/-- Every commit mask is below `2 ^ domSpan`: no entry names a clock past the span. -/
theorem mask_lt_domSpan {m : MemoryDesc} {c : MemCommit} (hc : c ∈ m.commits) :
    c.mask < 2 ^ m.domSpan := by
  have h := foldl_max_ge_mem (fun c : MemCommit => c.mask.log2 + 1) m.commits.toList
    (m.clock + 1) c (Array.mem_toList_iff.mpr hc)
  simp only [Array.foldl_toList] at h
  exact Nat.lt_of_lt_of_le Nat.lt_log2_self (Nat.pow_le_pow_right (by decide) h)

/-- When none of the memory's clocks fires, only a mask-0 entry matches. -/
theorem firedIs_mask_zero {m : MemoryDesc} {e : ClockEdges}
    (hq : ∀ k, m.inDom k = true → fires e k = false) {c : MemCommit} (hc : c ∈ m.commits)
    (hf : m.firedIs e c = true) : c.mask = 0 := by
  apply Nat.eq_of_testBit_eq
  intro k
  rw [Nat.zero_testBit]
  by_cases hk : k < m.domSpan
  · have h := (List.all_eq_true.mp hf) k (List.mem_range.mpr hk)
    cases hd : m.inDom k
    · simpa [hd] using h
    · simpa [hd, hq k hd] using h
  · exact Nat.testBit_lt_two_pow (Nat.lt_of_lt_of_le (mask_lt_domSpan hc)
      (Nat.pow_le_pow_right (by decide) (by omega)))

/-- Several clocks, none firing: the memory holds.
Hypothesis `hmask`: no commit entry has mask 0.
The loader refuses a mask-0 entry (`certShapeError` in `CertIOCodec`).
The commit table is documented to name only non-empty clock sets.
Without it a mask-0 entry would be taken when no clock fires (see the `#guard` below). -/
theorem mem_multi_none_fire_holds {rho : Nat → CertVal} {e : ClockEdges} {s : RuntimeState}
    {idx : Nat} {m : MemoryDesc} (hc : m.commits ≠ #[])
    (hq : ∀ k, m.inDom k = true → fires e k = false) (hmask : ∀ c ∈ m.commits, c.mask ≠ 0) :
    srcMemNext rho e s idx m = srcMemHeld rho s idx m := by
  have hin : m.inDom m.clock = true := by simp [MemoryDesc.inDom]
  have hall : m.allFire e = false := by
    rw [Bool.eq_false_iff]
    intro h
    have h' := (List.all_eq_true.mp h) m.clock (List.mem_range.mpr (clock_lt_domSpan m))
    simp [hin, hq _ hin] at h'
  have hfind : m.commits.find? (m.firedIs e) = none := by
    rw [Array.find?_eq_none]
    intro c hcm hf
    exact hmask c hcm (firedIs_mask_zero hq hcm hf)
  exact mem_multi_no_entry_holds hc hall hfind

/-! ## Write ports -/

section Writes
variable {m : Int → BV} {a d en : BV} {x : Int}

/-- A disabled write leaves the image. -/
theorem write_disabled (h : bv_nonzero en = false) : cert_mem_write m a d en = m := by
  simp [cert_mem_write, h]

/-- An enabled write stores its data, as given, at its address.
The data is stored at its own width: `cert_mem_write` does not cut it to the memory's word
width (see the `#guard` on a wide write below). -/
theorem write_same (h : bv_nonzero en = true) : cert_mem_write m a d en (bv_uint a) = d := by
  simp [cert_mem_write, h]

/-- A write leaves every other address. -/
theorem write_other (hx : x ≠ bv_uint a) : cert_mem_write m a d en x = m x := by
  unfold cert_mem_write
  split <;> simp [hx]

/-- Two writes to one address in a chain: the later one wins, when it is enabled. -/
theorem write_write_same_addr {a1 d1 en1 a2 d2 en2 : BV} (h2 : bv_nonzero en2 = true)
    (ha : bv_uint a1 = bv_uint a2) :
    cert_mem_write (cert_mem_write m a1 d1 en1) a2 d2 en2 = cert_mem_write m a2 d2 en2 := by
  funext y
  by_cases hy : y = bv_uint a2
  · simp [cert_mem_write, h2, hy]
  · rw [write_other hy, write_other hy, write_other (by rw [ha]; exact hy)]

/-- Two writes to different addresses commute, whatever their enables. -/
theorem write_write_comm {a1 d1 en1 a2 d2 en2 : BV} (ha : bv_uint a1 ≠ bv_uint a2) :
    cert_mem_write (cert_mem_write m a1 d1 en1) a2 d2 en2
      = cert_mem_write (cert_mem_write m a2 d2 en2) a1 d1 en1 := by
  funext y
  by_cases h1 : y = bv_uint a1
  · have h2 : y ≠ bv_uint a2 := by rw [h1]; exact ha
    rw [write_other h2, h1]
    by_cases he : bv_nonzero en1
    · simp [cert_mem_write, he]
    · have he' : bv_nonzero en1 = false := by simpa using he
      rw [write_disabled he', write_disabled he', write_other (by rw [← h1]; exact h2)]
  · rw [write_other h1]
    by_cases h2 : y = bv_uint a2
    · subst h2
      by_cases he : bv_nonzero en2
      · simp [cert_mem_write, he]
      · have he' : bv_nonzero en2 = false := by simpa using he
        rw [write_disabled he', write_disabled he', write_other h1]
    · rw [write_other h2, write_other h2, write_other h1]

end Writes

/-- A chain of write ports, applied in port order: each takes the previous image. -/
def writeChain (m : Int → BV) (ws : List (BV × BV × BV)) : Int → BV :=
  ws.foldl (fun img p => cert_mem_write img p.1 p.2.1 p.2.2) m

/-- Word `x` after a chain of writes is the data of the LAST enabled write to `x`,
and the old word when no enabled write names `x`.
This is "the later write in program order wins" for any number of ports. -/
theorem chain_word (x : Int) :
    ∀ (ws : List (BV × BV × BV)) (m : Int → BV),
      writeChain m ws x =
        ((ws.reverse.find? (fun p => bv_nonzero p.2.2 && decide (bv_uint p.1 = x))).map
          (fun p => p.2.1)).getD (m x) := by
  intro ws
  induction ws with
  | nil => intro m; rfl
  | cons p ws ih =>
      intro m
      show writeChain (cert_mem_write m p.1 p.2.1 p.2.2) ws x = _
      rw [ih, List.reverse_cons, List.find?_append]
      cases hf : ws.reverse.find? (fun p => bv_nonzero p.2.2 && decide (bv_uint p.1 = x)) with
      | some q => rfl
      | none =>
          simp only [Option.map_none, Option.getD_none, Option.none_or, List.find?_cons,
            List.find?_nil]
          by_cases hen : bv_nonzero p.2.2 <;> by_cases hx : bv_uint p.1 = x
          · simp [cert_mem_write, hen, hx]
          · simp [cert_mem_write, hen, hx, Ne.symm hx]
          · simp [cert_mem_write, hen, hx]
          · simp [cert_mem_write, hen, hx]

/-! ## Bits of a `BV` -/

open FastBV

/-- The number whose bit `i` is `g i` for `i < w`, and 0 above. -/
def natOfBits : Nat → (Nat → Bool) → Nat
  | 0, _ => 0
  | w + 1, g => natOfBits w g + (if g w then 2 ^ w else 0)

theorem natOfBits_lt (g : Nat → Bool) : ∀ w, natOfBits w g < 2 ^ w := by
  intro w
  induction w with
  | zero => simp [natOfBits]
  | succ w ih =>
      simp only [natOfBits, Nat.pow_succ]
      split <;> omega

theorem natOfBits_testBit (g : Nat → Bool) :
    ∀ w i, (natOfBits w g).testBit i = (decide (i < w) && g i) := by
  intro w
  induction w with
  | zero => intro i; simp [natOfBits]
  | succ w ih =>
      intro i
      have hlt := natOfBits_lt g w
      simp only [natOfBits]
      cases hg : g w
      · simp only [Bool.false_eq_true, ↓reduceIte, Nat.add_zero, ih]
        by_cases hi : i < w
        · simp [hi, Nat.lt_succ_of_lt hi]
        · by_cases hiw : i = w
          · subst hiw; simp [hg]
          · have : ¬ i < w + 1 := by omega
            simp [hi, this]
      · simp only [↓reduceIte]
        rw [Nat.add_comm]
        rcases Nat.lt_trichotomy i w with hi | hi | hi
        · rw [Nat.testBit_two_pow_add_gt hi, ih]
          simp [hi, Nat.lt_succ_of_lt hi]
        · subst hi
          rw [Nat.testBit_two_pow_add_eq, Nat.testBit_lt_two_pow hlt]
          simp [hg]
        · have hbig : 2 ^ w + natOfBits w g < 2 ^ i := by
            have : 2 ^ (w + 1) ≤ 2 ^ i := Nat.pow_le_pow_right (by decide) hi
            rw [Nat.pow_succ] at this
            omega
          rw [Nat.testBit_lt_two_pow hbig]
          have : ¬ i < w + 1 := by omega
          simp [this]

theorem bits_to_int_natOfBits (w : Nat) (g : Nat → Bool) :
    bits_to_int w g = (natOfBits w g : Int) :=
  bits_to_int_eq w g _ (natOfBits_lt g w) (fun i hi => by rw [natOfBits_testBit]; simp [hi])

theorem modNat_natCast (w n : Nat) : modNat w (n : Int) = n % 2 ^ w := modPow2_eq w n

theorem uintNat_mk_bv (w : Nat) (v : Int) : uintNat (mk_bv w v) = modNat w v := by
  show modNat w (v % 2 ^ w) = modNat w v
  rw [emod_two_pow_eq, modNat_natCast, Nat.mod_eq_of_lt (modNat_lt w v)]

/-- Bit `i` of `mk_bv w v`. -/
theorem bv_bit_mk_bv (w : Nat) (v : Int) (i : Nat) :
    bv_bit (mk_bv w v) i = (modNat w v).testBit i := by
  rw [bv_bit_eq, uintNat_mk_bv]

/-- A `BV` built from bits `g` has bit `i` equal to `g i` below its width. -/
theorem bv_bit_bits {w i : Nat} (g : Nat → Bool) (hi : i < w) :
    bv_bit (mk_bv w (bits_to_int w g)) i = g i := by
  rw [bv_bit_mk_bv, bits_to_int_natOfBits, modNat_natCast,
    Nat.mod_eq_of_lt (natOfBits_lt g w), natOfBits_testBit]
  simp [hi]

/-- Two `mk_bv w` values are equal when their bits below `w` agree. -/
theorem mk_bv_ext {w : Nat} {u v : Int} (h : ∀ i, i < w → bv_bit (mk_bv w u) i = bv_bit (mk_bv w v) i) :
    mk_bv w u = mk_bv w v := by
  have hn : modNat w u = modNat w v := by
    apply Nat.eq_of_testBit_eq
    intro i
    by_cases hi : i < w
    · have := h i hi
      rwa [bv_bit_mk_bv, bv_bit_mk_bv] at this
    · rw [Nat.testBit_lt_two_pow (Nat.lt_of_lt_of_le (modNat_lt w u)
          (Nat.pow_le_pow_right (by decide) (by omega))),
        Nat.testBit_lt_two_pow (Nat.lt_of_lt_of_le (modNat_lt w v)
          (Nat.pow_le_pow_right (by decide) (by omega)))]
  simp only [mk_bv, emod_two_pow_eq, hn]

/-- A `BV` whose unsigned value is 0 has no bit set. -/
theorem bv_bit_of_zero {x : BV} (h : bv_nonzero x = false) (j : Nat) : bv_bit x j = false := by
  have hz : bv_uint x = 0 := by simpa [bv_nonzero] using h
  simp [bv_bit, hz]

/-! ## Byte-enabled writes -/

/-- The byte-enabled word has width `w`. -/
theorem masked_update_width (w : Nat) (old new bev : BV) (bw : Nat) :
    (cert_masked_update w old new bev bw).width = w := rfl

/-- Bit `i` of a byte-enabled update is the new data's bit when byte `i / bw` is enabled,
and the old word's bit otherwise. -/
theorem masked_update_bit {w i : Nat} (old new bev : BV) (bw : Nat) (hi : i < w) :
    bv_bit (cert_masked_update w old new bev bw) i =
      if bv_bit bev (i / bw) then bv_bit new i else bv_bit old i :=
  bv_bit_bits _ hi

/-- Above its width, a byte-enabled update has no bit set. -/
theorem masked_update_bit_high {w i : Nat} (old new bev : BV) (bw : Nat) (hi : w ≤ i) :
    bv_bit (cert_masked_update w old new bev bw) i = false := by
  unfold cert_masked_update
  rw [bv_bit_mk_bv]
  exact Nat.testBit_lt_two_pow (Nat.lt_of_lt_of_le (modNat_lt _ _)
    (Nat.pow_le_pow_right (by decide) hi))

/-- Every byte enabled: the word is the new data at width `w`. -/
theorem masked_update_all {w : Nat} (old new bev : BV) (bw : Nat)
    (hall : ∀ i, i < w → bv_bit bev (i / bw) = true) :
    cert_masked_update w old new bev bw = bv_resize w new := by
  apply mk_bv_ext
  intro i hi
  rw [bv_bit_bits _ hi, hall i hi, bv_bit_mk_bv, bv_bit_eq new,
    bv_uint_eq_uintNat, modNat_natCast, Nat.testBit_mod_two_pow]
  simp [hi]

/-- No byte enabled: the word is the old word at width `w`. -/
theorem masked_update_none {w : Nat} (old new bev : BV) (bw : Nat)
    (hnone : ∀ i, i < w → bv_bit bev (i / bw) = false) :
    cert_masked_update w old new bev bw = bv_resize w old := by
  apply mk_bv_ext
  intro i hi
  rw [bv_bit_bits _ hi, hnone i hi, bv_bit_mk_bv, bv_bit_eq old,
    bv_uint_eq_uintNat, modNat_natCast, Nat.testBit_mod_two_pow]
  simp [hi]

section WritesBE
variable {w bw : Nat} {m : Int → BV} {a d bev : BV} {x : Int}

/-- A byte-enabled write with no enable bit set leaves the image. -/
theorem write_be_disabled (h : bv_nonzero bev = false) : cert_mem_write_be w m a d bev bw = m := by
  simp [cert_mem_write_be, h]

/-- A byte-enabled write leaves every other address. -/
theorem write_be_other (hx : x ≠ bv_uint a) : cert_mem_write_be w m a d bev bw x = m x := by
  unfold cert_mem_write_be
  split <;> simp [hx]

/-- Bit `i < w` of the written word: the new data's bit when byte `i / bw` is enabled,
the old word's bit otherwise.
No hypothesis on the enable: with no enable bit set, every byte is disabled and the word is
unchanged, which the formula also says. -/
theorem write_be_bit {i : Nat} (hi : i < w) :
    bv_bit (cert_mem_write_be w m a d bev bw (bv_uint a)) i =
      if bv_bit bev (i / bw) then bv_bit d i else bv_bit (m (bv_uint a)) i := by
  unfold cert_mem_write_be
  cases h : bv_nonzero bev
  · simp [bv_bit_of_zero h]
  · simp only [↓reduceIte]
    exact masked_update_bit _ _ _ _ hi

end WritesBE

/-! ## Reads -/

section Reads
variable {w : Nat} {m : Int → BV} {a en : BV}

/-- A disabled read gives 0. -/
theorem read_disabled (h : bv_nonzero en = false) : cert_mem_read w m a en = mk_bv w 0 := by
  simp [cert_mem_read, h]

/-- An enabled read gives the word at the address, cut or zero-extended to the read width. -/
theorem read_enabled (h : bv_nonzero en = true) :
    cert_mem_read w m a en = bv_resize w (m (bv_uint a)) := by
  simp [cert_mem_read, h]

/-- A read at another address does not see a write. -/
theorem read_after_write_other {ra ren wa wd wen : BV} (ha : bv_uint ra ≠ bv_uint wa) :
    cert_mem_read w (cert_mem_write m wa wd wen) ra ren = cert_mem_read w m ra ren := by
  simp only [cert_mem_read, write_other ha]

/-- A read of the post-write image at the written address sees the new data.
A read of the pre-step image `m` at that address sees `m`'s word instead (`read_enabled`).
This is the one address where the two readings differ. -/
theorem read_after_write_same {ra ren wa wd wen : BV} (hw : bv_nonzero wen = true)
    (ha : bv_uint ra = bv_uint wa) :
    cert_mem_read w (cert_mem_write m wa wd wen) ra ren =
      if bv_nonzero ren then bv_resize w wd else mk_bv w 0 := by
  simp only [cert_mem_read, ha, write_same hw]

/-- The same for a byte-enabled write at another address. -/
theorem read_after_write_be_other {bw : Nat} {ra ren wa wd be : BV} (ha : bv_uint ra ≠ bv_uint wa) :
    cert_mem_read w (cert_mem_write_be w m wa wd be bw) ra ren = cert_mem_read w m ra ren := by
  simp only [cert_mem_read, write_be_other ha]

end Reads

/-! ## Whole-array load -/

section Load
variable {w n : Nat} {m : Int → BV} {bus en : BV} {x : Int}

/-- A disabled load leaves the image. -/
theorem load_disabled (h : bv_nonzero en = false) : cert_mem_load w n m bus en = m := by
  simp [cert_mem_load, h]

/-- A load leaves every entry outside `0 ≤ x < n`. -/
theorem load_other (hx : ¬ (0 ≤ x ∧ x < n)) : cert_mem_load w n m bus en x = m x := by
  unfold cert_mem_load
  split <;> simp [hx]

/-- An enabled load sets entry `x` in `0 ≤ x < n` from the bus. -/
theorem load_entry (h : bv_nonzero en = true) (h0 : 0 ≤ x) (hx : x < n) :
    cert_mem_load w n m bus en x = mk_bv w (bv_uint bus / 2 ^ (x.toNat * w)) := by
  simp [cert_mem_load, h, h0, hx]

/-- Bit `i < w` of loaded entry `x` is bus bit `x * w + i`: entry 0 is in the low bits. -/
theorem load_entry_bit (h : bv_nonzero en = true) (h0 : 0 ≤ x) (hx : x < n) {i : Nat}
    (hi : i < w) :
    bv_bit (cert_mem_load w n m bus en x) i = bv_bit bus (x.toNat * w + i) := by
  rw [load_entry h h0 hx, bv_bit_mk_bv, bv_uint_eq_uintNat, two_pow_int, ← Int.natCast_ediv,
    modNat_natCast, Nat.testBit_mod_two_pow, Nat.testBit_div_two_pow, bv_bit_eq, Nat.add_comm]
  simp [hi]

end Load

/-! ## Power-on image -/

section PowerOn
variable {m : MemoryDesc} {a : Int}

/-- At a nonnegative address the power-on word is `initWord`. -/
theorem powerOnImg_word (h : 0 ≤ a) : m.powerOnImg a = mk_bv m.dw (m.initWord a.toNat : Int) := by
  simp [MemoryDesc.powerOnImg, h]

/-- Inside `init`, the power-on word is the `init` entry at the memory's width. -/
theorem powerOnImg_in (h0 : 0 ≤ a) (hlt : a.toNat < m.init.size) :
    m.powerOnImg a = mk_bv m.dw (m.init[a.toNat] : Int) := by
  simp [MemoryDesc.powerOnImg, h0, MemoryDesc.initWord, Array.getD, hlt]

/-- Past the end of `init`, the power-on word is 0. -/
theorem powerOnImg_past_end (h0 : 0 ≤ a) (hge : m.init.size ≤ a.toNat) :
    m.powerOnImg a = mk_bv m.dw 0 := by
  have : ¬ a.toNat < m.init.size := by omega
  simp [MemoryDesc.powerOnImg, h0, MemoryDesc.initWord, Array.getD, this]

/-- At a negative address, the power-on word is 0. -/
theorem powerOnImg_neg (h : a < 0) : m.powerOnImg a = mk_bv m.dw 0 := by
  have : ¬ 0 ≤ a := by omega
  simp [MemoryDesc.powerOnImg, this]

end PowerOn

/-! ## The step environment: what a read node reads -/

/-- `evalGraphG`'s result satisfies the per-node recurrence on a dependency-ordered list. -/
theorem evalGraphG_fix {V : Type} [NodeSemantics V] (G : GraphCert) :
    ∀ (ns : List Nat) (e : Nat → V), ns.Nodup → DepOrdered G ns →
      (∀ n ∈ ns, (G.nodes n).isSome) →
      ∀ n ∈ ns, evalGraphG ns G e n = evalNodeG G (evalGraphG ns G e) n := by
  intro ns
  induction ns with
  | nil => intro _ _ _ _ n hn; simp at hn
  | cons m ms ih =>
      intro e hnodup hdepord hsome n hn
      have hm_notin : m ∉ ms := (List.nodup_cons.mp hnodup).1
      have hstep : evalGraphG (m :: ms) G e
          = evalGraphG ms G (envSetG e m (evalNodeG G e m)) := rfl
      have hR : ∀ d, d ∉ ms →
          evalGraphG ms G (envSetG e m (evalNodeG G e m)) d = envSetG e m (evalNodeG G e m) d :=
        fun d hd => evalGraphG_not_mem G _ ms d hd
      rw [hstep]
      cases List.mem_cons.mp hn with
      | inl hnm =>
          subst hnm
          rw [hR n hm_notin, envSetG_eq]
          refine evalNodeG_congr_some G e _ n (hsome n List.mem_cons_self) ?_
          intro d hd
          have hdnot : d ∉ (n :: ms) := hdepord.1 d hd
          have hd1 : d ∉ ms := fun hc => hdnot (List.mem_cons_of_mem _ hc)
          have hd2 : d ≠ n := fun hc => hdnot (hc ▸ List.mem_cons_self)
          rw [hR d hd1, envSetG_ne _ _ _ _ hd2]
      | inr hnms =>
          exact ih _ (List.nodup_cons.mp hnodup).2 hdepord.2
            (fun n' h => hsome n' (List.mem_cons_of_mem _ h)) n hnms

/-- A source slot holds its start-of-step value. -/
theorem stepEnv_source (D : DesignCert) (i : RuntimeInput) (s : RuntimeState) {k : Nat}
    {sd : SourceDesc} (hk : D.sources[k]? = some sd) :
    stepEnv D i s k = sourceValue i s sd := by
  have hlt : k < D.sources.size := lt_size_of_getElem? hk
  rw [stepEnv, evalGraphG_not_mem _ _ D.toGraphCert.topo k (DesignCert.source_not_mem_topo hlt)]
  simp [srcEnv, hk]

/-- A memory's source slot holds its PRE-step image. -/
theorem stepEnv_memImg (D : DesignCert) (i : RuntimeInput) (s : RuntimeState)
    {k idx aw dw : Nat} (hk : D.sources[k]? = some (.memImg idx aw dw)) :
    stepEnv D i s k = .mem (s.mems[idx]?.getD (fun _ => mk_bv 0 0)) :=
  stepEnv_source D i s hk

/-- Node `j`'s slot holds its operator applied to its operands' slots. -/
theorem stepEnv_node {D : DesignCert} (hdb : D.DepsBounded) (i : RuntimeInput)
    (s : RuntimeState) {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c) :
    stepEnv D i s (D.sources.size + j) =
      eval_op_cert c.op c.width (c.deps.toList.map (stepEnv D i s)) := by
  have hj : j < D.nodes.size := lt_size_of_getElem? hc
  have hmem : D.slotOfNode j ∈ D.toGraphCert.topo := DesignCert.slotOfNode_mem_topo hj
  have hfix := evalGraphG_fix D.toGraphCert D.toGraphCert.topo (srcEnv D i s)
    DesignCert.topo_nodup (DesignCert.wf_depOrdered hdb) DesignCert.wf_isSome _ hmem
  have hG : D.toGraphCert.nodes (D.slotOfNode j) =
      some { nid := D.slotOfNode j, op := c.op, width := c.width, deps := c.deps.toList } := by
    simp [DesignCert.toGraphCert, DesignCert.nodeAt?_slotOfNode, hc]
  show evalGraphG _ _ _ (D.slotOfNode j) = _
  rw [hfix]
  simp only [evalNodeG, hG]
  rfl

/-- A read node reads whatever image its memory operand holds. -/
theorem stepEnv_read {D : DesignCert} (hdb : D.DepsBounded) (i : RuntimeInput)
    (s : RuntimeState) {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    (hop : c.op = .Op_MemRead) {k ra ren : Nat} (hdeps : c.deps.toList = [k, ra, ren]) :
    stepEnv D i s (D.sources.size + j) =
      .bv (cert_mem_read c.width (stepEnv D i s k).asMem (stepEnv D i s ra).asBV
        (stepEnv D i s ren).asBV) := by
  rw [stepEnv_node hdb i s hc, hop, hdeps]
  rfl

/-- **Read during write.**  A read node whose memory operand is the memory's source slot
reads the PRE-step image.
The right side names no write node and not `nextImg`.
So no write that commits in this step, at any address, changes what the read returns.
This is the old-data rule of a Verilog nonblocking write. -/
theorem stepEnv_read_preStep {D : DesignCert} (hdb : D.DepsBounded) (i : RuntimeInput)
    (s : RuntimeState) {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    (hop : c.op = .Op_MemRead) {k ra ren idx aw dw : Nat} (hdeps : c.deps.toList = [k, ra, ren])
    (hk : D.sources[k]? = some (.memImg idx aw dw)) :
    stepEnv D i s (D.sources.size + j) =
      .bv (cert_mem_read c.width (s.mems[idx]?.getD (fun _ => mk_bv 0 0))
        (stepEnv D i s ra).asBV (stepEnv D i s ren).asBV) := by
  rw [stepEnv_read hdb i s hc hop hdeps, stepEnv_memImg D i s hk]
  rfl

/-- A write node's slot holds its operand image with the write applied.
A read node whose memory operand is such a slot sees the write: the new-data reading. -/
theorem stepEnv_write {D : DesignCert} (hdb : D.DepsBounded) (i : RuntimeInput)
    (s : RuntimeState) {j : Nat} {c : DenseNodeCert} (hc : D.nodes[j]? = some c)
    (hop : c.op = .Op_MemWrite) {k wa wd wen : Nat} (hdeps : c.deps.toList = [k, wa, wd, wen]) :
    stepEnv D i s (D.sources.size + j) =
      .mem (cert_mem_write (stepEnv D i s k).asMem (stepEnv D i s wa).asBV
        (stepEnv D i s wd).asBV (stepEnv D i s wen).asBV) := by
  rw [stepEnv_node hdb i s hc, hop, hdeps]
  rfl

/-! ## The rules inside `interpretDesign` -/

/-- Flop `idx`'s next value is the flop rule on the step environment. -/
theorem interpretDesign_flop (D : DesignCert) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) (idx : Nat) :
    (interpretDesign D e i s).nextState.flops[idx]? =
      D.flops[idx]?.map (srcFlopNext (stepEnv D i s) e s idx) := by
  simp [interpretDesign, Array.getElem?_mapIdx]

/-- Memory `idx`'s next image is the memory rule on the step environment. -/
theorem interpretDesign_mem (D : DesignCert) (e : ClockEdges) (i : RuntimeInput)
    (s : RuntimeState) (idx : Nat) :
    (interpretDesign D e i s).nextState.mems[idx]? =
      D.memories[idx]?.map (srcMemNext (stepEnv D i s) e s idx) := by
  simp [interpretDesign, Array.getElem?_mapIdx]

theorem flops_getElem?_held {s : RuntimeState} {idx : Nat} (f : FlopDesc)
    (h : idx < s.flops.size) : s.flops[idx]? = some (flopHeld s idx f) := by
  simp [flopHeld, Array.getElem?_eq_getElem h]

theorem mems_getElem?_held {s : RuntimeState} {idx : Nat} (h : idx < s.mems.size) :
    some (s.mems[idx]?.getD (fun _ => mk_bv 0 0)) = s.mems[idx]? := by
  simp [Array.getElem?_eq_getElem h]

section Lifted
variable {D : DesignCert} {e : ClockEdges} {i : RuntimeInput} {s : RuntimeState} {idx : Nat}

/-- In `interpretDesign`: a quiet flop keeps its current value, unless an asynchronous reset
is asserted. -/
theorem interpretDesign_flop_quiet_holds (hwf : RuntimeWF D i s) {f : FlopDesc}
    (hf : D.flops[idx]? = some f) (hq : fires e f.clock = false)
    (hr : (resetOn (stepEnv D i s) f && f.asyncReset) = false) :
    (interpretDesign D e i s).nextState.flops[idx]? = s.flops[idx]? := by
  have hlt : idx < s.flops.size := by rw [hwf.flopsSized]; exact lt_size_of_getElem? hf
  rw [interpretDesign_flop, hf, Option.map_some, flop_quiet_holds hq hr, flops_getElem?_held f hlt]

/-- In `interpretDesign`: an asserted asynchronous reset loads the reset value, with or
without an edge. -/
theorem interpretDesign_flop_async_reset {f : FlopDesc} (hf : D.flops[idx]? = some f)
    (hr : resetOn (stepEnv D i s) f = true) (ha : f.asyncReset = true) :
    (interpretDesign D e i s).nextState.flops[idx]? = some (mk_bv f.width f.resetValue) := by
  rw [interpretDesign_flop, hf, Option.map_some, flop_async_reset hr ha]

/-- In `interpretDesign`: a synchronous reset on a quiet clock leaves the flop, asserted or not. -/
theorem interpretDesign_flop_sync_reset_quiet_holds (hwf : RuntimeWF D i s) {f : FlopDesc}
    (hf : D.flops[idx]? = some f) (ha : f.asyncReset = false) (hq : fires e f.clock = false) :
    (interpretDesign D e i s).nextState.flops[idx]? = s.flops[idx]? :=
  interpretDesign_flop_quiet_holds hwf hf hq (by simp [ha])

/-- In `interpretDesign`: at an edge an asserted reset loads the reset value. -/
theorem interpretDesign_flop_reset_at_edge {f : FlopDesc} (hf : D.flops[idx]? = some f)
    (hr : resetOn (stepEnv D i s) f = true) (he : fires e f.clock = true) :
    (interpretDesign D e i s).nextState.flops[idx]? = some (mk_bv f.width f.resetValue) := by
  rw [interpretDesign_flop, hf, Option.map_some, flop_reset_at_edge hr he]

/-- In `interpretDesign`: at an edge with reset and enable off, the flop keeps its value. -/
theorem interpretDesign_flop_disabled_holds (hwf : RuntimeWF D i s) {f : FlopDesc}
    (hf : D.flops[idx]? = some f) (he : fires e f.clock = true)
    (hr : resetOn (stepEnv D i s) f = false) (hen : enableOn (stepEnv D i s) f = false) :
    (interpretDesign D e i s).nextState.flops[idx]? = s.flops[idx]? := by
  have hlt : idx < s.flops.size := by rw [hwf.flopsSized]; exact lt_size_of_getElem? hf
  rw [interpretDesign_flop, hf, Option.map_some, flop_disabled_holds he hr hen,
    flops_getElem?_held f hlt]

/-- In `interpretDesign`: at an edge with reset off and enable on, the flop takes `din`. -/
theorem interpretDesign_flop_enabled_loads {f : FlopDesc} (hf : D.flops[idx]? = some f)
    (he : fires e f.clock = true) (hr : resetOn (stepEnv D i s) f = false)
    (hen : enableOn (stepEnv D i s) f = true) :
    (interpretDesign D e i s).nextState.flops[idx]? =
      some (bv_resize f.width (stepEnv D i s f.din).asBV) := by
  rw [interpretDesign_flop, hf, Option.map_some, flop_enabled_loads he hr hen]

/-- In `interpretDesign`: a quiet one-clock memory with no asynchronous reset keeps its
current image. -/
theorem interpretDesign_mem_quiet_holds (hwf : RuntimeWF D i s) {m : MemoryDesc}
    (hm : D.memories[idx]? = some m) (hc : m.commits = #[]) (hq : fires e m.clock = false)
    (ha : m.asyncImg = none) :
    (interpretDesign D e i s).nextState.mems[idx]? = s.mems[idx]? := by
  have hlt : idx < s.mems.size := by rw [hwf.memsSized]; exact lt_size_of_getElem? hm
  rw [interpretDesign_mem, hm, Option.map_some, mem_single_quiet_holds hc hq ha]
  exact mems_getElem?_held hlt

/-- In `interpretDesign`: a quiet one-clock memory with an asynchronous whole-array reset
takes the reset image slot. -/
theorem interpretDesign_mem_quiet_async {m : MemoryDesc} (hm : D.memories[idx]? = some m)
    (hc : m.commits = #[]) (hq : fires e m.clock = false) {a : Nat} (ha : m.asyncImg = some a) :
    (interpretDesign D e i s).nextState.mems[idx]? = some (stepEnv D i s a).asMem := by
  rw [interpretDesign_mem, hm, Option.map_some, mem_single_quiet_async hc hq ha]

/-- In `interpretDesign`: a one-clock memory whose clock fires takes `nextImg`. -/
theorem interpretDesign_mem_fires {m : MemoryDesc} (hm : D.memories[idx]? = some m)
    (hc : m.commits = #[]) (he : fires e m.clock = true) :
    (interpretDesign D e i s).nextState.mems[idx]? = some (stepEnv D i s m.nextImg).asMem := by
  rw [interpretDesign_mem, hm, Option.map_some, mem_single_fires hc he]

/-- In `interpretDesign`: a several-clock memory none of whose clocks fires keeps its
current image (no asynchronous reset; no mask-0 entry, as the loader requires). -/
theorem interpretDesign_mem_multi_none_fire_holds (hwf : RuntimeWF D i s) {m : MemoryDesc}
    (hm : D.memories[idx]? = some m) (hc : m.commits ≠ #[])
    (hq : ∀ k, m.inDom k = true → fires e k = false) (hmask : ∀ c ∈ m.commits, c.mask ≠ 0)
    (ha : m.asyncImg = none) :
    (interpretDesign D e i s).nextState.mems[idx]? = s.mems[idx]? := by
  have hlt : idx < s.mems.size := by rw [hwf.memsSized]; exact lt_size_of_getElem? hm
  rw [interpretDesign_mem, hm, Option.map_some, mem_multi_none_fire_holds hc hq hmask,
    srcMemHeld_of_none _ _ _ _ ha]
  exact mems_getElem?_held hlt

/-- In `interpretDesign`: a several-clock memory takes the image of the entry for exactly the
clocks that fire. -/
theorem interpretDesign_mem_multi_entry {m : MemoryDesc} (hm : D.memories[idx]? = some m)
    (hc : m.commits ≠ #[]) (ha : m.allFire e = false) {c : MemCommit}
    (hf : m.commits.find? (m.firedIs e) = some c) :
    (interpretDesign D e i s).nextState.mems[idx]? = some (stepEnv D i s c.nextImg).asMem := by
  rw [interpretDesign_mem, hm, Option.map_some, mem_multi_entry hc ha hf]

end Lifted

/-! ## One clock: every declared clock fires -/

/-- The one-clock flop rule, with its conditions named. -/
theorem srcFlopNextLegacy_eq (rho : Nat → CertVal) (s : RuntimeState) (idx : Nat) (f : FlopDesc) :
    srcFlopNextLegacy rho s idx f =
      if resetOn rho f then mk_bv f.width f.resetValue
      else if enableOn rho f then bv_resize f.width (rho f.din).asBV
      else flopHeld s idx f := rfl

/-- Under the all-fire edge vector a flop follows the one-clock rule. -/
theorem flop_allEdges {D : DesignCert} (rho : Nat → CertVal) (s : RuntimeState) (idx : Nat)
    {f : FlopDesc} (hf : f.clock < D.clocks.size) :
    srcFlopNext rho (allEdges D) s idx f = srcFlopNextLegacy rho s idx f :=
  srcFlopNext_fires _ _ _ _ _ (fires_allEdges hf)

/-- Under the all-fire edge vector, a synchronous and an asynchronous reset act the same. -/
theorem flop_allEdges_async_irrelevant {D : DesignCert} (rho : Nat → CertVal) (s : RuntimeState)
    (idx : Nat) {f : FlopDesc} (hf : f.clock < D.clocks.size) (b : Bool) :
    srcFlopNext rho (allEdges D) s idx f =
      srcFlopNext rho (allEdges D) s idx { f with asyncReset := b } := by
  rw [flop_allEdges rho s idx hf, flop_allEdges (f := { f with asyncReset := b }) rho s idx hf]
  rfl

/-- In `interpretDesign` under the all-fire edge vector: reset, else `din` if enabled, else hold. -/
theorem interpretDesign_allEdges_flop {D : DesignCert} (i : RuntimeInput) (s : RuntimeState)
    {idx : Nat} {f : FlopDesc} (hf : D.flops[idx]? = some f) (hclk : f.clock < D.clocks.size) :
    (interpretDesign D (allEdges D) i s).nextState.flops[idx]? =
      some (if resetOn (stepEnv D i s) f then mk_bv f.width f.resetValue
            else if enableOn (stepEnv D i s) f then bv_resize f.width (stepEnv D i s f.din).asBV
            else flopHeld s idx f) := by
  rw [interpretDesign_flop, hf, Option.map_some, flop_allEdges _ _ _ hclk, srcFlopNextLegacy_eq]

/-- In `interpretDesign` under the all-fire edge vector: every memory takes `nextImg`. -/
theorem interpretDesign_allEdges_mem {D : DesignCert} (i : RuntimeInput) (s : RuntimeState)
    {idx : Nat} {m : MemoryDesc} (hm : D.memories[idx]? = some m) (hclk : m.clock < D.clocks.size)
    (hcm : ∀ c ∈ m.commits, c.mask < 2 ^ D.clocks.size) :
    (interpretDesign D (allEdges D) i s).nextState.mems[idx]? =
      some (stepEnv D i s m.nextImg).asMem := by
  rw [interpretDesign_mem, hm, Option.map_some, srcMemNext_allEdges _ _ _ hclk hcm]

/-! ## Corner checks on concrete designs and values -/

/-- Two flops on two clocks.
Flop 0: synchronous active-high reset (input 1), enable (input 2), clock 0, reset value 5.
Flop 1: asynchronous active-low reset (input 3), no enable, clock 1, reset value 9.
Both load input 0. -/
def flopDemo : DesignCert where
  sources  := #[.input 0 8, .input 1 1, .input 2 1, .input 3 1, .flopQ 0 8, .flopQ 1 8]
  nodes    := #[]
  outputs  := #[]
  flops    := #[{ width := 8, din := 0, enable := some 2, resetPin := some 1, resetValue := 5,
                  resetActiveLow := false, clock := 0 },
                { width := 8, din := 0, enable := none, resetPin := some 3, resetValue := 9,
                  resetActiveLow := true, clock := 1, asyncReset := true }]
  memories := #[]
  clocks   := #[{ name := "c0" }, { name := "c1" }]

/-- Inputs: data, rst, en, rst_n. -/
def flopIn (data rst en rstN : Int) : RuntimeInput :=
  #[mk_bv 8 data, mk_bv 1 rst, mk_bv 1 en, mk_bv 1 rstN]

/-- Stored values 1 and 2. -/
def flopSt : RuntimeState := { flops := #[mk_bv 8 1, mk_bv 8 2], mems := #[] }

-- No clock fires, both resets asserted: the synchronous flop holds, the asynchronous one resets.
#guard (interpretDesign flopDemo #[false, false] (flopIn 0x42 1 1 0) flopSt).nextState.flops
  == #[mk_bv 8 1, mk_bv 8 9]
-- Clock 0 fires, both resets asserted, enable on: reset wins over data at the edge.
#guard (interpretDesign flopDemo #[true, false] (flopIn 0x42 1 1 0) flopSt).nextState.flops
  == #[mk_bv 8 5, mk_bv 8 9]
-- Clock 0 fires with reset asserted and enable OFF: reset still wins.
#guard (interpretDesign flopDemo #[true, false] (flopIn 0x42 1 0 1) flopSt).nextState.flops
  == #[mk_bv 8 5, mk_bv 8 2]
-- Both clocks fire, no reset, enable off: flop 0 holds; flop 1 has no enable and loads.
#guard (interpretDesign flopDemo #[true, true] (flopIn 0x42 0 0 1) flopSt).nextState.flops
  == #[mk_bv 8 1, mk_bv 8 0x42]
-- Clock 0 fires, no reset, enable on: flop 0 loads; flop 1's clock is quiet, so it holds.
#guard (interpretDesign flopDemo #[true, false] (flopIn 0x42 0 1 1) flopSt).nextState.flops
  == #[mk_bv 8 0x42, mk_bv 8 2]
-- Polarity: rst_n = 1 is not asserted, rst_n = 0 is; rst = 0 is not asserted, rst = 1 is.
#guard (interpretDesign flopDemo #[false, false] (flopIn 0x42 1 1 1) flopSt).nextState.flops
  == #[mk_bv 8 1, mk_bv 8 2]
-- An undeclared or missing edge entry never fires.
#guard (interpretDesign flopDemo #[] (flopIn 0x42 0 1 1) flopSt).nextState.flops
  == #[mk_bv 8 1, mk_bv 8 2]
-- The all-fire vector of a two-clock design.
#guard allEdges flopDemo == #[true, true]

/-- One memory (4-bit address, 8-bit words) with one write port, read twice.
Slot 0: the memory's pre-step image. Slots 1-4: addr, data, wen, ren.
Node 0 (slot 5) reads the pre-step image. Node 1 (slot 6) writes.
Node 2 (slot 7) reads the post-write image. The memory's next image is slot 6. -/
def memDemo : DesignCert where
  sources  := #[.memImg 0 4 8, .input 0 4, .input 1 8, .input 2 1, .input 3 1]
  nodes    := #[{ op := .Op_MemRead,  width := 8, deps := #[0, 1, 4] },
                { op := .Op_MemWrite, width := 8, deps := #[0, 1, 2, 3] },
                { op := .Op_MemRead,  width := 8, deps := #[6, 1, 4] }]
  outputs  := #[{ slot := 5, width := 8 }, { slot := 7, width := 8 }]
  flops    := #[]
  memories := #[{ aw := 4, dw := 8, nextImg := 6 }]

/-- Word 3 holds 0x11; every other word is 0. -/
def memSt : RuntimeState :=
  { flops := #[], mems := #[fun x => if x = 3 then mk_bv 8 0x11 else mk_bv 8 0] }

def memIn (addr data wen ren : Int) : RuntimeInput :=
  #[mk_bv 4 addr, mk_bv 8 data, mk_bv 1 wen, mk_bv 1 ren]

-- Read during write at address 3: the pre-step read returns the OLD word 0x11;
-- a read of the write node's image returns the new word 0x22.
#guard (interpretDesign memDemo #[true] (memIn 3 0x22 1 1) memSt).outputs
  == #[mk_bv 8 0x11, mk_bv 8 0x22]
-- The write commits: word 3 becomes 0x22, word 4 stays 0.
#guard ((interpretDesign memDemo #[true] (memIn 3 0x22 1 1) memSt).nextState.mems[0]! 3)
  == mk_bv 8 0x22
#guard ((interpretDesign memDemo #[true] (memIn 3 0x22 1 1) memSt).nextState.mems[0]! 4)
  == mk_bv 8 0
-- The clock is quiet: the write does not commit, word 3 stays 0x11.
#guard ((interpretDesign memDemo #[false] (memIn 3 0x22 1 1) memSt).nextState.mems[0]! 3)
  == mk_bv 8 0x11
-- A disabled read gives 0, even of a nonzero word.
#guard (interpretDesign memDemo #[true] (memIn 3 0x22 0 0) memSt).outputs
  == #[mk_bv 8 0, mk_bv 8 0]

-- A write to address 3, then another to address 3: the later one wins.
#guard (cert_mem_write (cert_mem_write (fun _ => mk_bv 8 0) (mk_bv 4 3) (mk_bv 8 7) (mk_bv 1 1))
    (mk_bv 4 3) (mk_bv 8 9) (mk_bv 1 1)) 3 == mk_bv 8 9
-- The same chain with the later write disabled: the earlier one stays.
#guard (cert_mem_write (cert_mem_write (fun _ => mk_bv 8 0) (mk_bv 4 3) (mk_bv 8 7) (mk_bv 1 1))
    (mk_bv 4 3) (mk_bv 8 9) (mk_bv 1 0)) 3 == mk_bv 8 7
-- The address is taken unsigned at its own width: address 19 at 4 bits is word 3.
#guard (cert_mem_write (fun _ => mk_bv 8 0) (mk_bv 4 19) (mk_bv 8 7) (mk_bv 1 1)) 3 == mk_bv 8 7
-- A chain of three ports through `writeChain`: the last enabled write to word 3 wins.
#guard writeChain (fun _ => mk_bv 8 0)
    [(mk_bv 4 3, mk_bv 8 1, mk_bv 1 1), (mk_bv 4 3, mk_bv 8 2, mk_bv 1 1),
     (mk_bv 4 3, mk_bv 8 3, mk_bv 1 0)] 3 == mk_bv 8 2

-- A plain write stores its data at the data's own width, not the memory's word width.
-- Here a 16-bit datum goes into a memory whose other words are 8 bits wide.
-- A read at 8 bits sees 0x34, as Verilog's `mem[a] <= d` with 8-bit words would store.
-- A read at 16 bits sees 0x1234, which an 8-bit Verilog memory cannot hold.
#guard cert_mem_read 8 (cert_mem_write (fun _ => mk_bv 8 0) (mk_bv 4 3) (mk_bv 16 0x1234)
    (mk_bv 1 1)) (mk_bv 4 3) (mk_bv 1 1) == mk_bv 8 0x34
#guard cert_mem_read 16 (cert_mem_write (fun _ => mk_bv 8 0) (mk_bv 4 3) (mk_bv 16 0x1234)
    (mk_bv 1 1)) (mk_bv 4 3) (mk_bv 1 1) == mk_bv 16 0x1234

-- Byte enable 0b0101, byte width 8: bytes 0 and 2 come from the new data.
#guard cert_masked_update 32 (mk_bv 32 0x11223344) (mk_bv 32 0xAABBCCDD) (mk_bv 4 0b0101) 8
  == mk_bv 32 0x11BB33DD
#guard (cert_mem_write_be 32 (fun _ => mk_bv 32 0x11223344) (mk_bv 4 3) (mk_bv 32 0xAABBCCDD)
    (mk_bv 4 0b0101) 8) 3 == mk_bv 32 0x11BB33DD
#guard (cert_mem_write_be 32 (fun _ => mk_bv 32 0x11223344) (mk_bv 4 3) (mk_bv 32 0xAABBCCDD)
    (mk_bv 4 0b0101) 8) 4 == mk_bv 32 0x11223344
-- Byte enable 0: the word is untouched.
#guard (cert_mem_write_be 32 (fun _ => mk_bv 32 0x11223344) (mk_bv 4 3) (mk_bv 32 0xAABBCCDD)
    (mk_bv 4 0) 8) 3 == mk_bv 32 0x11223344

-- Whole-array load of 2 entries of 8 bits from bus 0xBBAA: entry 0 is the low byte.
#guard (cert_mem_load 8 2 (fun _ => mk_bv 8 0x55) (mk_bv 16 0xBBAA) (mk_bv 1 1)) 0 == mk_bv 8 0xAA
#guard (cert_mem_load 8 2 (fun _ => mk_bv 8 0x55) (mk_bv 16 0xBBAA) (mk_bv 1 1)) 1 == mk_bv 8 0xBB
#guard (cert_mem_load 8 2 (fun _ => mk_bv 8 0x55) (mk_bv 16 0xBBAA) (mk_bv 1 1)) 2 == mk_bv 8 0x55
#guard (cert_mem_load 8 2 (fun _ => mk_bv 8 0x55) (mk_bv 16 0xBBAA) (mk_bv 1 1)) (-1) == mk_bv 8 0x55
#guard (cert_mem_load 8 2 (fun _ => mk_bv 8 0x55) (mk_bv 16 0xBBAA) (mk_bv 1 0)) 0 == mk_bv 8 0x55

-- Power-on image: `init` entries at the memory's width, 0 past the end and below 0.
#guard ({ aw := 2, dw := 4, nextImg := 0, init := #[7, 17] } : MemoryDesc).powerOnImg 0 == mk_bv 4 7
#guard ({ aw := 2, dw := 4, nextImg := 0, init := #[7, 17] } : MemoryDesc).powerOnImg 1 == mk_bv 4 1
#guard ({ aw := 2, dw := 4, nextImg := 0, init := #[7, 17] } : MemoryDesc).powerOnImg 2 == mk_bv 4 0
#guard ({ aw := 2, dw := 4, nextImg := 0, init := #[7, 17] } : MemoryDesc).powerOnImg (-1) == mk_bv 4 0

/-- A memory on clocks 0 and 1.
Entry mask 1 (clock 0 alone) names slot 1; entry mask 2 (clock 1 alone) names slot 2.
`nextImg` (both clocks) is slot 3. -/
def multiMem : MemoryDesc :=
  { aw := 2, dw := 8, nextImg := 3, clock := 0,
    commits := #[{ mask := 1, nextImg := 1 }, { mask := 2, nextImg := 2 }] }

/-- Slot `k` holds an image whose every word is `k`. -/
def slotImgs : Nat → CertVal := fun k => .mem (fun _ => mk_bv 8 k)

/-- The stored image: every word is 0xEE. -/
def multiSt : RuntimeState := { flops := #[], mems := #[fun _ => mk_bv 8 0xEE] }

#guard srcMemNext slotImgs #[true, true] multiSt 0 multiMem 0 == mk_bv 8 3
#guard srcMemNext slotImgs #[true, false] multiSt 0 multiMem 0 == mk_bv 8 1
#guard srcMemNext slotImgs #[false, true] multiSt 0 multiMem 0 == mk_bv 8 2
#guard srcMemNext slotImgs #[false, false] multiSt 0 multiMem 0 == mk_bv 8 0xEE
-- The same memory with an asynchronous reset image in slot 4: a quiet step takes it.
#guard srcMemNext slotImgs #[false, false] multiSt 0 { multiMem with asyncImg := some 4 } 0
  == mk_bv 8 4
-- A table with no entry for clock 1 alone: the memory holds when only clock 1 fires.
#guard srcMemNext slotImgs #[false, true] multiSt 0
    { multiMem with commits := #[{ mask := 1, nextImg := 1 }] } 0 == mk_bv 8 0xEE
-- A mask-0 entry (which the loader refuses) is taken when no clock fires.
-- This is why `mem_multi_none_fire_holds` assumes every mask is nonzero.
#guard srcMemNext slotImgs #[false, false] multiSt 0
    { multiMem with commits := #[{ mask := 0, nextImg := 5 }, { mask := 1, nextImg := 1 }] } 0
  == mk_bv 8 5

end StateSpec
end Compiler

-- the axiom gate: every theorem above uses only propext, Classical.choice and Quot.sound
#audit_axioms Compiler.StateSpec.srcFlopNext_eq
#audit_axioms Compiler.StateSpec.lt_size_of_getElem?
#audit_axioms Compiler.StateSpec.flop_quiet_holds
#audit_axioms Compiler.StateSpec.flop_quiet_holds_of_no_reset
#audit_axioms Compiler.StateSpec.flop_async_reset
#audit_axioms Compiler.StateSpec.flop_sync_reset_quiet_holds
#audit_axioms Compiler.StateSpec.flop_reset_at_edge
#audit_axioms Compiler.StateSpec.flop_reset_priority
#audit_axioms Compiler.StateSpec.flop_disabled_holds
#audit_axioms Compiler.StateSpec.flop_enabled_loads
#audit_axioms Compiler.StateSpec.enableOn_none
#audit_axioms Compiler.StateSpec.resetOn_none
#audit_axioms Compiler.StateSpec.resetOn_activeLow
#audit_axioms Compiler.StateSpec.resetOn_activeHigh
#audit_axioms Compiler.StateSpec.enableOn_some
#audit_axioms Compiler.StateSpec.sourceValue_flopQAsync_on
#audit_axioms Compiler.StateSpec.sourceValue_flopQAsync_off
#audit_axioms Compiler.StateSpec.mem_single_quiet_holds
#audit_axioms Compiler.StateSpec.mem_single_quiet_async
#audit_axioms Compiler.StateSpec.mem_single_fires
#audit_axioms Compiler.StateSpec.mem_multi_allFire
#audit_axioms Compiler.StateSpec.mem_multi_entry
#audit_axioms Compiler.StateSpec.mem_multi_no_entry_holds
#audit_axioms Compiler.StateSpec.foldl_max_ge_init
#audit_axioms Compiler.StateSpec.foldl_max_ge_mem
#audit_axioms Compiler.StateSpec.clock_lt_domSpan
#audit_axioms Compiler.StateSpec.mask_lt_domSpan
#audit_axioms Compiler.StateSpec.firedIs_mask_zero
#audit_axioms Compiler.StateSpec.mem_multi_none_fire_holds
#audit_axioms Compiler.StateSpec.write_disabled
#audit_axioms Compiler.StateSpec.write_same
#audit_axioms Compiler.StateSpec.write_other
#audit_axioms Compiler.StateSpec.write_write_same_addr
#audit_axioms Compiler.StateSpec.write_write_comm
#audit_axioms Compiler.StateSpec.chain_word
#audit_axioms Compiler.StateSpec.natOfBits_lt
#audit_axioms Compiler.StateSpec.natOfBits_testBit
#audit_axioms Compiler.StateSpec.bits_to_int_natOfBits
#audit_axioms Compiler.StateSpec.modNat_natCast
#audit_axioms Compiler.StateSpec.uintNat_mk_bv
#audit_axioms Compiler.StateSpec.bv_bit_mk_bv
#audit_axioms Compiler.StateSpec.bv_bit_bits
#audit_axioms Compiler.StateSpec.mk_bv_ext
#audit_axioms Compiler.StateSpec.bv_bit_of_zero
#audit_axioms Compiler.StateSpec.masked_update_width
#audit_axioms Compiler.StateSpec.masked_update_bit
#audit_axioms Compiler.StateSpec.masked_update_bit_high
#audit_axioms Compiler.StateSpec.masked_update_all
#audit_axioms Compiler.StateSpec.masked_update_none
#audit_axioms Compiler.StateSpec.write_be_disabled
#audit_axioms Compiler.StateSpec.write_be_other
#audit_axioms Compiler.StateSpec.write_be_bit
#audit_axioms Compiler.StateSpec.read_disabled
#audit_axioms Compiler.StateSpec.read_enabled
#audit_axioms Compiler.StateSpec.read_after_write_other
#audit_axioms Compiler.StateSpec.read_after_write_same
#audit_axioms Compiler.StateSpec.read_after_write_be_other
#audit_axioms Compiler.StateSpec.load_disabled
#audit_axioms Compiler.StateSpec.load_other
#audit_axioms Compiler.StateSpec.load_entry
#audit_axioms Compiler.StateSpec.load_entry_bit
#audit_axioms Compiler.StateSpec.powerOnImg_word
#audit_axioms Compiler.StateSpec.powerOnImg_in
#audit_axioms Compiler.StateSpec.powerOnImg_past_end
#audit_axioms Compiler.StateSpec.powerOnImg_neg
#audit_axioms Compiler.StateSpec.evalGraphG_fix
#audit_axioms Compiler.StateSpec.stepEnv_source
#audit_axioms Compiler.StateSpec.stepEnv_memImg
#audit_axioms Compiler.StateSpec.stepEnv_node
#audit_axioms Compiler.StateSpec.stepEnv_read
#audit_axioms Compiler.StateSpec.stepEnv_read_preStep
#audit_axioms Compiler.StateSpec.stepEnv_write
#audit_axioms Compiler.StateSpec.interpretDesign_flop
#audit_axioms Compiler.StateSpec.interpretDesign_mem
#audit_axioms Compiler.StateSpec.flops_getElem?_held
#audit_axioms Compiler.StateSpec.mems_getElem?_held
#audit_axioms Compiler.StateSpec.interpretDesign_flop_quiet_holds
#audit_axioms Compiler.StateSpec.interpretDesign_flop_async_reset
#audit_axioms Compiler.StateSpec.interpretDesign_flop_sync_reset_quiet_holds
#audit_axioms Compiler.StateSpec.interpretDesign_flop_reset_at_edge
#audit_axioms Compiler.StateSpec.interpretDesign_flop_disabled_holds
#audit_axioms Compiler.StateSpec.interpretDesign_flop_enabled_loads
#audit_axioms Compiler.StateSpec.interpretDesign_mem_quiet_holds
#audit_axioms Compiler.StateSpec.interpretDesign_mem_quiet_async
#audit_axioms Compiler.StateSpec.interpretDesign_mem_fires
#audit_axioms Compiler.StateSpec.interpretDesign_mem_multi_none_fire_holds
#audit_axioms Compiler.StateSpec.interpretDesign_mem_multi_entry
#audit_axioms Compiler.StateSpec.srcFlopNextLegacy_eq
#audit_axioms Compiler.StateSpec.flop_allEdges
#audit_axioms Compiler.StateSpec.flop_allEdges_async_irrelevant
#audit_axioms Compiler.StateSpec.interpretDesign_allEdges_flop
#audit_axioms Compiler.StateSpec.interpretDesign_allEdges_mem
