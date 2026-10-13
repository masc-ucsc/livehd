/-
# The executable runtime state / input / edge policy

`admitsB` (HardwareMemAdmit) is a STATIC check: it looks at the design only.
`admits_mainM_resolved` (HardwareMemSlotTy) still carries six RUNTIME premises —
the memory shape of the incoming state, how far the sources read into the input
and flop vectors, the flop-array size, and the two clock-ordinal bounds against
the edge vector.  This module makes those executable, and strengthens the memory
shape from a LENGTH to a word-level statement.

## What the shape says now

`StateShapeOK` says only `words.length = 2 ^ aw`.  That is what the address fit
needs, and nothing more, so it was all that was proved.  It is not what a real
state must satisfy: an image whose words are 7 bits wide against a `dw = 32`
memory has the right length and the wrong contents.  `MemShapeOK` adds the two
missing facts — EVERY word is `dw` wide, and so is the default, which is the
word every out-of-range read returns — and `MemShapeOK.toFullTable` shows it
still implies the length statement every existing proof consumes.

## What is NOT claimed here

Word-level preservation IS proved, in §7: `admits_run_pres` concludes
`stateOKB D r.nextState = true`, not merely the length-level `StateShapeOK`.
`admits_run` itself is left at the weaker conclusion on purpose, so the theorem
earlier call sites consume keeps its exact meaning.

§9 closes the remaining policy-scope gap: `stateOKB` pins the flop VECTOR's
length but not the WIDTH of the values in it, so on its own it accepts a 7-bit
value against a 32-bit flop descriptor.  That was a gap in what the policy SAYS,
never a contradiction of the adequacy equation, and `flopShapeOKB` closes it
without altering any existing predicate.  `SourceSupported`, `OpSupported` and
`memFree` are untouched and the public route still refuses memory-bearing designs.
-/
import LeanSemanticPrimitives.Projection.HardwareMemSlotTy

namespace Projection
namespace HwMem
open Surface Compiler Projection.Hw

/-! ## 1. The memory shape, at word level -/

/-- A declared memory's image: the full table, every word at the declared data
width, and the default — the word every out-of-range read returns — at that
width too. -/
def MemShapeOK (md : MemoryDesc) (m : MemImg) : Prop :=
  m.words.length = 2 ^ md.aw
  ∧ (∀ w ∈ m.words, w.width = md.dw)
  ∧ m.dflt.width = md.dw

theorem MemShapeOK.toFullTable {md : MemoryDesc} {m : MemImg}
    (h : MemShapeOK md m) : MemImg.FullTable md.aw m := h.1

def memImgOKB (md : MemoryDesc) (m : MemImg) : Bool :=
  (m.words.length == 2 ^ md.aw)
  && m.words.all (fun w => w.width == md.dw)
  && (m.dflt.width == md.dw)

theorem memImgOKB_sound {md : MemoryDesc} {m : MemImg}
    (h : memImgOKB md m = true) : MemShapeOK md m := by
  unfold memImgOKB at h
  simp only [Bool.and_eq_true, beq_iff_eq, List.all_eq_true] at h
  exact ⟨h.1.1, fun w hw => by simpa using h.1.2 w hw, h.2⟩

/-! ## 2. The policy, as four executable checks -/

/-- The incoming state: every DECLARED memory has an image of the declared
shape, and the flop vector is at least as long as the design's flop array. -/
def stateOKB (D : DesignCert) (st : StateM) : Bool :=
  decide (D.flops.size ≤ st.flops.size)
  && (List.range D.memories.size).all (fun idx =>
       match D.memories[idx]?, st.mems[idx]? with
       | some md, some m => memImgOKB md m
       | _,       _      => false)

/-- How far the sources read into the input vector. -/
def inputOKB (D : DesignCert) (n : Nat) : Bool :=
  D.sources.toList.all (fun sd => decide (SourceInputBound sd n))

/-- …and into the flop vector. -/
def flopBoundOKB (D : DesignCert) (n : Nat) : Bool :=
  D.sources.toList.all (fun sd => decide (SourceFlopBound sd n))

/-- Every flop and every memory commits on an ordinal the edge vector actually
carries, so `firesAt` is not reading past it. -/
def edgesOKB (D : DesignCert) (e : ClockEdges) : Bool :=
  D.flops.toList.all (fun f => decide (f.clock < e.size))
  && D.memories.toList.all (fun md => decide (md.clock < e.size))

def runtimeOKB (D : DesignCert) (i : RuntimeInput) (st : StateM)
    (e : ClockEdges) : Bool :=
  stateOKB D st && inputOKB D i.size && flopBoundOKB D st.flops.size
  && edgesOKB D e

/-! ## 3. Each check discharges its premise -/

private theorem mem_lt_of_getElem? {α : Type _} {a : Array α} {idx : Nat} {v : α}
    (h : a[idx]? = some v) : idx < a.size := by
  rcases Nat.lt_or_ge idx a.size with hlt | hge
  · exact hlt
  · rw [Array.getElem?_eq_none hge] at h; exact absurd h (by simp)

theorem stateOKB_shapeW {D : DesignCert} {st : StateM} (h : stateOKB D st = true) :
    ∀ (idx : Nat) (md : MemoryDesc), D.memories[idx]? = some md →
      ∃ m, st.mems[idx]? = some m ∧ MemShapeOK md m := by
  unfold stateOKB at h
  simp only [Bool.and_eq_true, List.all_eq_true] at h
  intro idx md hmd
  have hlt : idx < D.memories.size := mem_lt_of_getElem? hmd
  have hr : idx ∈ List.range D.memories.size := List.mem_range.mpr hlt
  have := h.2 idx hr
  rw [hmd] at this
  cases hm : st.mems[idx]? with
  | none   => rw [hm] at this; simp at this
  | some m =>
      rw [hm] at this
      exact ⟨m, rfl, memImgOKB_sound this⟩

theorem stateOKB_shape {D : DesignCert} {st : StateM} (h : stateOKB D st = true) :
    StateShapeOK D st := by
  intro idx md hmd
  obtain ⟨m, hm, hsh⟩ := stateOKB_shapeW h idx md hmd
  exact ⟨m, hm, hsh.toFullTable⟩

theorem stateOKB_flopSize {D : DesignCert} {st : StateM} (h : stateOKB D st = true) :
    D.flops.size ≤ st.flops.size := by
  unfold stateOKB at h
  simp only [Bool.and_eq_true, decide_eq_true_eq] at h
  exact h.1

theorem inputOKB_sound {D : DesignCert} {n : Nat} (h : inputOKB D n = true) :
    ∀ sd ∈ D.sources.toList, SourceInputBound sd n := by
  unfold inputOKB at h
  simp only [List.all_eq_true, decide_eq_true_eq] at h
  exact h

theorem flopBoundOKB_sound {D : DesignCert} {n : Nat} (h : flopBoundOKB D n = true) :
    ∀ sd ∈ D.sources.toList, SourceFlopBound sd n := by
  unfold flopBoundOKB at h
  simp only [List.all_eq_true, decide_eq_true_eq] at h
  exact h

theorem edgesOKB_flops {D : DesignCert} {e : ClockEdges} (h : edgesOKB D e = true) :
    ∀ f ∈ D.flops.toList, f.clock < e.size := by
  unfold edgesOKB at h
  simp only [Bool.and_eq_true, List.all_eq_true, decide_eq_true_eq] at h
  exact h.1

