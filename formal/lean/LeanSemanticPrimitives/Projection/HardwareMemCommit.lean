/-
# Clock-controlled memory commit, over the ACTUAL `hwMemS` / `hwMemP`

`memNext` and `memNexts` are the last per-memory step before a whole cycle.
This module proves them in general and relates the COMPLETE resulting memory
array to `Compiler.interpretDesign`'s own `srcMemNext`.

## What is proved, and in what form

* **Active clock** — the memory's own `MemoryDesc.nextImg` slot is selected out
  of the typed final `PrefixRelM`, as the ACTUAL image that slot holds, not as
  a function postulated to exist.
* **Quiet clock** — the EXACT prior first-order image comes back, word for
  word, from `memAt` on the incoming state's image list.
* **Every address, not sampled reads** — the correspondence is stated as an
  equality of `MemImg.denote` against `srcMemNext`, i.e. an equality of
  `Int → BV` FUNCTIONS, so it holds at every address simultaneously.  No read
  is sampled anywhere in the statement.
* **Shape for the next step** — under the static policy `NextImgGeomOK` the
  committed array again satisfies `StateShapeOK`, so a design stays admissible
  across cycles.

Kind, slot and clock facts are taken from the real checker and WF records —
`Direct.DesignSemWF.memKinds` for "the next-image slot is a memory",
`memClocks` for "the clock ordinal is declared", `DesignCertWF.slotsInRange`
for the slot — never assumed.

## What `StateShapeOK` is NOT

`StateShapeOK` constrains **table length only** (`FullTable`).  It is not
complete admission, and this module does not pretend otherwise.  Still explicit
and still owed:

* **word and default widths** — that each stored word and the default are
  `dw` bits;
* **data and mask widths** at a write, beyond the address width that
  `WriteAddrFits` already pins;
* **ROM initialisation** — a `memConst` keeps its actual `contents.size` and
  its own default, is assigned no mutable geometry, and is therefore never a
  write target.

Nothing below weakens those; they are named so that `FullTable` is not read as
more than it is.

## How the admission checker is meant to establish `NextImgGeomOK`

`NextImgGeomOK D G` says each memory's `nextImg` slot carries that memory's
declared address width.  `checkDesign` already establishes the KIND half
(`memKinds`: the slot is a memory).  The geometry half is the natural
refinement: propagate a slot's address width the way `WritePropagates` does at
a write node and compare it with `MemoryDesc.aw`, which is a static walk over
the dense slot order.  It is carried as a premise here rather than asserted,
and the checker extension is not claimed to exist yet.
-/
import LeanSemanticPrimitives.Projection.HardwareMemFold

namespace Projection
namespace HwMem

open Surface Compiler Projection.Hw Projection.HwLocal

set_option maxHeartbeats 4000000
set_option maxRecDepth 100000

/-! ## 1. Reading a memory slot's ACTUAL image out of the witness list -/

/-- The image a slot holds, or the absent image if it is not a memory slot. -/
def slotImg (ss : List SlotV) (k : Nat) : MemImg :=
  match ss[k]? with
  | some (.img g) => g
  | _             => Hardware.absentImg