theorem edgesOKB_mems {D : DesignCert} {e : ClockEdges} (h : edgesOKB D e = true) :
    ∀ md ∈ D.memories.toList, md.clock < e.size := by
  unfold edgesOKB at h
  simp only [Bool.and_eq_true, List.all_eq_true, decide_eq_true_eq] at h
  exact h.2

/-! ## 4. Two Booleans give the whole resolved cycle

Nothing is assumed: `admitsB` is decided from the design and `runtimeOKB` from
the state, the input and the edge vector, and between them they discharge every
antecedent of `admits_mainM_resolved`. -/

theorem admits_run {D : DesignCert} {i : RuntimeInput} {st : StateM} {e : ClockEdges}
    (hA : admitsB D = true) (hR : runtimeOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
        (StateM.encResultM r)
      ∧ r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ StateShapeOK D r.nextState := by
  unfold runtimeOKB at hR
  simp only [Bool.and_eq_true] at hR
  obtain ⟨⟨⟨hst, hin⟩, hfb⟩, hed⟩ := hR
  exact admits_mainM_resolved hA (stateOKB_shape hst) (inputOKB_sound hin)
    (flopBoundOKB_sound hfb) (edgesOKB_flops hed) (edgesOKB_mems hed)
    (stateOKB_flopSize hst)

/-! ## 5. Executed: the policy accepts the fixtures and REFUSES the near misses

`mixedDesign` declares one `aw = 5`, `dw = 32` memory on clock 0 and reads
`input 0 … 3`; `twoMemDesign` declares two, on clocks 0 and 1. -/

#guard memImgOKB { aw := 5, dw := 32, nextImg := 7, clock := 0 } initImg == true
#guard stateOKB mixedDesign initState == true
#guard stateOKB mixedDesign zeroState == true
#guard stateOKB twoMemDesign twoMemState == true
#guard inputOKB mixedDesign 4 == true
#guard flopBoundOKB mixedDesign 0 == true
#guard edgesOKB mixedDesign #[true] == true
#guard edgesOKB twoMemDesign #[true, false] == true
#guard runtimeOKB mixedDesign #[mk_bv 5 20, mk_bv 32 0xFFFFFFFF, mk_bv 1 1, mk_bv 32 0xF]
         initState #[true] == true
#guard runtimeOKB twoMemDesign #[mk_bv 5 3, mk_bv 32 0xFFFFFFFF, mk_bv 1 1, mk_bv 32 0xF]
         twoMemState #[true, true] == true

/-- A table of the right LENGTH whose words are the wrong width: `FullTable`
cannot see this, `MemShapeOK` refuses it. -/
private def narrowImg : MemImg :=
  ⟨(List.range 32).map (fun k => mk_bv 7 (Int.ofNat k)), mk_bv 32 0⟩
/-- …and one whose words are right but whose DEFAULT is not. -/
private def badDfltImg : MemImg :=
  ⟨(List.range 32).map (fun k => mk_bv 32 (Int.ofNat k)), mk_bv 7 0⟩
/-- …and one that is simply too short. -/
private def shortImg : MemImg :=
  ⟨(List.range 16).map (fun k => mk_bv 32 (Int.ofNat k)), mk_bv 32 0⟩

#guard MemImg.FullTable 5 narrowImg       -- the OLD shape accepts it
#guard memImgOKB { aw := 5, dw := 32, nextImg := 7, clock := 0 } narrowImg == false
#guard memImgOKB { aw := 5, dw := 32, nextImg := 7, clock := 0 } badDfltImg == false
#guard memImgOKB { aw := 5, dw := 32, nextImg := 7, clock := 0 } shortImg == false
#guard stateOKB mixedDesign { flops := #[], mems := #[narrowImg] } == false
#guard stateOKB mixedDesign { flops := #[], mems := #[] } == false        -- image ABSENT
#guard stateOKB twoMemDesign { flops := #[], mems := #[imgA] } == false   -- only one of two
#guard inputOKB mixedDesign 3 == false                                   -- `input 3` reads past
#guard edgesOKB twoMemDesign #[true] == false                            -- clock 1 undeclared
#guard edgesOKB mixedDesign #[] == false
#guard runtimeOKB mixedDesign #[mk_bv 5 20, mk_bv 32 0, mk_bv 1 1] initState #[true] == false
#guard runtimeOKB twoMemDesign #[mk_bv 5 3, mk_bv 32 0, mk_bv 1 1, mk_bv 32 0]
         twoMemState #[true] == false


/-! ## 6. The word width of a memory slot, on the REFERENCE side

The object side of a memory is a `MemImg` — a word list and a default.  The
reference side is a FUNCTION `Int → BV`, which has neither.  So a word-level
shape statement about the witness list cannot be read off `PrefixRelM.den`
alone, and §63.42's `denote_does_not_determine_geometry` says the LENGTH really
is lost.  The WIDTHS, however, are not: a full table's every address is in
range, so each word is the denotation at its own index, and the default is the
denotation at any negative address.  That is what makes the next state's word
shape recoverable from facts about the reference function, with no change to
`PrefixRelM`, `sourcesThenNodes` or `mainM_adequate`. -/

/-- The data width a memory type carries; `none` for a scalar. -/
def tyDW : SlotTy → Option Nat
  | .bv _     => none
  | .tbl _ dw => some dw
  | .rom dw _ => some dw

/-- The word-level reading of `stateOKB`, as a Prop. -/
def StateWordOK (D : DesignCert) (st : StateM) : Prop :=
  ∀ (idx : Nat) (md : MemoryDesc), D.memories[idx]? = some md →
    ∃ m, st.mems[idx]? = some m ∧ MemShapeOK md m

theorem stateOKB_word {D : DesignCert} {st : StateM} (h : stateOKB D st = true) :
    StateWordOK D st := stateOKB_shapeW h

/-- Shape gives width, at EVERY address: in range it is a word, out of range it
is the default. -/
theorem memShape_denote_width {md : MemoryDesc} {m : MemImg}
    (h : MemShapeOK md m) (x : Int) : (m.denote x).width = md.dw := by
  unfold MemImg.denote MemImg.get
  by_cases hx : 0 ≤ x
  · simp only [hx, if_true]
    cases hw : m.words[x.toNat]? with
    | none   => simpa using h.2.2
    | some w =>
        simp only [Option.getD_some]
        obtain ⟨hlt, heq⟩ := List.getElem?_eq_some_iff.mp hw
        exact h.2.1 w (heq ▸ List.getElem_mem hlt)
  · simp only [hx, if_false]
    exact h.2.2

/-- …and the converse, which is the one that recovers the next state's shape:
the length from the table statement, the words from the denotation at their own
indices, the default from the denotation at `-1`. -/
theorem memShape_of_denote {md : MemoryDesc} {m : MemImg}
    (hfull : MemImg.FullTable md.aw m)
    (hw : ∀ x : Int, (m.denote x).width = md.dw) : MemShapeOK md m := by
  refine ⟨hfull, ?_, ?_⟩
  · intro w hwm
    obtain ⟨n, hn, heq⟩ := List.getElem_of_mem hwm
    have hx := hw (Int.ofNat n)
    have hg : m.get (Int.ofNat n) = m.words[n] := by
      unfold MemImg.get
      simp [List.getElem?_eq_getElem hn]
    rw [MemImg.denote, hg] at hx
    rw [← heq]; exact hx
  · have hx := hw (-1)
    rwa [MemImg.denote, MemImg.get_neg m (by decide)] at hx

/-- `checkSource` pins BOTH of a `memImg` source's declared widths to the
memory it names, not just the address width. -/
theorem memDesc_dw_of_sourceOK {D : DesignCert} {k idx aw dw : Nat}
    (hsem : Direct.DesignSemWF D)
    (hsd : D.sources[k]? = some (.memImg idx aw dw)) :
    ∃ md, D.memories[idx]? = some md ∧ md.aw = aw ∧ md.dw = dw := by
  have h := hsem.sourceOK k _ hsd
  rw [Direct.checkSource] at h
  by_cases hz : (dw == 0) = true
  · rw [if_pos hz] at h; simp at h
  · rw [if_neg hz] at h
    cases hm : D.memories[idx]? with
    | none    => rw [hm] at h; simp at h
    | some md =>
        rw [hm] at h
        have h' : (if (md.aw == aw && md.dw == dw) = true then (none : Option Direct.SimError)
                   else some (Direct.SimError.memDescMismatch k idx)) = none := h
        have hok : (md.aw == aw && md.dw == dw) = true := by
          by_cases hb : (md.aw == aw && md.dw == dw) = true
          · exact hb
          · rw [if_neg hb] at h'; simp at h' 
        simp only [Bool.and_eq_true, beq_iff_eq] at hok
        exact ⟨md, rfl, hok.1, hok.2⟩

/-- A byte-enabled write returns words of the NODE width where it writes and
the image's own words everywhere else, so a node width equal to the table's
data width preserves the width pointwise. -/
theorem cert_mem_write_be_width {w dw bw : Nat} {f : Int → BV} {a d be : BV}
    (hw : w = dw) (hf : ∀ x : Int, (f x).width = dw) :
    ∀ x : Int, ((cert_mem_write_be w f a d be bw) x).width = dw := by
  intro x
  unfold cert_mem_write_be
  by_cases hbe : bv_nonzero be
  · simp only [hbe, if_true]
    by_cases hx : x = bv_uint a
    · simp only [hx, if_true, cert_masked_update, mk_bv]
      exact hw
    · simp only [hx, if_false]; exact hf x
  · simp only [hbe, Bool.false_eq_true, if_false]; exact hf x

/-- The write case of `nodeTy`, with the NODE WIDTH as well as the geometry.
`nodeTy_write_geom` returns the address fit; this returns `c.width = dw`, which
is what makes the written table keep its word width. -/
theorem nodeTy_write_dw {ty : Nat → Option SlotTy} {c : DenseNodeCert}
    {bw d0 d1 d2 d3 : Nat} {t : SlotTy}
    (hop : c.op = .Op_MemWriteBE bw) (hdeps : c.deps.toList = [d0, d1, d2, d3])
    (h : nodeTy ty c = some t) :
    ∃ aw dw, t = .tbl aw dw ∧ ty d0 = some (.tbl aw dw) ∧ c.width = dw := by
  have hb : nodeAdmittedB c = true := by
    cases hbb : nodeAdmittedB c with
    | false => exfalso; unfold nodeTy at h; rw [hbb] at h; simp at h
    | true  => rfl
  unfold nodeTy at h
  rw [hb] at h
  simp only [Bool.not_true, Bool.false_eq_true, if_false] at h
  rw [hop, hdeps] at h
  simp only [List.map_cons, List.map_nil] at h
  split at h
  · split at h
    · rename_i aw' dw' heq
      split at h
      · rename_i hcond
        simp only [Bool.and_eq_true, decide_eq_true_eq, beq_iff_eq] at hcond
        simp only [Option.some.injEq] at h
        exact ⟨aw', dw', h.symm, heq, hcond.1.1.1.2⟩
      · simp at h
    · simp at h
  · simp at h

/-- Only a byte-enabled write can type as a TABLE: every other arm of `nodeTy`
returns `.bv` or nothing. -/
theorem nodeTy_tbl_op {ty : Nat → Option SlotTy} {c : DenseNodeCert} {aw dw : Nat}
    (h : nodeTy ty c = some (.tbl aw dw)) : ∃ bw, c.op = .Op_MemWriteBE bw := by
  unfold nodeTy at h
  split at h
  · simp at h
  · split at h
    · -- Op_MemRead: every result is a bit vector
      exfalso
      split at h
      · split at h
        · split at h
          · simp at h
          · simp at h
        · simp at h
        · simp at h
      · simp at h
    · -- Op_MemWriteBE
      exact ⟨_, by assumption⟩
    · -- every other operator
      exfalso
      split at h <;> simp at h

/-! ### The induction

Over the dense order, and — unlike §63.43's scalar lift — a genuine one: a
written table's values are the values of the image it wrote into, everywhere
the write did not land.  It is NOT circular: every statement here is about
`slotValM`, the reference side, so none of the fold lemmas that assume
`WriteAddrFits` is involved. -/