theorem slotImg_eq {ss : List SlotV} {k : Nat} {g : MemImg} (h : k < ss.length)
    (hv : ss[k]'h = .img g) : slotImg ss k = g := by
  unfold slotImg
  rw [List.getElem?_eq_getElem h, hv]

/-- A slot the CHECKER calls a memory holds an actual image, and that image
denotes the reference's slot value. -/
theorem memSlot_img {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {m : Nat} {ss : List SlotV}
    (hpre : PrefixRelM D i st G m ss) {k : Nat} (hk : k < ss.length)
    (hkind : Direct.slotIsMem D k = some true) :
    ss[k]'hk = .img (slotImg ss k)
    ∧ MemImg.denote (slotImg ss k) = (slotValM D i st k).asMem := by
  obtain ⟨g, hv, _, hden⟩ := dep_mem hpre hk hkind
  rw [slotImg_eq hk hv]
  exact ⟨hv, hden⟩

/-- Reading a memory slot through the object's `slot`, with the length coercion
handled once.  Stated with a NAMED environment so nothing is left to
metavariable plumbing. -/
theorem slot_readImg {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {m : Nat} {ss : List SlotV}
    (hpre : PrefixRelM D i st G m ss) {k : Nat} (hk : k < ss.length)
    (hkind : Direct.slotIsMem D k = some true)
    {σ : SEnv} {ee en es : SExp}
    (henv : SEval hwS σ ee (objEnv (ss.map SlotV.enc)))
    (hlen : SEval hwS σ en (.int (Int.ofNat ss.length)))
    (hes : SEval hwS σ es (.int (Int.ofNat k))) :
    SEval hwMemS σ (.call "slot" [ee, en, es]) (MemImg.encMemImg (slotImg ss k)) := by
  obtain ⟨hv, -⟩ := memSlot_img hpre hk hkind
  have hk' : k < (ss.map SlotV.enc).length := by simpa using hk
  have hlen' : SEval hwS σ en (.int (Int.ofNat (ss.map SlotV.enc).length)) := by
    rw [List.length_map]
    exact hlen
  have h := lift (slot_agree hk' henv hlen' hes)
  rw [List.getElem_map, hv] at h
  exact h

/-! ## 2. One memory's next image -/

/-- The typed next image: the `nextImg` slot's ACTUAL image on an active clock,
the EXACT prior image when the clock is quiet. -/
def memNextOf (e : ClockEdges) (st : StateM) (ss : List SlotV) (idx : Nat)
    (md : MemoryDesc) : MemImg :=
  Hardware.memCommit (fires e md.clock) (slotImg ss md.nextImg)
    (Hardware.srcMemImg st idx)

@[simp] theorem memNextOf_quiet (e : ClockEdges) (st : StateM) (ss : List SlotV)
    (idx : Nat) (md : MemoryDesc) (hq : fires e md.clock = false) :
    memNextOf e st ss idx md = Hardware.srcMemImg st idx := by
  simp [memNextOf, hq]

@[simp] theorem memNextOf_active (e : ClockEdges) (st : StateM) (ss : List SlotV)
    (idx : Nat) (md : MemoryDesc) (ha : fires e md.clock = true) :
    memNextOf e st ss idx md = slotImg ss md.nextImg := by
  simp [memNextOf, ha]

/-- **`memNext` computes it**, on the real program. -/
theorem memNext_agree {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {m : Nat} {ss : List SlotV} {e : ClockEdges}
    (hpre : PrefixRelM D i st G m ss)
    {idx : Nat} {md : MemoryDesc}
    (hclk : md.clock < e.size)
    (hnext : md.nextImg < ss.length)
    (hkind : Direct.slotIsMem D md.nextImg = some true)
    {σ : SEnv} {em ee eenv en ems eidx : SExp}
    (h1 : SEval hwMemS σ em (encMemory md))
    (h2 : SEval hwMemS σ ee (encEdges e))
    (h3 : SEval hwMemS σ eenv (objEnv (ss.map SlotV.enc)))
    (h4 : SEval hwMemS σ en (.int (Int.ofNat ss.length)))
    (h5 : SEval hwMemS σ ems (encListG MemImg.encMemImg st.mems.toList))
    (h6 : SEval hwMemS σ eidx (encNat idx)) :
    SEval hwMemS σ (.call "memNext" [em, ee, eenv, en, ems, eidx])
      (MemImg.encMemImg (memNextOf e st ss idx md)) := by
  refine SEval_call6 h1 h2 h3 h4 h5 h6 rfl rfl ?_
  refine SEval_switch_of_tag (.ref rfl) rfl rfl ?_
  refine SEval_ite_of_bool (bb := fires e md.clock)
    (lift (firesAt_agree hclk (.ref rfl) (.ref rfl))) ?_ ?_
  · -- ACTIVE: the nextImg slot's actual image
    intro ha
    rw [memNextOf_active e st ss idx md ha]
    exact slot_readImg hpre hnext hkind (.ref rfl) (.ref rfl) (.ref rfl)
  · -- QUIET: the exact prior image
    intro hq
    rw [memNextOf_quiet e st ss idx md hq]
    have hx : MemImg.encMemImg (Hardware.srcMemImg st idx)
        = MemImg.encMemImg (if (0:Int) ≤ (Int.ofNat idx)
            then st.mems.toList[(Int.ofNat idx).toNat]?.getD Hardware.absentImg
            else Hardware.absentImg) := by
      simp [Hardware.srcMemImg]
    rw [hx]
    exact memAt_sound st.mems.toList (Int.ofNat idx) (.ref rfl) (.ref rfl)

/-! ## 3. The whole memory array -/

/-- The object's fold, as a function: element `p` of the list gets index
`idx + p`, exactly as `memNexts` increments. -/
def memNextsOf (e : ClockEdges) (st : StateM) (ss : List SlotV) :
    Nat → List MemoryDesc → List MemImg
  | _,   []          => []
  | idx, md :: rest  => memNextOf e st ss idx md :: memNextsOf e st ss (idx + 1) rest

@[simp] theorem memNextsOf_length (e : ClockEdges) (st : StateM) (ss : List SlotV) :
    ∀ (idx : Nat) (mems : List MemoryDesc),
      (memNextsOf e st ss idx mems).length = mems.length
  | _,   []       => rfl
  | idx, _ :: rest => by
      simp [memNextsOf, memNextsOf_length e st ss (idx + 1) rest]

/-- **`memNexts` computes it**, for every suffix and every starting index. -/
theorem memNexts_agree {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {m : Nat} {ss : List SlotV} {e : ClockEdges}
    (hpre : PrefixRelM D i st G m ss) :
    ∀ (mems : List MemoryDesc) (idx : Nat) (σ : SEnv) (eml ee eenv en ems eidx : SExp),
      (∀ md ∈ mems, md.clock < e.size) →
      (∀ md ∈ mems, md.nextImg < ss.length) →
      (∀ md ∈ mems, Direct.slotIsMem D md.nextImg = some true) →
      SEval hwMemS σ eml (encListG encMemory mems) →
      SEval hwMemS σ ee (encEdges e) →
      SEval hwMemS σ eenv (objEnv (ss.map SlotV.enc)) →
      SEval hwMemS σ en (.int (Int.ofNat ss.length)) →
      SEval hwMemS σ ems (encListG MemImg.encMemImg st.mems.toList) →
      SEval hwMemS σ eidx (encNat idx) →
      SEval hwMemS σ (.call "memNexts" [eml, ee, eenv, en, ems, eidx])
        (encListG MemImg.encMemImg (memNextsOf e st ss idx mems))
  | [],         _,   _, _, _, _, _, _, _, _,     _,     _,     hl, he, hv, hn, hms, hi => by
      refine SEval_call6 hl he hv hn hms hi rfl rfl ?_
      exact SEval.iteT (SEval_prim1 (.ref rfl) rfl) .lit
  | md :: rest, idx, _, _, _, _, _, _, _, hclk, hnext, hkind, hl, he, hv, hn, hms, hi => by
      refine SEval_call6 hl he hv hn hms hi rfl rfl ?_
      refine SEval.iteF (SEval_prim1 (.ref rfl) rfl) ?_
      refine SEval_consP ?_ ?_
      · exact memNext_agree hpre (hclk md List.mem_cons_self)
          (hnext md List.mem_cons_self) (hkind md List.mem_cons_self)
          (SEval_hd (.ref rfl)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)
      · exact memNexts_agree hpre rest (idx + 1) _ _ _ _ _ _ _
          (fun x hx => hclk x (List.mem_cons_of_mem _ hx))
          (fun x hx => hnext x (List.mem_cons_of_mem _ hx))
          (fun x hx => hkind x (List.mem_cons_of_mem _ hx))
          (SEval_tl (.ref rfl)) (.ref rfl) (.ref rfl) (.ref rfl) (.ref rfl)
          (SEval_prim2 (.ref rfl) .lit (by simp [evalPrim, encNat] <;> omega))

/-! ## 4. Correspondence with the reference, at EVERY address

`MemImg.denote` is an `Int → BV` function, so the equality below holds at every
address at once.  Nothing here samples a read. -/

theorem memNextOf_denote {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {m : Nat} {ss : List SlotV} {e : ClockEdges}
    (hpre : PrefixRelM D i st G m ss)
    {idx : Nat} {md : MemoryDesc}
    (hnext : md.nextImg < ss.length)
    (hkind : Direct.slotIsMem D md.nextImg = some true) :
    MemImg.denote (memNextOf e st ss idx md)
      = Compiler.srcMemNext (slotValM D i st) e (StateM.denote st) idx md := by
  obtain ⟨-, hden⟩ := memSlot_img hpre hnext hkind
  unfold memNextOf Compiler.srcMemNext
  rw [Hardware.memCommit_denote e st idx md (slotImg ss md.nextImg)
        ((slotValM D i st md.nextImg).asMem) hden]

/-- …and the whole array, element by element and therefore address by address. -/
theorem memNextsOf_denote {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {m : Nat} {ss : List SlotV} {e : ClockEdges}
    (hpre : PrefixRelM D i st G m ss) :
    ∀ (mems : List MemoryDesc) (idx p : Nat) (md : MemoryDesc),
      (∀ md' ∈ mems, md'.nextImg < ss.length) →
      (∀ md' ∈ mems, Direct.slotIsMem D md'.nextImg = some true) →
      mems[p]? = some md →
      ∃ g, (memNextsOf e st ss idx mems)[p]? = some g
         ∧ MemImg.denote g
             = Compiler.srcMemNext (slotValM D i st) e (StateM.denote st) (idx + p) md
  | [],         _,   p, _,  _,     _,     hp => by simp at hp
  | md' :: rest, idx, p, md, hnext, hkind, hp => by
      cases p with
      | zero =>
          have hmd : md' = md := by simpa using hp
          subst hmd
          refine ⟨memNextOf e st ss idx md', by simp [memNextsOf], ?_⟩
          simpa using memNextOf_denote hpre (hnext md' List.mem_cons_self)
            (hkind md' List.mem_cons_self)
      | succ q =>
          have hq : rest[q]? = some md := by simpa using hp
          obtain ⟨g, hg, hd⟩ := memNextsOf_denote hpre rest (idx + 1) q md
            (fun x hx => hnext x (List.mem_cons_of_mem _ hx))
            (fun x hx => hkind x (List.mem_cons_of_mem _ hx)) hq
          refine ⟨g, by simpa [memNextsOf] using hg, ?_⟩
          rw [hd]
          congr 1
          omega

/-! ## 5. Indexing the committed array -/

theorem memNextsOf_get {e : ClockEdges} {st : StateM} {ss : List SlotV} :
    ∀ (mems : List MemoryDesc) (base p : Nat) (x : MemoryDesc),
      mems[p]? = some x →
      (memNextsOf e st ss base mems)[p]? = some (memNextOf e st ss (base + p) x)
  | [],        _,    _, _, hp => by simp at hp
  | a :: rest, base, p, x, hp => by
      cases p with
      | zero =>
          have hax : a = x := by simpa using hp
          subst hax
          simp [memNextsOf]
      | succ q =>
          have hq : rest[q]? = some x := by simpa using hp
          have ih := memNextsOf_get (e := e) (st := st) (ss := ss) rest (base + 1) q x hq
          have harg : base + 1 + q = base + (q + 1) := by omega
          rw [harg] at ih
          simpa [memNextsOf] using ih

/-! ## 6. Shape preservation for the NEXT step -/

/-- The static policy: each memory's `nextImg` slot carries that memory's
declared address width.  See the header for how an admission checker is meant
to establish it; it is a premise here, not a claim. -/
def NextImgGeomOK (D : DesignCert) (G : Nat → Option Nat) : Prop :=
  ∀ (idx : Nat) (md : MemoryDesc), D.memories[idx]? = some md →
    G md.nextImg = some md.aw

/-- **The committed image still has the admitted table shape**, on either
branch: from the invariant's geometry when the clock fires, from the incoming
state's shape when it does not. -/
theorem memNextOf_FullTable {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {m : Nat} {ss : List SlotV} {e : ClockEdges}
    (hpre : PrefixRelM D i st G m ss)
    (hstate : StateShapeOK D st) (hpol : NextImgGeomOK D G)
    {idx : Nat} {md : MemoryDesc} (hmd : D.memories[idx]? = some md)
    (hnext : md.nextImg < ss.length)
    (hkind : Direct.slotIsMem D md.nextImg = some true) :
    MemImg.FullTable md.aw (memNextOf e st ss idx md) := by
  unfold memNextOf Hardware.memCommit
  cases hf : fires e md.clock
  · simp only [Bool.false_eq_true, if_false]
    obtain ⟨g, hg, hfull⟩ := hstate idx md hmd
    have hsm : Hardware.srcMemImg st idx = g := by simp [Hardware.srcMemImg, hg]
    rw [hsm]; exact hfull
  · simp only [if_true]
    obtain ⟨hv, -⟩ := memSlot_img hpre hnext hkind
    exact hpre.geom md.nextImg hnext (slotImg ss md.nextImg) hv md.aw (hpol idx md hmd)

/-- …so the state built from the commit is admissible again, and the design can
take another step. -/
theorem stepState_shape {D : DesignCert} {i : RuntimeInput} {st : StateM}
    {G : Nat → Option Nat} {m : Nat} {ss : List SlotV} {e : ClockEdges}
    (hpre : PrefixRelM D i st G m ss)
    (hstate : StateShapeOK D st) (hpol : NextImgGeomOK D G)
    (hnext : ∀ md ∈ D.memories.toList, md.nextImg < ss.length)
    (hkind : ∀ md ∈ D.memories.toList, Direct.slotIsMem D md.nextImg = some true)
    (nf : Array BV) :
    StateShapeOK D
      { flops := nf
      , mems  := (memNextsOf e st ss 0 D.memories.toList).toArray } := by
  intro idx md hmd
  have hlist : D.memories.toList[idx]? = some md := by rw [← hmd]; simp
  have hmem : md ∈ D.memories.toList := by
    obtain ⟨h1, h2⟩ := List.getElem?_eq_some_iff.mp hlist
    exact h2 ▸ List.getElem_mem h1
  have hget := memNextsOf_get (e := e) (st := st) (ss := ss)
    D.memories.toList 0 idx md hlist
  refine ⟨memNextOf e st ss idx md, ?_, ?_⟩
  · simpa using hget
  · exact memNextOf_FullTable hpre hstate hpol hmd (hnext md hmem) (hkind md hmem)

/-! ## 7. Two memories on DISTINCT clocks

Six sources (two mutable images, an address, a byte-enable mask, a read enable
and a scalar operand), four nodes (a read, an intervening scalar `Op_And`, and
one byte-enabled write per memory), and two memories on clock ordinals 0 and 1.
The certificate is accepted by `Direct.checkDesign` itself. -/

def twoMemDesign : DesignCert :=
  { sources  := #[ .memImg 0 5 32        -- slot 0: memory 0's image
                 , .memImg 1 5 32        -- slot 1: memory 1's image
                 , .input 0 5            -- slot 2: address
                 , .input 1 32           -- slot 3: byte-enable mask
                 , .input 2 1            -- slot 4: read enable
                 , .input 3 32 ]         -- slot 5: scalar operand
  , nodes    := #[ { op := .Op_MemRead,      width := 32, deps := #[0, 2, 4] }
                 , { op := .Op_And,          width := 32, deps := #[6, 5] }
                 , { op := .Op_MemWriteBE 1, width := 32, deps := #[0, 2, 7, 3] }
                 , { op := .Op_MemWriteBE 1, width := 32, deps := #[1, 2, 7, 3] } ]
  , outputs  := #[ { slot := 7, width := 32 } ]
  , flops    := #[]
  , memories := #[ { aw := 5, dw := 32, nextImg := 8, clock := 0 }
                 , { aw := 5, dw := 32, nextImg := 9, clock := 1 } ]
  , clocks   := #[{ name := "clk0" }, { name := "clk1" }] }

/- the REAL checker accepts it, and the two next-image slots are memories -/
#guard Direct.acceptsDesign twoMemDesign
#guard twoMemDesign.nodes.toList.all nodeAdmittedB
#guard Direct.slotIsMem twoMemDesign 8 == some true
#guard Direct.slotIsMem twoMemDesign 9 == some true
#guard Direct.slotIsMem twoMemDesign 7 == some false

theorem twoMemDesign_semwf : Direct.DesignSemWF twoMemDesign := by
  have hacc : Direct.acceptsDesign twoMemDesign = true := by decide
  cases hd : Direct.designErrors twoMemDesign with
  | none =>
      exact Direct.checkDesign_sound (u := ()) (by unfold Direct.checkDesign; rw [hd])
  | some e =>
      exfalso
      unfold Direct.acceptsDesign at hacc
      rw [hd] at hacc
      simp at hacc

theorem twoMemDesign_admitted : ∀ c ∈ twoMemDesign.nodes.toList, NodeAdmitted c := by
  intro c hc
  have h : nodeAdmittedB c = true := by revert hc; revert c; decide
  exact (nodeAdmittedB_iff c).mp h

theorem twoMemDesign_arity :
    ∀ c ∈ twoMemDesign.nodes.toList, ArityOK c.op c.deps.toList := by
  intro c hc; revert hc; revert c; decide

/-- Two distinct initialised tables, so a commit on one domain is visibly
different from a commit on the other. -/
def imgA : MemImg :=
  ⟨(List.range 32).map (fun k => mk_bv 32 (Int.ofNat k + 1)), mk_bv 32 0⟩
def imgB : MemImg :=
  ⟨(List.range 32).map (fun k => mk_bv 32 (Int.ofNat k + 100)), mk_bv 32 0⟩

def twoMemState : StateM := { flops := #[], mems := #[imgA, imgB] }

theorem imgA_full : MemImg.FullTable 5 imgA := by simp [MemImg.FullTable, imgA]
theorem imgB_full : MemImg.FullTable 5 imgB := by simp [MemImg.FullTable, imgB]

theorem twoMemState_shape : StateShapeOK twoMemDesign twoMemState := by
  intro idx md hmd
  rcases idx with _|_|idx
  · have hmd' : ({ aw := 5, dw := 32, nextImg := 8, clock := 0 } : MemoryDesc) = md := by
      simpa [twoMemDesign] using hmd
    subst hmd'; exact ⟨imgA, rfl, imgA_full⟩
  · have hmd' : ({ aw := 5, dw := 32, nextImg := 9, clock := 1 } : MemoryDesc) = md := by
      simpa [twoMemDesign] using hmd
    subst hmd'; exact ⟨imgB, rfl, imgB_full⟩
  · simp [twoMemDesign] at hmd

def twoMemG : Nat → Option Nat
  | 0 => some 5
  | 1 => some 5
  | 8 => some 5
  | 9 => some 5
  | _ => none

theorem twoMemG_src :
    ∀ k, k < twoMemDesign.sources.size → twoMemG k = srcGeomOf twoMemDesign k := by
  intro k hk
  have h6 : k < 6 := by simpa [twoMemDesign] using hk
  rcases k with _|_|_|_|_|_|k
  · rfl
  · rfl
  · rfl
  · rfl
  · rfl
  · rfl
  · omega

/-- **The next-image geometry policy holds for this design**: each memory's
`nextImg` slot carries that memory's declared address width. -/
theorem twoMemG_nextImg : NextImgGeomOK twoMemDesign twoMemG := by
  intro idx md hmd
  rcases idx with _|_|idx
  · have hmd' : ({ aw := 5, dw := 32, nextImg := 8, clock := 0 } : MemoryDesc) = md := by
      simpa [twoMemDesign] using hmd
    subst hmd'; rfl
  · have hmd' : ({ aw := 5, dw := 32, nextImg := 9, clock := 1 } : MemoryDesc) = md := by
      simpa [twoMemDesign] using hmd
    subst hmd'; rfl
  · simp [twoMemDesign] at hmd

theorem twoMem_addr_width (i : RuntimeInput) (st : StateM) :
    (slotValM twoMemDesign i st 2).asBV.width = 5 := by
  rw [slotValM_source (D := twoMemDesign) (i := i) (st := st)
        (k := 2) (by simp [twoMemDesign])]
  simp [twoMemDesign, sourceValue, CertVal.asBV, bv_resize, mk_bv]

theorem twoMemG_prop : ∀ (j : Nat) (c : DenseNodeCert),
    twoMemDesign.nodes[j]? = some c → WritePropagates twoMemDesign twoMemG j c := by
  intro j c hc bw hop d0 hd0
  rcases j with _|_|_|_|j
  · have hc' : ({ op := .Op_MemRead, width := 32, deps := #[0, 2, 4] } : DenseNodeCert) = c := by
      simpa [twoMemDesign] using hc
    subst hc'; simp at hop
  · have hc' : ({ op := .Op_And, width := 32, deps := #[6, 5] } : DenseNodeCert) = c := by
      simpa [twoMemDesign] using hc
    subst hc'; simp at hop
  · have hc' : ({ op := .Op_MemWriteBE 1, width := 32, deps := #[0, 2, 7, 3] } : DenseNodeCert) = c := by
      simpa [twoMemDesign] using hc
    subst hc'
    have h0 : (0 : Nat) = d0 := by simpa using hd0
    subst h0; rfl
  · have hc' : ({ op := .Op_MemWriteBE 1, width := 32, deps := #[1, 2, 7, 3] } : DenseNodeCert) = c := by
      simpa [twoMemDesign] using hc
    subst hc'
    have h1 : (1 : Nat) = d0 := by simpa using hd0
    subst h1; rfl
  · simp [twoMemDesign] at hc

theorem twoMemG_fits (i : RuntimeInput) (st : StateM) :
    ∀ c ∈ twoMemDesign.nodes.toList, WriteAddrFits twoMemDesign i st twoMemG c := by
  intro c hc bw hop d0 d1 d3 hd0 hd1 hd3 _hen
  have hc4 : c = ({ op := .Op_MemRead, width := 32, deps := #[0, 2, 4] } : DenseNodeCert)
           ∨ c = ({ op := .Op_And, width := 32, deps := #[6, 5] } : DenseNodeCert)
           ∨ c = ({ op := .Op_MemWriteBE 1, width := 32, deps := #[0, 2, 7, 3] } : DenseNodeCert)
           ∨ c = ({ op := .Op_MemWriteBE 1, width := 32, deps := #[1, 2, 7, 3] } : DenseNodeCert) := by
    simpa [twoMemDesign] using hc
  rcases hc4 with h | h | h | h <;> subst h
  · simp at hop
  · simp at hop
  · have h0 : (0 : Nat) = d0 := by simpa using hd0
    have h1 : (2 : Nat) = d1 := by simpa using hd1
    subst h0; subst h1
    exact ⟨5, rfl, by simp [twoMem_addr_width]⟩
  · have h0 : (1 : Nat) = d0 := by simpa using hd0
    have h1 : (2 : Nat) = d1 := by simpa using hd1
    subst h0; subst h1
    exact ⟨5, rfl, by simp [twoMem_addr_width]⟩

/-- **The whole chain, instantiated**: the node fold produces a typed witness
list, `memNexts` commits each memory on ITS OWN clock, and the resulting state
still carries the admitted geometry — so another step is possible. -/
theorem twoMem_commit (i : RuntimeInput) (e : ClockEdges) (he : 2 ≤ e.size) :
    ∃ ss' : List SlotV,
      PrefixRelM twoMemDesign i twoMemState twoMemG
        (twoMemDesign.sources.size + twoMemDesign.nodes.size) ss'
      ∧ (∀ (σ : SEnv) (eml ee eenv en ems eidx : SExp),
          SEval hwMemS σ eml (encListG encMemory twoMemDesign.memories.toList) →
          SEval hwMemS σ ee (encEdges e) →
          SEval hwMemS σ eenv (objEnv (ss'.map SlotV.enc)) →
          SEval hwMemS σ en (.int (Int.ofNat ss'.length)) →
          SEval hwMemS σ ems (encListG MemImg.encMemImg twoMemState.mems.toList) →
          SEval hwMemS σ eidx (encNat 0) →
          SEval hwMemS σ (.call "memNexts" [eml, ee, eenv, en, ems, eidx])
            (encListG MemImg.encMemImg
              (memNextsOf e twoMemState ss' 0 twoMemDesign.memories.toList)))
      ∧ StateShapeOK twoMemDesign
          { flops := #[]
          , mems  := (memNextsOf e twoMemState ss' 0
                        twoMemDesign.memories.toList).toArray } := by
  obtain ⟨ss', hss', -⟩ :=
    sourcesThenNodes (D := twoMemDesign) (i := i) (st := twoMemState) (G := twoMemG)
      (designCertWF_of_semwf twoMemDesign_semwf) twoMemDesign_semwf twoMemState_shape
      twoMemG_src twoMemDesign_admitted twoMemDesign_arity twoMemG_prop
      (twoMemG_fits i twoMemState)
  have hlen : ss'.length = 10 := by
    have := hss'.len; simpa [twoMemDesign] using this
  have hclk : ∀ md ∈ twoMemDesign.memories.toList, md.clock < e.size := by
    intro md hmd
    have : md = { aw := 5, dw := 32, nextImg := 8, clock := 0 }
         ∨ md = { aw := 5, dw := 32, nextImg := 9, clock := 1 } := by
      simpa [twoMemDesign] using hmd
    rcases this with h | h <;> subst h <;> simp <;> omega
  have hnext : ∀ md ∈ twoMemDesign.memories.toList, md.nextImg < ss'.length := by
    intro md hmd
    have : md = { aw := 5, dw := 32, nextImg := 8, clock := 0 }
         ∨ md = { aw := 5, dw := 32, nextImg := 9, clock := 1 } := by
      simpa [twoMemDesign] using hmd
    rw [hlen]
    rcases this with h | h <;> subst h <;> simp
  have hkind : ∀ md ∈ twoMemDesign.memories.toList,
      Direct.slotIsMem twoMemDesign md.nextImg = some true := by
    intro md hmd
    have : md = { aw := 5, dw := 32, nextImg := 8, clock := 0 }
         ∨ md = { aw := 5, dw := 32, nextImg := 9, clock := 1 } := by
      simpa [twoMemDesign] using hmd
    rcases this with h | h <;> subst h <;> decide
  refine ⟨ss', hss', ?_, ?_⟩
  · intro σ eml ee eenv en ems eidx hl hev hv hn hms hi
    exact memNexts_agree hss' twoMemDesign.memories.toList 0 σ eml ee eenv en ems eidx
      hclk hnext hkind hl hev hv hn hms hi
  · exact stepState_shape hss' twoMemState_shape twoMemG_nextImg hnext hkind #[]

/-! ### Executed clock controls

Address 20: memory 0 holds 21 there, memory 1 holds 120.  The read returns 21,
the scalar `Op_And` with `0xF` gives 5, and each write stores 5 into its own
memory.  Which of those actually lands is decided by the EDGE VECTOR. -/

private def twoMemEnv0 (st : StateM) (i : RuntimeInput) : Val :=
  objEnv ((twoMemDesign.sources.toList.map (srcSlotV i st)).map SlotV.enc)

private def runNodes (st : StateM) (i : RuntimeInput) : EvalResult :=
  evalFuel 400000 hwMemP []
    (.call ((idOf "evalNodesM").getD 999)
       [ .lit (encListG encNode twoMemDesign.nodes.toList)
       , .lit (twoMemEnv0 st i)
       , .lit (.int 6) ])

private def runCommit (st : StateM) (i : RuntimeInput) (e : ClockEdges) :
    Option (MemImg × MemImg) :=
  match runNodes st i with
  | .value env =>
      (match evalFuel 400000 hwMemP []
          (.call ((idOf "memNexts").getD 999)
             [ .lit (encListG encMemory twoMemDesign.memories.toList)
             , .lit (encEdges e)
             , .lit env
             , .lit (.int 10)
             , .lit (encListG MemImg.encMemImg st.mems.toList)
             , .lit (.int 0) ]) with
       | .value (.cons a (.cons b .nil)) =>
           (match MemImg.decMemImg a, MemImg.decMemImg b with
            | some g0, some g1 => some (g0, g1)
            | _,       _       => none)
       | _ => none)
  | _ => none

private def inWrite : RuntimeInput :=
  #[mk_bv 5 20, mk_bv 32 0xFFFFFFFF, mk_bv 1 1, mk_bv 32 0xF]
private def inNoWrite : RuntimeInput :=
  #[mk_bv 5 20, mk_bv 32 0,          mk_bv 1 1, mk_bv 32 0xF]

/- the starting contents at address 20 -/
#guard imgA.get 20 == mk_bv 32 21
#guard imgB.get 20 == mk_bv 32 120

/- CLOCK 0 ACTIVE ONLY: memory 0 commits the write, memory 1 holds exactly -/
#guard (match runCommit twoMemState inWrite #[true, false] with
        | some (g0, g1) => g0.get 20 == mk_bv 32 5 && g1 == imgB
        | none          => false)

/- CLOCK 1 ACTIVE ONLY: memory 1 commits, memory 0 holds exactly -/
#guard (match runCommit twoMemState inWrite #[false, true] with
        | some (g0, g1) => g0 == imgA && g1.get 20 == mk_bv 32 5
        | none          => false)

/- BOTH QUIET: both images come back EXACTLY, which is a genuine control --
   the committed values (5, 5) differ from the held ones (21, 120), so a proof
   that ignored the clock would be wrong here -/
#guard (match runCommit twoMemState inWrite #[false, false] with
        | some (g0, g1) => g0 == imgA && g1 == imgB
        | none          => false)
#guard (imgA.get 20 != mk_bv 32 5 && imgB.get 20 != mk_bv 32 5)

/- BOTH ACTIVE: both commit -/
#guard (match runCommit twoMemState inWrite #[true, true] with
        | some (g0, g1) => g0.get 20 == mk_bv 32 5 && g1.get 20 == mk_bv 32 5
        | none          => false)

/- an EXACT DISABLED no-op: a zero byte-enable leaves both images alone even
   with both clocks active -/
#guard (match runCommit twoMemState inNoWrite #[true, true] with
        | some (g0, g1) => g0 == imgA && g1 == imgB
        | none          => false)

/- …and the shape survives: still a full 2^5 table on both -/
#guard (match runCommit twoMemState inWrite #[true, true] with
        | some (g0, g1) => g0.words.length == 32 && g1.words.length == 32
        | none          => false)

#print axioms twoMemDesign_semwf
#print axioms twoMemG_src
#print axioms twoMemG_nextImg
#print axioms twoMemG_prop
#print axioms twoMemG_fits
#print axioms twoMem_commit

#print axioms memSlot_img
#print axioms slot_readImg
#print axioms memNextsOf_get
#print axioms memNext_agree
#print axioms memNexts_agree
#print axioms memNextOf_denote
#print axioms memNextsOf_denote
#print axioms memNextOf_FullTable
#print axioms stepState_shape

end HwMem
end Projection