theorem admits_slot_memwidth {D : DesignCert} (h : admitsB D = true)
    {st : StateM} (hW : StateWordOK D st) (i : RuntimeInput) :
    ∀ (k : Nat), k < D.numSlots → ∀ (t : SlotTy) (dw : Nat),
      tyAt D k = some t → tyDW t = some dw →
      ∀ x : Int, ((slotValM D i st k).asMem x).width = dw := by
  obtain ⟨ts, hts⟩ := admitsB_infer h
  intro k
  induction k using Nat.strongRecOn with
  | _ k ih =>
    intro hk t dw hty hdw x
    by_cases hs : k < D.sources.size
    · -- a SOURCE
      rw [tyAt_source hs hts] at hty
      rw [slotValM_source hs]
      have hsd : D.sources[k]? = some (D.sources[k]'hs) := by
        rw [Array.getElem?_eq_getElem hs]
      have hty' : srcTy (D.sources[k]'hs) = t := Option.some.inj hty
      cases hsrc : D.sources[k]'hs with
      | input _ _ => rw [hsrc] at hty'; subst hty'; simp [srcTy, tyDW] at hdw
      | const _ _ => rw [hsrc] at hty'; subst hty'; simp [srcTy, tyDW] at hdw
      | flopQ _ _ => rw [hsrc] at hty'; subst hty'; simp [srcTy, tyDW] at hdw
      | flopQAsync _ _ _ _ _ => rw [hsrc] at hty'; subst hty'; simp [srcTy, tyDW] at hdw
      | memImg idx aw dw' =>
          rw [hsrc] at hty'
          have hdw' : dw' = dw := by
            rw [← hty'] at hdw; simpa [srcTy, tyDW] using hdw
          subst hdw'
          rw [hsrc] at hsd
          obtain ⟨md, hmd, -, hmddw⟩ := memDesc_dw_of_sourceOK (admits_semwf h) hsd
          obtain ⟨m, hm, hshape⟩ := hW idx md hmd
          have : (StateM.denote st).mems[idx]? = some m.denote := by
            simp [StateM.denote, Array.getElem?_map, hm]
          simp only [Compiler.sourceValue, this, Option.getD_some, CertVal.asMem]
          rw [← hmddw]; exact memShape_denote_width hshape x
      | memConst aw dw' ct =>
          rw [hsrc] at hty'
          have hdw' : dw' = dw := by
            rw [← hty'] at hdw; simpa [srcTy, tyDW] using hdw
          subst hdw'
          simp only [Compiler.sourceValue, CertVal.asMem]
          split <;> simp [mk_bv]
    · -- a NODE
      have hj : k - D.sources.size < D.nodes.size := by
        unfold DesignCert.numSlots at hk; omega
      have hk' : k = D.sources.size + (k - D.sources.size) := by omega
      obtain ⟨c, hc⟩ : ∃ c, D.nodes[k - D.sources.size]? = some c :=
        ⟨D.nodes[k - D.sources.size], by rw [Array.getElem?_eq_getElem hj]⟩
      have hlt : D.sources.size + (k - D.sources.size) < ts.size := by
        rw [admitsB_size h hts]; unfold DesignCert.numSlots; omega
      have hnode := admits_node_ty hts (admits_wf h) hc
      rw [Array.getElem?_eq_getElem hlt] at hnode
      have htyk : tyAt D (D.sources.size + (k - D.sources.size)) = some t := by
        rw [← hk']; exact hty
      unfold tyAt at htyk
      rw [hts] at htyk
      simp only [Option.bind_some] at htyk
      rw [Array.getElem?_eq_getElem hlt] at htyk
      have hres : nodeTy (fun d => ts[d]?) c = some t := by rw [hnode]; exact htyk
      -- the result type is a table, so the operator is a byte-enabled write
      obtain ⟨aw0, dw0, hteq⟩ : ∃ aw0 dw0, t = .tbl aw0 dw0 := by
        rcases nodeTy_result hres with hbv | hx
        · exfalso; rw [hbv] at hdw; simp [tyDW] at hdw
        · exact hx
      subst hteq
      have hdw0 : dw0 = dw := by simpa [tyDW] using hdw
      obtain ⟨bw, hop⟩ := nodeTy_tbl_op hres
      obtain ⟨d0, d1, d2, d3, hdeps⟩ := memWriteBE_deps (admits_semwf h) hc hop
      obtain ⟨aw1, dw1, htbl, hd0ty, hwid⟩ := nodeTy_write_dw hop hdeps hres
      have hx : aw0 = aw1 ∧ dw0 = dw1 := by simpa [SlotTy.tbl.injEq] using htbl
      have hwid' : c.width = dw := by rw [hwid, ← hx.2]; exact hdw0
      -- the memory operand is an EARLIER slot, so the hypothesis applies
      have hd0lt : d0 < D.sources.size + (k - D.sources.size) :=
        (admits_wf h).depsBounded _ _ hc d0 (by rw [hdeps]; simp)
      have hd0k : d0 < k := by omega
      have hd0num : d0 < D.numSlots := by unfold DesignCert.numSlots; omega
      have hd0tyAt : tyAt D d0 = some (.tbl aw1 dw1) := by
        unfold tyAt; rw [hts]; simpa using hd0ty
      have hIH := ih d0 hd0k hd0num (.tbl aw1 dw1) dw1 hd0tyAt (by simp [tyDW])
      have hIH' : ∀ y : Int, ((slotValM D i st d0).asMem y).width = dw := by
        intro y; rw [← hdw0, hx.2]; exact hIH y
      -- and the node value is the write applied to it
      rw [hk', ← nodeValM_rec (i := i) (st := st) (admits_wf h) hj hc, hop]
      simp only [hdeps, List.map_cons, List.map_nil, eval_op_cert, CertVal.asMem]
      exact cert_mem_write_be_width hwid' hIH' x

#print axioms memShape_of_denote
#print axioms admits_slot_memwidth


/-! ## 7. The next state passes the SAME policy

`admits_run` concludes the length-level `StateShapeOK`.  With §6 the word level
follows too, so a step's output is a legal input to the next step — which is
what makes the policy a loop invariant rather than a one-off precondition. -/

theorem memImgOKB_complete {md : MemoryDesc} {m : MemImg}
    (h : MemShapeOK md m) : memImgOKB md m = true := by
  unfold memImgOKB
  simp only [Bool.and_eq_true, beq_iff_eq, List.all_eq_true]
  exact ⟨⟨h.1, fun w hw => by simpa using h.2.1 w hw⟩, h.2.2⟩

theorem stateOKB_complete {D : DesignCert} {st : StateM}
    (hfl : D.flops.size ≤ st.flops.size) (hW : StateWordOK D st) :
    stateOKB D st = true := by
  unfold stateOKB
  simp only [Bool.and_eq_true, decide_eq_true_eq, List.all_eq_true]
  refine ⟨hfl, ?_⟩
  intro idx hidx
  have hlt : idx < D.memories.size := List.mem_range.mp hidx
  obtain ⟨m, hm, hsh⟩ := hW idx _ (by rw [Array.getElem?_eq_getElem hlt])
  rw [Array.getElem?_eq_getElem hlt, hm]
  exact memImgOKB_complete hsh

/-- The checker pins the next-image slot's FULL type — both widths — to the
memory it commits. -/
theorem admits_nextImgTy {D : DesignCert} (h : admitsB D = true)
    {idx : Nat} {md : MemoryDesc} (hmd : D.memories[idx]? = some md) :
    tyAt D md.nextImg = some (.tbl md.aw md.dw) ∧ md.nextImg < D.numSlots := by
  obtain ⟨ts, hts⟩ := admitsB_infer h
  have hmem : md ∈ D.memories.toList := by
    obtain ⟨h1, h2⟩ := List.getElem?_eq_some_iff.mp
      (show D.memories.toList[idx]? = some md by rw [← hmd]; simp)
    exact h2 ▸ List.getElem_mem h1
  have hsz := admitsB_size h hts
  unfold admitsB at h
  rw [hts] at h
  simp only [Bool.and_eq_true] at h
  have hthis := List.all_eq_true.mp h.2.2 md hmem
  cases hty : ts[md.nextImg]? with
  | none   => rw [hty] at hthis; simp at hthis
  | some t =>
      cases t with
      | bv w     => rw [hty] at hthis; simp at hthis
      | rom dw n => rw [hty] at hthis; simp at hthis
      | tbl aw dw =>
          rw [hty] at hthis
          simp only [Bool.and_eq_true, beq_iff_eq] at hthis
          constructor
          · unfold tyAt; rw [hts]
            simp only [Option.bind_some, hty, hthis.1, hthis.2]
          · rw [← hsz]; exact mem_lt_of_getElem? hty

/-- **The policy is preserved.**  Both branches of the per-memory commit give an
image of the declared shape: the written table from §6's induction, the quiet
one from the incoming state. -/
theorem admits_run_pres {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} (hA : admitsB D = true) (hR : runtimeOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
        (StateM.encResultM r)
      ∧ r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateOKB D r.nextState = true := by
  have hst : stateOKB D st = true := by
    unfold runtimeOKB at hR
    simp only [Bool.and_eq_true] at hR
    exact hR.1.1.1
  obtain ⟨r, hev, hden, hsh⟩ := admits_run hA hR
  refine ⟨r, hev, hden, ?_⟩
  have hfl : r.nextState.flops.size = D.flops.size := by
    have hx := congrArg (fun z => z.nextState.flops.size) hden
    simpa [StateM.ResultM.denote, StateM.denote, Compiler.interpretDesign] using hx
  refine stateOKB_complete (by omega) ?_
  intro idx md hmd
  obtain ⟨m, hm, hfull⟩ := hsh idx md hmd
  refine ⟨m, hm, memShape_of_denote hfull ?_⟩
  intro x
  have hidx : idx < D.memories.size := mem_lt_of_getElem? hmd
  -- the image's denotation IS the reference's next memory function
  have hlhs : (StateM.denote r.nextState).mems[idx]? = some m.denote := by
    simp [StateM.denote, Array.getElem?_map, hm]
  have hrhs : (Compiler.interpretDesign D e i (StateM.denote st)).nextState.mems[idx]?
      = some (Compiler.srcMemNext (slotValM D i st) e (StateM.denote st) idx md) := by
    have hmd' : D.memories[idx] = md := by
      rw [Array.getElem?_eq_getElem hidx] at hmd; exact Option.some.inj hmd
    unfold Compiler.interpretDesign
    simp [hidx, hmd']
    all_goals (first | rfl | (funext k; rfl))
  have hx := congrArg (fun z => z.nextState.mems[idx]?) hden
  simp only [StateM.ResultM.denote] at hx
  rw [hlhs, hrhs] at hx
  have hmden : m.denote
      = Compiler.srcMemNext (slotValM D i st) e (StateM.denote st) idx md :=
    Option.some.inj hx
  rw [hmden]
  unfold Compiler.srcMemNext
  by_cases hf : fires e md.clock
  · simp only [hf, if_true]
    obtain ⟨hty, hnum⟩ := admits_nextImgTy hA hmd
    exact admits_slot_memwidth hA (stateOKB_word hst) i md.nextImg hnum
      (.tbl md.aw md.dw) md.dw hty (by simp [tyDW]) x
  · simp only [hf, Bool.false_eq_true, if_false]
    obtain ⟨m0, hm0, hsh0⟩ := stateOKB_word hst idx md hmd
    have hq : (StateM.denote st).mems[idx]? = some m0.denote := by
      simp [StateM.denote, Array.getElem?_map, hm0]
    rw [hq, Option.getD_some]
    exact memShape_denote_width hsh0 x

/-- The flop-vector bound is not an independent fact: `checkSource` already
pins every `flopQ`/`flopQAsync` source to a DECLARED flop, so a state with at
least as many flops as the design declares is long enough for all of them. -/
theorem flopBoundOKB_of_flopSize {D : DesignCert} {n : Nat}
    (hsem : Direct.DesignSemWF D) (hfl : D.flops.size ≤ n) :
    flopBoundOKB D n = true := by
  unfold flopBoundOKB
  simp only [List.all_eq_true, decide_eq_true_eq]
  intro sd hsd
  obtain ⟨j, hj, hje⟩ := List.getElem_of_mem hsd
  have hsj : D.sources[j]? = some sd := by
    rw [← hje]; simp [Array.getElem?_eq_getElem, (by simpa using hj : j < D.sources.size)]
  have hck := hsem.sourceOK j sd hsj
  have hbound : ∀ (idx w : Nat), sd = .flopQ idx w ∨
      (∃ ri rv al, sd = .flopQAsync idx w ri rv al) → idx < n := by
    intro idx w hcase
    have hlt : idx < D.flops.size := by
      rcases hcase with rfl | ⟨ri, rv, al, rfl⟩ <;>
      · rw [Direct.checkSource] at hck
        split at hck
        · simp at hck
        · cases hf : D.flops[idx]? with
          | none   => rw [hf] at hck; simp at hck
          | some f => exact mem_lt_of_getElem? hf
    omega
  cases sd with
  | input _ _ => exact trivial
  | const _ _ => exact trivial
  | flopQ idx w => exact hbound idx w (Or.inl rfl)
  | flopQAsync idx w ri rv al => exact hbound idx w (Or.inr ⟨ri, rv, al, rfl⟩)
  | memImg _ _ _ => exact trivial
  | memConst _ _ _ => exact trivial

/-- **Two steps at the strengthened shape**, the second on the state the first
returned — composable precisely because the policy is preserved. -/
theorem admits_run_two {D : DesignCert} {i₁ i₂ : RuntimeInput} {st : StateM}
    {e₁ e₂ : ClockEdges} (hA : admitsB D = true)
    (hR : runtimeOKB D i₁ st e₁ = true) :
    ∃ r₁ : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e₁, encInput i₁, StateM.encStateM st].map Term.lit))
        (StateM.encResultM r₁)
      ∧ r₁.denote = Compiler.interpretDesign D e₁ i₁ (StateM.denote st)
      ∧ (inputOKB D i₂.size = true → edgesOKB D e₂ = true →
          ∃ r₂ : StateM.ResultM,
            Eval hwMemP []
              (.call hwMemP.entry
                ([encDesign D, encEdges e₂, encInput i₂,
                  StateM.encStateM r₁.nextState].map Term.lit))
              (StateM.encResultM r₂)
            ∧ r₂.denote
                = Compiler.interpretDesign D e₂ i₂ (StateM.denote r₁.nextState)
            ∧ stateOKB D r₂.nextState = true) := by
  obtain ⟨r₁, hev, hden, hok⟩ := admits_run_pres hA hR
  refine ⟨r₁, hev, hden, ?_⟩
  intro hin hed
  have hfb : flopBoundOKB D r₁.nextState.flops.size = true :=
    flopBoundOKB_of_flopSize (admits_semwf hA) (stateOKB_flopSize hok)
  have hR₂ : runtimeOKB D i₂ r₁.nextState e₂ = true := by
    unfold runtimeOKB
    simp only [Bool.and_eq_true]
    exact ⟨⟨⟨hok, hin⟩, hfb⟩, hed⟩
  exact admits_run_pres hA hR₂

#print axioms admits_run_pres
#print axioms admits_run_two


/-! ## 8. Initialisation: every admitted design HAS a legal state

Checking a state is only half of a policy.  If no state passed, `admits_run`
would be vacuous.  `initStateOf` builds one from the design alone — each flop at
its declared reset value, each memory a zero table of its declared geometry —
and it passes. -/

def initFlopsOf (D : DesignCert) : Array BV :=
  D.flops.map (fun f => mk_bv f.width f.resetValue)

def initMemsOf (D : DesignCert) : Array MemImg :=
  D.memories.map (fun md => MemImg.zeroImg md.aw md.dw)

def initStateOf (D : DesignCert) : StateM :=
  { flops := initFlopsOf D, mems := initMemsOf D }

@[simp] theorem initStateOf_flops_size (D : DesignCert) :
    (initStateOf D).flops.size = D.flops.size := by simp [initStateOf, initFlopsOf]

@[simp] theorem initStateOf_mems_size (D : DesignCert) :
    (initStateOf D).mems.size = D.memories.size := by simp [initStateOf, initMemsOf]

theorem zeroImg_shape (md : MemoryDesc) :
    MemShapeOK md (MemImg.zeroImg md.aw md.dw) := by
  refine ⟨by simp [MemImg.zeroImg], ?_, by simp [MemImg.zeroImg, mk_bv]⟩
  intro w hw
  have hmem : w ∈ List.replicate (2 ^ md.aw) (mk_bv md.dw 0) := by
    simpa [MemImg.zeroImg] using hw
  rw [List.eq_of_mem_replicate hmem]; simp [mk_bv]

theorem initStateOf_word (D : DesignCert) : StateWordOK D (initStateOf D) := by
  intro idx md hmd
  have hlt : idx < D.memories.size := mem_lt_of_getElem? hmd
  have hmd' : D.memories[idx] = md := by
    rw [Array.getElem?_eq_getElem hlt] at hmd; exact Option.some.inj hmd
  refine ⟨MemImg.zeroImg md.aw md.dw, ?_, zeroImg_shape md⟩
  simp [initStateOf, initMemsOf, hlt, hmd']

/-- **The canonical state passes.** -/
theorem initStateOf_ok (D : DesignCert) : stateOKB D (initStateOf D) = true :=
  stateOKB_complete (by simp) (initStateOf_word D)

/-- …and it is `RuntimeWF` for the ORIGINAL reference, exactly — not merely
long enough.  `stateOKB` deliberately allows a longer vector, because the
interpreter reads by index; `RuntimeWF` wants the sizes on the nose, and the
canonical state has them. -/
theorem initStateOf_runtimeWF (D : DesignCert) (i : RuntimeInput) :
    Compiler.RuntimeWF D i (StateM.denote (initStateOf D)) where
  flopsSized := by simp [StateM.denote]
  memsSized  := by simp [StateM.denote]

/-- Any state the policy accepts whose vectors are exactly the declared sizes is
`RuntimeWF` too. -/
theorem runtimeWF_of_sizes {D : DesignCert} {st : StateM} {i : RuntimeInput}
    (hf : st.flops.size = D.flops.size) (hm : st.mems.size = D.memories.size) :
    Compiler.RuntimeWF D i (StateM.denote st) where
  flopsSized := by simpa [StateM.denote] using hf
  memsSized  := by simpa [StateM.denote] using hm

/-- **Every admitted design can actually be run**, from a state built for it:
the state premise is discharged by construction and the flop bound follows from
`checkSource`, so only the input and the edge vector remain. -/
theorem admits_run_init {D : DesignCert} {i : RuntimeInput} {e : ClockEdges}
    (hA : admitsB D = true) (hin : inputOKB D i.size = true)
    (hed : edgesOKB D e = true) :
    ∃ r : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e, encInput i,
            StateM.encStateM (initStateOf D)].map Term.lit))
        (StateM.encResultM r)
      ∧ r.denote
          = Compiler.interpretDesign D e i (StateM.denote (initStateOf D))
      ∧ stateOKB D r.nextState = true := by
  refine admits_run_pres hA ?_
  unfold runtimeOKB
  simp only [Bool.and_eq_true]
  refine ⟨⟨⟨initStateOf_ok D, hin⟩, ?_⟩, hed⟩
  exact flopBoundOKB_of_flopSize (admits_semwf hA) (by simp)

/-! ### …and the shape survives the codec

`StateM.decStateM_encStateM` is the round trip.  What matters for the policy is that
the state that comes BACK out of the encoder is the one that went in, so it
passes the same check and denotes the same `RuntimeState`: the interpreter's
literal argument carries the shape, it is not re-established on the far side. -/

theorem stateOKB_codec {D : DesignCert} {st : StateM} (h : stateOKB D st = true) :
    ∃ st', StateM.decStateM (StateM.encStateM st) = some st'
         ∧ stateOKB D st' = true
         ∧ StateM.denote st' = StateM.denote st :=
  ⟨st, StateM.decStateM_encStateM st, h, rfl⟩

theorem initStateOf_codec (D : DesignCert) :
    ∃ st, StateM.decStateM (StateM.encStateM (initStateOf D)) = some st
        ∧ stateOKB D st = true
        ∧ StateM.denote st = StateM.denote (initStateOf D) :=
  stateOKB_codec (initStateOf_ok D)

/-- The `RuntimeState` relation the interpreter's correctness statements use,
at the canonical state. -/
theorem StateRelR_initStateOf (D : DesignCert) :
    StateM.StateRelR (StateM.encStateM (initStateOf D))
      (StateM.denote (initStateOf D)) :=
  StateM.StateRelR_encStateM _

/-! ### Executed -/

#guard stateOKB mixedDesign (initStateOf mixedDesign) == true
#guard stateOKB twoMemDesign (initStateOf twoMemDesign) == true
#guard (initStateOf twoMemDesign).mems.size == 2
#guard (initStateOf mixedDesign).mems.size == 1
#guard ((initStateOf mixedDesign).mems[0]!).words.length == 32
#guard runtimeOKB mixedDesign #[mk_bv 5 20, mk_bv 32 0xFFFFFFFF, mk_bv 1 1, mk_bv 32 0xF]
         (initStateOf mixedDesign) #[true] == true
#guard runtimeOKB twoMemDesign #[mk_bv 5 3, mk_bv 32 0xFFFFFFFF, mk_bv 1 1, mk_bv 32 0xF]
         (initStateOf twoMemDesign) #[true, true] == true

#print axioms initStateOf_ok
#print axioms admits_run_init
#print axioms initStateOf_codec


/-! ## 9. The DECLARED FLOP WIDTH, which the size check does not see

`stateOKB` pins the flop vector's LENGTH and the memories' word widths; it says
nothing about the width of the values IN the flop vector, so on its own it
accepts a 7-bit value against a 32-bit flop descriptor.  That is a gap in what
the policy states, not a contradiction of anything proved above: the adequacy
equation holds either way.  This section states the missing fact and proves it
preserved, leaving every existing predicate and theorem exactly as it was.

### Why the RAW INPUT widths are a different matter, and stay unchecked

Every source read RESIZES to the width its descriptor declares:

* `sourceValue (.input idx w)` is `bv_resize w (i[idx]?.getD (mk_bv w 0))`;
* `sourceValue (.flopQ idx w)` is `bv_resize w (s.flops[idx]?.getD (mk_bv w 0))`;
* both branches of `.flopQAsync` resize too — the reset value is `mk_bv w rv`;
* and `srcFlopNext`'s CAPTURE branch is `bv_resize f.width (rho f.din).asBV`.

So a raw input vector whose entries have the "wrong" widths cannot be observed
at all.  `inputOKB` therefore requires `idx < i.size` and NOTHING about
`i[idx].width`, and it stays that way: asserting declared widths on the raw
vector would be a demand the semantics never makes, and it would reject inputs
the reference accepts.

The STORED FLOP width is not like that, and that asymmetry is the whole point:
`srcFlopNext`'s HOLD branch — a quiet clock, or an edge with the enable low —
is `s.flops[idx]?.getD (mk_bv f.width 0)`, which copies the stored value into
the next state VERBATIM, with no resize.  A narrow value therefore survives the
step, and its encoding, so the policy has to pin it. -/

def FlopShapeOK (D : DesignCert) (st : StateM) : Prop :=
  ∀ (idx : Nat) (f : FlopDesc), D.flops[idx]? = some f →
    ∃ b, st.flops[idx]? = some b ∧ b.width = f.width

def flopShapeOKB (D : DesignCert) (st : StateM) : Bool :=
  (List.range D.flops.size).all (fun idx =>
    match D.flops[idx]?, st.flops[idx]? with
    | some f, some b => b.width == f.width
    | _,      _      => false)

theorem flopShapeOKB_sound {D : DesignCert} {st : StateM}
    (h : flopShapeOKB D st = true) : FlopShapeOK D st := by
  unfold flopShapeOKB at h
  simp only [List.all_eq_true] at h
  intro idx f hf
  have hlt : idx < D.flops.size := mem_lt_of_getElem? hf
  have := h idx (List.mem_range.mpr hlt)
  rw [hf] at this
  cases hb : st.flops[idx]? with
  | none   => rw [hb] at this; simp at this
  | some b => rw [hb] at this; exact ⟨b, rfl, by simpa using this⟩

theorem flopShapeOKB_complete {D : DesignCert} {st : StateM}
    (h : FlopShapeOK D st) : flopShapeOKB D st = true := by
  unfold flopShapeOKB
  simp only [List.all_eq_true]
  intro idx hidx
  have hlt : idx < D.flops.size := List.mem_range.mp hidx
  obtain ⟨b, hb, hw⟩ := h idx _ (by rw [Array.getElem?_eq_getElem hlt])
  rw [Array.getElem?_eq_getElem hlt, hb]
  simpa using hw

/-- The refinement.  `stateOKB` and `runtimeOKB` are untouched; these are their
conjunction with the flop-width fact, so every theorem stated about the old
predicates keeps its exact meaning. -/
def stateFullOKB (D : DesignCert) (st : StateM) : Bool :=
  stateOKB D st && flopShapeOKB D st

def runtimeFullOKB (D : DesignCert) (i : RuntimeInput) (st : StateM)
    (e : ClockEdges) : Bool :=
  runtimeOKB D i st e && flopShapeOKB D st

/-! ### The width holds on every branch of the commit rule

Stated branch by branch against the ACTUAL reference rule — each branch's
VALUE first, then its width — so reset (synchronous and asynchronous),
capture-with-enable and hold-on-a-quiet-clock are each discharged on their own
terms rather than by one opaque case split. -/

/-- The reset pin's asserted value, as `srcFlopNext` computes it. -/
def resetAsserted (rho : Nat → CertVal) (f : FlopDesc) : Bool :=
  match f.resetPin with
  | none   => false
  | some r => xor f.resetActiveLow (bv_nonzero (rho r).asBV)

/-- The enable's value, as `srcFlopNext` computes it. -/
def flopEnabled (rho : Nat → CertVal) (f : FlopDesc) : Bool :=
  match f.enable with
  | none    => true
  | some en => bv_nonzero (rho en).asBV

/-- The rule, with its two guards named.  `rfl`: the `let`s are definitional. -/
theorem srcFlopNext_unfold (rho : Nat → CertVal) (e : ClockEdges)
    (s : RuntimeState) (idx : Nat) (f : FlopDesc) :
    Compiler.srcFlopNext rho e s idx f
      = (if (resetAsserted rho f && (fires e f.clock || f.asyncReset)) = true then
           mk_bv f.width f.resetValue
         else if (fires e f.clock && flopEnabled rho f) = true then
           bv_resize f.width (rho f.din).asBV
         else s.flops[idx]?.getD (mk_bv f.width 0)) := rfl

/-- RESET: asserted, and either this flop's clock fires or the reset is
ASYNCHRONOUS — the one case where a quiet clock still changes the flop. -/
theorem srcFlopNext_reset {rho : Nat → CertVal} {e : ClockEdges}
    {s : RuntimeState} {idx : Nat} {f : FlopDesc}
    (hr : resetAsserted rho f = true)
    (he : (fires e f.clock || f.asyncReset) = true) :
    Compiler.srcFlopNext rho e s idx f = mk_bv f.width f.resetValue := by
  rw [srcFlopNext_unfold, if_pos (by rw [hr, he]; rfl)]

/-- CAPTURE: no reset, the clock fires, the enable is high.  The value is
`bv_resize`d to the declared width, so the `din` slot's own width is
irrelevant. -/
theorem srcFlopNext_capture {rho : Nat → CertVal} {e : ClockEdges}
    {s : RuntimeState} {idx : Nat} {f : FlopDesc}
    (hr : resetAsserted rho f = false) (hed : fires e f.clock = true)
    (hen : flopEnabled rho f = true) :
    Compiler.srcFlopNext rho e s idx f = bv_resize f.width (rho f.din).asBV := by
  rw [srcFlopNext_unfold, if_neg (by rw [hr]; simp), if_pos (by rw [hed, hen]; rfl)]

/-- HOLD: no reset, and either a quiet clock or a low enable.  The stored value
is copied VERBATIM — no resize — which is why the policy must pin its width. -/
theorem srcFlopNext_hold {rho : Nat → CertVal} {e : ClockEdges}
    {s : RuntimeState} {idx : Nat} {f : FlopDesc}
    (hr : resetAsserted rho f = false)
    (hq : fires e f.clock = false ∨ flopEnabled rho f = false) :
    Compiler.srcFlopNext rho e s idx f = s.flops[idx]?.getD (mk_bv f.width 0) := by
  have hcond : (fires e f.clock && flopEnabled rho f) = false := by
    rcases hq with h | h
    · rw [h]; rfl
    · rw [h]; simp
  rw [srcFlopNext_unfold, if_neg (by rw [hr]; simp), if_neg (by rw [hcond]; simp)]

theorem srcFlopNext_width_reset {rho : Nat → CertVal} {e : ClockEdges}
    {s : RuntimeState} {idx : Nat} {f : FlopDesc}
    (hr : resetAsserted rho f = true)
    (he : (fires e f.clock || f.asyncReset) = true) :
    (Compiler.srcFlopNext rho e s idx f).width = f.width := by
  rw [srcFlopNext_reset hr he]; rfl

theorem srcFlopNext_width_capture {rho : Nat → CertVal} {e : ClockEdges}
    {s : RuntimeState} {idx : Nat} {f : FlopDesc}
    (hr : resetAsserted rho f = false) (hed : fires e f.clock = true)
    (hen : flopEnabled rho f = true) :
    (Compiler.srcFlopNext rho e s idx f).width = f.width := by
  rw [srcFlopNext_capture hr hed hen]; rfl

theorem srcFlopNext_width_hold {rho : Nat → CertVal} {e : ClockEdges}
    {s : RuntimeState} {idx : Nat} {f : FlopDesc}
    (hr : resetAsserted rho f = false)
    (hq : fires e f.clock = false ∨ flopEnabled rho f = false)
    (hs : ∀ b, s.flops[idx]? = some b → b.width = f.width) :
    (Compiler.srcFlopNext rho e s idx f).width = f.width := by
  rw [srcFlopNext_hold hr hq]
  cases hb : s.flops[idx]? with
  | none   => simp [mk_bv]
  | some b => simpa using hs b hb

/-- The three branches together: the only premise any of them needs is the
policy's own, and only the HOLD branch needs it. -/
theorem srcFlopNext_width {rho : Nat → CertVal} {e : ClockEdges}
    {s : RuntimeState} {idx : Nat} {f : FlopDesc}
    (hs : ∀ b, s.flops[idx]? = some b → b.width = f.width) :
    (Compiler.srcFlopNext rho e s idx f).width = f.width := by
  rw [srcFlopNext_unfold]
  split
  · rfl
  · split
    · rfl
    · cases hb : s.flops[idx]? with
      | none   => simp [mk_bv]
      | some b => simpa using hs b hb

/-! ### Preservation, and the strengthened route

`StateM.denote` carries the flop array VERBATIM, so the returned state's flop
widths are read straight off the adequacy equation — the same move §7 made for
the memories, and for the same reason. -/

theorem admits_run_full {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {e : ClockEdges} (hA : admitsB D = true)
    (hR : runtimeFullOKB D i st e = true) :
    ∃ r : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e, encInput i, StateM.encStateM st].map Term.lit))
        (StateM.encResultM r)
      ∧ r.denote = Compiler.interpretDesign D e i (StateM.denote st)
      ∧ stateFullOKB D r.nextState = true := by
  unfold runtimeFullOKB at hR
  simp only [Bool.and_eq_true] at hR
  obtain ⟨hR0, hflop⟩ := hR
  obtain ⟨r, hev, hden, hok⟩ := admits_run_pres hA hR0
  refine ⟨r, hev, hden, ?_⟩
  unfold stateFullOKB
  simp only [Bool.and_eq_true]
  refine ⟨hok, flopShapeOKB_complete ?_⟩
  intro idx f hf
  have hlt : idx < D.flops.size := mem_lt_of_getElem? hf
  have hf' : D.flops[idx] = f := by
    rw [Array.getElem?_eq_getElem hlt] at hf; exact Option.some.inj hf
  -- the returned flop array IS the reference's, verbatim
  have hx := congrArg (fun z => z.nextState.flops) hden
  simp only [StateM.ResultM.denote] at hx
  have hfl : r.nextState.flops
      = D.flops.mapIdx (fun idx f =>
          Compiler.srcFlopNext (slotValM D i st) e (StateM.denote st) idx f) := by
    have : (StateM.denote r.nextState).flops = r.nextState.flops := rfl
    rw [← this, hx]
    unfold Compiler.interpretDesign
    simp
    all_goals (first | rfl | (funext k; rfl))
  refine ⟨Compiler.srcFlopNext (slotValM D i st) e (StateM.denote st) idx f, ?_, ?_⟩
  · rw [hfl, Array.getElem?_eq_getElem (by simpa using hlt), Array.getElem_mapIdx, hf']
  · refine srcFlopNext_width ?_
    intro b hb
    obtain ⟨b', hb', hw⟩ := flopShapeOKB_sound hflop idx f hf
    have : b = b' := by
      have hd : (StateM.denote st).flops = st.flops := rfl
      rw [hd, hb'] at hb; exact (Option.some.inj hb).symm
    rw [this]; exact hw

/-- The canonical state carries the declared flop widths too: its values are
`mk_bv f.width f.resetValue`. -/
theorem initStateOf_flopShape (D : DesignCert) : FlopShapeOK D (initStateOf D) := by
  intro idx f hf
  have hlt : idx < D.flops.size := mem_lt_of_getElem? hf
  have hf' : D.flops[idx] = f := by
    rw [Array.getElem?_eq_getElem hlt] at hf; exact Option.some.inj hf
  refine ⟨mk_bv f.width f.resetValue, ?_, rfl⟩
  simp [initStateOf, initFlopsOf, hlt, hf']

theorem initStateOf_full_ok (D : DesignCert) :
    stateFullOKB D (initStateOf D) = true := by
  unfold stateFullOKB
  simp only [Bool.and_eq_true]
  exact ⟨initStateOf_ok D, flopShapeOKB_complete (initStateOf_flopShape D)⟩

theorem admits_run_full_init {D : DesignCert} {i : RuntimeInput} {e : ClockEdges}
    (hA : admitsB D = true) (hin : inputOKB D i.size = true)
    (hed : edgesOKB D e = true) :
    ∃ r : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e, encInput i,
            StateM.encStateM (initStateOf D)].map Term.lit))
        (StateM.encResultM r)
      ∧ r.denote
          = Compiler.interpretDesign D e i (StateM.denote (initStateOf D))
      ∧ stateFullOKB D r.nextState = true := by
  refine admits_run_full hA ?_
  unfold runtimeFullOKB runtimeOKB
  simp only [Bool.and_eq_true]
  refine ⟨⟨⟨⟨initStateOf_ok D, hin⟩, ?_⟩, hed⟩, ?_⟩
  · exact flopBoundOKB_of_flopSize (admits_semwf hA) (by simp)
  · exact flopShapeOKB_complete (initStateOf_flopShape D)

/-- **Two steps at the strengthened policy**, the second on the state the first
returned — the flop widths are preserved, so nothing about the state has to be
re-checked. -/
theorem admits_run_full_two {D : DesignCert} {i₁ i₂ : RuntimeInput} {st : StateM}
    {e₁ e₂ : ClockEdges} (hA : admitsB D = true)
    (hR : runtimeFullOKB D i₁ st e₁ = true) :
    ∃ r₁ : StateM.ResultM,
      Eval hwMemP []
        (.call hwMemP.entry
          ([encDesign D, encEdges e₁, encInput i₁, StateM.encStateM st].map Term.lit))
        (StateM.encResultM r₁)
      ∧ r₁.denote = Compiler.interpretDesign D e₁ i₁ (StateM.denote st)
      ∧ (inputOKB D i₂.size = true → edgesOKB D e₂ = true →
          ∃ r₂ : StateM.ResultM,
            Eval hwMemP []
              (.call hwMemP.entry
                ([encDesign D, encEdges e₂, encInput i₂,
                  StateM.encStateM r₁.nextState].map Term.lit))
              (StateM.encResultM r₂)
            ∧ r₂.denote
                = Compiler.interpretDesign D e₂ i₂ (StateM.denote r₁.nextState)
            ∧ stateFullOKB D r₂.nextState = true) := by
  obtain ⟨r₁, hev, hden, hok⟩ := admits_run_full hA hR
  refine ⟨r₁, hev, hden, ?_⟩
  intro hin hed
  have hok' : stateOKB D r₁.nextState = true ∧ flopShapeOKB D r₁.nextState = true := by
    unfold stateFullOKB at hok; simpa only [Bool.and_eq_true] using hok
  refine admits_run_full hA ?_
  unfold runtimeFullOKB runtimeOKB
  simp only [Bool.and_eq_true]
  refine ⟨⟨⟨⟨hok'.1, hin⟩, ?_⟩, hed⟩, hok'.2⟩
  exact flopBoundOKB_of_flopSize (admits_semwf hA) (stateOKB_flopSize hok'.1)

/-! ### Executed: the refinement bites exactly where the size check did not

`mixedDesign`, `twoMemDesign` and `laneDesign` declare no flops, so the flop
refinement is vacuous for them and the canonical state passes either way —
including `laneDesign`, which is the `byte_w = 8` design, so the strengthened
route carries the lane distinction too. -/

#guard stateFullOKB mixedDesign (initStateOf mixedDesign) == true
#guard stateFullOKB twoMemDesign (initStateOf twoMemDesign) == true
#guard stateFullOKB laneDesign (initStateOf laneDesign) == true
#guard flopShapeOKB mixedDesign zeroState == true
#guard flopShapeOKB mixedDesign initState == true

#print axioms srcFlopNext_width
#print axioms admits_run_full
#print axioms admits_run_full_init
#print axioms admits_run_full_two

#print axioms memImgOKB_sound
#print axioms stateOKB_shape
#print axioms admits_run

end HwMem
end Projection
